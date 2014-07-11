/*
 * Lexer for HOCON properties file.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 9 July 2014
 * All rights reserved.
 */

#include "tsconfig_lex.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "tsconfig_err.h"

// Default amount to buffer when searching ahead
#define LEX_PEEK_BATCH_SIZE 32

static inline void set_nostr_tok(tscfg_tok_tag tag, tscfg_tok *tok);
static inline tscfg_tok_tag tag_from_char(char c);

static tscfg_rc lex_peek(tscfg_lex_state *lex, char *buf, size_t len, size_t *got);
static tscfg_rc lex_read_more(tscfg_lex_state *lex, size_t bytes);
static tscfg_rc lex_read(tscfg_lex_state *lex, unsigned char *buf, size_t bytes,
                         size_t *read_bytes);
static void lex_eat(tscfg_lex_state *lex, size_t bytes);

static tscfg_rc extract_hocon_ws(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str);
static tscfg_rc extract_comment_or_hocon_unquoted(tscfg_lex_state *lex,
                                tscfg_tok *tok, bool include_comm_str);
static tscfg_rc extract_line_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str);
static tscfg_rc extract_multiline_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str);

static tscfg_rc extract_json_number(tscfg_lex_state *lex, char c, tscfg_tok *tok);
static tscfg_rc extract_hocon_str(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_json_str(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_json_str_escape(tscfg_lex_state *lex, char *escape,
                    size_t len, char *result, size_t *consumed);
static tscfg_rc extract_hocon_multiline_str(tscfg_lex_state *lex,
                                            tscfg_tok *tok);
static tscfg_rc extract_hocon_unquoted(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_keyword_or_hocon_unquoted(tscfg_lex_state *lex, char c,
                                                  tscfg_tok *tok);

static bool is_hocon_whitespace(char c);
static bool is_hocon_newline(char c);
static bool is_hocon_unquoted_char(char c);
static bool is_comment_start(const char *buf, size_t len);

// TODO: HOCON spec specifies unicode whitespace too
#define CASE_HOCON_WHITESPACE \
  case ' ': case '\t': case '\n': case '\r': case '\v': case '\f': \
  case 0x01C /* file sep */: case 0x01D /* group sep */: \
  case 0x01E /* record sep */: case 0x01F /* unit sep */

static void lex_report_err(tscfg_lex_state *lex, const char *fmt, ...);

tscfg_rc tscfg_lex_init(tscfg_lex_state *lex, ts_config_input in) {
  lex->in = in;

  size_t buf_init_size = 512;
  lex->buf = malloc(buf_init_size);
  TSCFG_CHECK_MALLOC(lex->buf);
  lex->buf_size = buf_init_size;
  lex->buf_len = 0;

  return TSCFG_OK;
}

void tscfg_lex_finalize(tscfg_lex_state *lex) {
  // Invalidate input
  lex->in.kind = TS_CONFIG_IN_NONE;

  free(lex->buf);
  lex->buf = NULL;
  lex->buf_size = 0;
  lex->buf_len = 0;
}

tscfg_rc tscfg_read_tok(tscfg_lex_state *lex, tscfg_tok *tok,
                        tscfg_lex_opts opts) {
  assert(lex != NULL);
  assert(tok != NULL);

  tscfg_rc rc;

  // Next character should be start of token.
  char c;
  size_t got;
  rc = lex_peek(lex, &c, 1, &got);
  TSCFG_CHECK(rc);

  if (got == 0) {
    set_nostr_tok(TSCFG_TOK_EOF, tok);
    return TSCFG_OK;
  }

  assert(got == 1);

  switch (c) {
    CASE_HOCON_WHITESPACE:
      return extract_hocon_ws(lex, tok, opts.include_ws_str);
    case '"':
      // string, either single quoted or triple quoted
      return extract_hocon_str(lex, tok);
    case '{':
    case '}':
    case '(':
    case ')':
    case '[':
    case ']':
    case ',':
    case '=':
    case ':':
      // Single character toks
      lex_eat(lex, 1);
      set_nostr_tok(tag_from_char(c), tok);
      return TSCFG_OK;
    case '+': {
      lex_eat(lex, 1);

      rc = lex_peek(lex, &c, 1, &got);
      TSCFG_CHECK(rc);

      if (got == 0) {
        lex_report_err(lex, "Trailing + at end of file");
        return TSCFG_ERR_SYNTAX;
      } else if (c != '=') {
        // Must be += operator
        lex_eat(lex, 1);
        set_nostr_tok(TSCFG_TOK_PLUSEQUAL, tok);
        return TSCFG_OK;
      } else {
        lex_report_err(lex, "Invalid char %c after =", c);
        return TSCFG_ERR_SYNTAX;
      }
    }

    case '-': case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      return extract_json_number(lex, c, tok);

    case 't':
    case 'f':
    case 'n':
      // try to parse as keyword, otherwise unquoted string
      return extract_keyword_or_hocon_unquoted(lex, c, tok);

    case '#':
      lex_eat(lex, 1);
      return extract_line_comment(lex, tok, opts.include_comm_str);

    case '/':
      return extract_comment_or_hocon_unquoted(lex, tok, opts.include_comm_str);

    default:
      if (is_hocon_unquoted_char(c)) {
        return extract_hocon_unquoted(lex, tok);
      } else {
        lex_report_err(lex, "Unexpected character: %c", c);
        return TSCFG_ERR_SYNTAX;
      }
  }
}

/*
 * Set token without string
 */
static inline void set_nostr_tok(tscfg_tok_tag tag, tscfg_tok *tok) {
  tok->tag = tag;
  tok->str = NULL;
  tok->length = 0;
}

/*
 * Translate single character token into tag.
 */
static inline tscfg_tok_tag tag_from_char(char c) {
  switch (c) {
    case '{':
      return TSCFG_TOK_OPEN_BRACE;
    case '}':
      return TSCFG_TOK_CLOSE_BRACE;
    case '(':
      return TSCFG_TOK_OPEN_PAREN;
    case ')':
      return TSCFG_TOK_CLOSE_PAREN;
    case '[':
      return TSCFG_TOK_OPEN_SQUARE;
    case ']':
      return TSCFG_TOK_CLOSE_SQUARE;
    case ',':
      return TSCFG_TOK_COMMA;
    case '=':
      return TSCFG_TOK_EQUAL;
    case ':':
      return TSCFG_TOK_COLON;
    default:
      // Should not get here
      assert(false);
  }
}

static tscfg_rc lex_peek(tscfg_lex_state *lex, char *buf, size_t len, size_t *got) {
  tscfg_rc rc;

  if (lex->buf_len < len) {
    // Attempt to read more
    size_t toread = len - lex->buf_len;
    rc = lex_read_more(lex, toread);
    TSCFG_CHECK(rc);
  }

  *got = lex->buf_len < len ? lex->buf_len : len;
  memcpy(buf, lex->buf, *got);
  return TSCFG_OK;
}

/*
 * Read additional bytes into buffers.
 * If hits end of file, will not read as many as requested
 */
static tscfg_rc lex_read_more(tscfg_lex_state *lex, size_t bytes) {
  tscfg_rc rc;
  size_t size_needed = bytes + lex->buf_len;

  if (size_needed >= lex->buf_size) {
    void *tmp = realloc(lex->buf, size_needed);
    TSCFG_CHECK_MALLOC(tmp);

    lex->buf = tmp;
    lex->buf_size = size_needed;
  }

  size_t read_bytes = 0;
  rc = lex_read(lex, &lex->buf[lex->buf_len], bytes, &read_bytes);
  TSCFG_CHECK(rc);

  lex->buf_len += read_bytes;

  return TSCFG_OK;
}

/*
 * Lexer read from input.
 * read_bytes: on success, set to number of bytes read.  Only < bytes if EOF.
 */
static tscfg_rc lex_read(tscfg_lex_state *lex, unsigned char *buf, size_t bytes,
                         size_t *read_bytes) {
  ts_config_input *in = &lex->in;
  if (in->kind == TS_CONFIG_IN_FILE) {

    size_t read = fread(buf, 1, bytes, in->data.f);
    if (read != bytes) {
      // Need to distinguish between eof and error
      int err_code = ferror(in->data.f);
      if (err_code != 0) {
        lex_report_err(lex, "Error reading input");
        return TSCFG_ERR_IO;
      }
    }

    *read_bytes = read;
  } else if (in->kind == TS_CONFIG_IN_STR) {
    assert(in->data.s.pos <= in->data.s.len);
    size_t remaining = in->data.s.len - in->data.s.pos;
    size_t copy_bytes = (remaining < bytes) ? remaining : bytes;
    memcpy(buf, &in->data.s.str[in->data.s.pos], copy_bytes);
    in->data.s.pos += copy_bytes;

    *read_bytes = copy_bytes;
  } else {
    tscfg_report_err("Unsupported input type: %i", (int)in->kind);
    return TSCFG_ERR_UNIMPL;
  }

  return TSCFG_OK;
}

static void lex_eat(tscfg_lex_state *lex, size_t bytes) {
  assert(bytes <= lex->buf_len);

  /* Bump data forward
   * Note that this is inefficient if we are frequently bumping small portions of a large
   * buffer, since this requires moving the remainder of the buffer.
   */
  size_t remaining = lex->buf_len - bytes;
  if (remaining > 0) {
    memmove(&lex->buf[0], &lex->buf[bytes], remaining);
  }
  lex->buf_len = remaining;
}

static tscfg_rc extract_comment_or_hocon_unquoted(tscfg_lex_state *lex,
                                tscfg_tok *tok, bool include_comm_str) {
  tscfg_rc rc;

  char buf[2];
  size_t got;
  rc = lex_peek(lex, buf, 2, &got);
  TSCFG_CHECK(rc);

  assert(buf[0] == '/'); // Only case handled

  if (buf[1] == '/') {
    lex_eat(lex, 2);
    return extract_line_comment(lex, tok, include_comm_str);
  } else if (buf[1] == '*') {
    lex_eat(lex, 2);
    return extract_multiline_comment(lex, tok, include_comm_str);
  } else {
    // Not comment, interpret as unquoted token
    return extract_hocon_unquoted(lex, tok);
  }
}

static tscfg_rc extract_line_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str) {
  tscfg_rc rc;

  size_t len = 0, str_size;
  char *str = NULL;
  if (include_str) {
    str_size = 32;
    str = malloc(str_size);
    TSCFG_CHECK_MALLOC(str);
  } else {
    str_size = 0;
  }

  char buf_storage[LEX_PEEK_BATCH_SIZE];

  bool end_of_line = false;

  while (!end_of_line) {

    if (include_str) {
      // Resize more aggressively since strings are often long
      size_t min_size = len + LEX_PEEK_BATCH_SIZE + 1;
      if (str_size < min_size) {
        size_t new_size = str_size * 2;
        new_size = (new_size > min_size) ? new_size : min_size;
        void *tmp = realloc(str, new_size);
        TSCFG_CHECK_MALLOC_GOTO(str, cleanup, rc);
        str = tmp;
        str_size = new_size;
      }
    }

    char *buf = include_str ? &str[len] : buf_storage;

    size_t got;
    rc = lex_peek(lex, buf, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got == 0) {
      break;
    }

    size_t num_chars = 0;
    while (num_chars < got) {
      // Don't consume newline, need to produce whitespace token
      if (buf[num_chars] == '\n') {
        end_of_line = true;
        break;
      }
      num_chars++;
    }

    lex_eat(lex, num_chars);
    len += num_chars;
  }

  tok->tag = TSCFG_TOK_COMMENT;
  if (include_str) {
    str[len] = '\0';
    tok->length = len;
    tok->str = str;
  } else {
    tok->length = 0;
    tok->str = 0;
  }

  return TSCFG_OK;

cleanup:
  free(str);
  return rc;
}

/*
 * Search for end of multiline comment, i.e * then /
 */
static tscfg_rc extract_multiline_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str) {
  tscfg_rc rc;

  size_t len = 0, str_size;
  char *str = NULL;
  if (include_str) {
    str_size = 64;
    str = malloc(str_size);
    TSCFG_CHECK_MALLOC(str);
  } else {
    str_size = 0;
  }

  char buf_storage[LEX_PEEK_BATCH_SIZE];

  bool in_comment = true;

  while (in_comment) {

    if (include_str) {
      // Resize more aggressively since comments are often long
      size_t min_size = len + LEX_PEEK_BATCH_SIZE + 1;
      if (str_size < min_size) {
        size_t new_size = str_size * 2;
        new_size = (new_size > min_size) ? new_size : min_size;
        void *tmp = realloc(str, new_size);
        TSCFG_CHECK_MALLOC_GOTO(str, cleanup, rc);
        str = tmp;
        str_size = new_size;
      }
    }

    char *buf = include_str ? &str[len] : buf_storage;

    size_t got;
    rc = lex_peek(lex, buf, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got < 2) {
      // Cannot be comment close, therefore unclosed comment

      lex_eat(lex, got);
      len += got;

      lex_report_err(lex, "/* comment without matching */");
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }

    size_t num_chars = 0;
    while (num_chars < got - 1) {
      if (buf[num_chars] == '*' && buf[num_chars + 1] == '/') {
        num_chars += 2;
        in_comment = false;
        break;
      } else {
        // May not need to check next position
        int advance = (buf[num_chars + 1] == '*') ? 1 : 2;
        num_chars += advance;
      }
    }

    lex_eat(lex, num_chars);
    len += num_chars;
  }

  tok->tag = TSCFG_TOK_COMMENT;
  if (include_str) {
    str[len] = '\0';
    tok->length = len;
    tok->str = str;
  } else {
    tok->length = 0;
    tok->str = 0;
  }

  return TSCFG_OK;

cleanup:
  free(str);
  return rc;
}


/*
 * Remove any leading whitespace characters from input.
 * tok: fill in tok with appropriate type depending on whether we saw at least
 *      one newline.
 * include_str: if true, include string data
 */
static tscfg_rc extract_hocon_ws(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str) {
  tscfg_rc rc;

  size_t len = 0, str_size;
  char *str = NULL;
  if (include_str) {
    str_size = 128;
    str = malloc(str_size);
    TSCFG_CHECK_MALLOC(str);
  } else {
    str_size = 0;
  }

  char buf_storage[LEX_PEEK_BATCH_SIZE];
  bool saw_newline = false;

  while (true) {

    if (include_str) {
      // Resize more aggressively since strings are often long
      size_t min_size = len + LEX_PEEK_BATCH_SIZE + 1;
      if (str_size < min_size) {
        size_t new_size = str_size * 2;
        new_size = (new_size > min_size) ? new_size : min_size;
        void *tmp = realloc(str, new_size);
        TSCFG_CHECK_MALLOC_GOTO(str, cleanup, rc);
        str = tmp;
        str_size = new_size;
      }
    }
    char *buf = include_str ? &str[len] : buf_storage;

    size_t got;
    rc = lex_peek(lex, buf, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    assert(got <= LEX_PEEK_BATCH_SIZE);

    size_t ws_chars = 0;
    while (ws_chars < got &&
           is_hocon_whitespace(buf[ws_chars])) {
      if (is_hocon_newline(buf[ws_chars])) {
        saw_newline = true;
      }
      ws_chars++;
    }

    if (ws_chars > 0) {
      lex_eat(lex, ws_chars);
      len += ws_chars;
    }

    if (ws_chars < got || got == 0) {
      // End of whitespace or file
      break;
    }
  }

  tok->tag = saw_newline ? TSCFG_TOK_WS_NEWLINE : TSCFG_TOK_WS;
  if (include_str) {
    str[len] = '\0';
    tok->length = len;
    tok->str = str;
  } else {
    tok->length = 0;
    tok->str = 0;
  }

  return TSCFG_OK;

cleanup:
  free(str);
  return rc;
}

/*
 * Extract JSON/HOCON number token
 * c: first character of string
 * TODO: doesn't handle exponentials
 */
static tscfg_rc extract_json_number(tscfg_lex_state *lex, char c,
                                    tscfg_tok *tok) {
  tscfg_rc rc;
  size_t str_size = 32;
  char *str = malloc(str_size);
  TSCFG_CHECK_MALLOC(str);

  size_t len = 1;
  str[0] = c;

  bool saw_dec_point = false;

  while (true) {
    size_t min_size = len + LEX_PEEK_BATCH_SIZE + 1;
    if (str_size < min_size) {
      str = realloc(str, min_size);
      TSCFG_CHECK_MALLOC_GOTO(str, cleanup, rc);
      str_size = min_size;
    }

    char *pos = &str[len];
    size_t got;
    rc = lex_peek(lex, pos, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    assert(got <= LEX_PEEK_BATCH_SIZE);

    size_t num_chars = 0;
    while (num_chars < got) {
      c = pos[num_chars];
      if ((c >= '0' && c <= '9')) {
        num_chars++;
      } else if (!saw_dec_point && c == '.') {
        saw_dec_point = true;
        num_chars++;
      } else {
        break;
      }
    }

    if (num_chars > 0) {
      lex_eat(lex, num_chars);
      len += num_chars;
    }

    if (num_chars < got || got == 0) {
      // End of whitespace or file
      break;
    }

  }

  tok->tag = TSCFG_TOK_NUMBER;
  str[len] = '\0';
  tok->length = len;
  tok->str = str;
  return TSCFG_OK;

cleanup:
  free(str);
  return rc;
}

/*
 * Extract string according to HOCON rules.
 * Assumes that " is currently first character in lexer.
 */
static tscfg_rc extract_hocon_str(tscfg_lex_state *lex, tscfg_tok *tok) {
  tscfg_rc rc;
  // Remove initial "
  lex_eat(lex, 1);

  char buf[2];
  size_t got = 0;
  rc = lex_peek(lex, buf, 2, &got);
  TSCFG_CHECK(rc);

  if (got == 2 && memcmp(buf, "\"\"", 2) == 0) {
    lex_eat(lex, 2);
    // Multiline string
    return extract_hocon_multiline_str(lex, tok);
  } else {
    // Regular JSON string
    return extract_json_str(lex, tok);
  }
}

static tscfg_rc extract_json_str(tscfg_lex_state *lex, tscfg_tok *tok) {
  tscfg_rc rc;
  size_t len = 0;
  size_t str_size = 32;
  char *str = malloc(str_size);
  TSCFG_CHECK_MALLOC(str);

  bool end_of_string = false;

  do {
    size_t min_size = len + 2;
    if (str_size < min_size) {
      size_t new_size = str_size * 2;
      new_size = (new_size > min_size) ? new_size : min_size;
      void *tmp = realloc(str, new_size);
      TSCFG_CHECK_MALLOC_GOTO(str, cleanup, rc);
      str = tmp;
      str_size = new_size;
    }

    const size_t lookahead = 5; // Enough to handle all escape codes
    char buf[lookahead];

    size_t got;
    rc = lex_peek(lex, buf, lookahead, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got == 0) {
      lex_report_err(lex, "String missing closing \"");
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }

    char c = buf[0];
    if (c == '"') {
      lex_eat(lex, 1);
      end_of_string = true;
    } else if (c == '\\') {
      char escaped;
      size_t escape_len;
      rc = extract_json_str_escape(lex, buf, got, &escaped, &escape_len);
      TSCFG_CHECK_GOTO(rc, cleanup);

      len++;
      lex_eat(lex, escape_len);
    } else {
      lex_eat(lex, 1);
      str[len++] = buf[0];
    }
  } while (!end_of_string);

  tok->tag = TSCFG_TOK_UNQUOTED;
  str[len] = '\0';
  tok->length = len;
  tok->str = str;
  return TSCFG_OK;

cleanup:
  free(str);
  return rc;
}

/*
 * Handle a JSON string escape code.
 *
 * escape: escape sequence, including initial \, plus maybe trailing chars
 * len: length of sequence, including any trailing chars
 * result: escaped character
 * consumed: number of characters consumed
 */
static tscfg_rc extract_json_str_escape(tscfg_lex_state *lex, char *escape,
                    size_t len, char *result, size_t *consumed) {
  assert(len >= 1);
  assert(escape[0] == '\\');

  if (len == 1) {
    lex_report_err(lex, "\\ without escape code in string");
    return TSCFG_ERR_SYNTAX;
  }

  char c = escape[1];
  switch (c) {
    case '\\':
    case '"':
    case '/':
      *result = c;
      *consumed = 2;
      break;
    case 'b':
      *result = '\b';
      *consumed = 2;
      break;
    case 'f':
      *result = '\f';
      *consumed = 2;
      break;
    case 'n':
      *result = '\n';
      *consumed = 2;
      break;
    case 'r':
      *result = '\r';
      *consumed = 2;
      break;
    case 't':
      *result = '\t';
      *consumed = 2;
      break;
    case 'u':
      // TODO: implement unicode escape codes
      return TSCFG_ERR_UNIMPL;
  }

  return TSCFG_OK;
}

/*
 * Extract multiline string.  Assume lexer has been advanced past open
 * quotes.
 */
static tscfg_rc extract_hocon_multiline_str(tscfg_lex_state *lex,
                                            tscfg_tok *tok) {
  tscfg_rc rc;
  size_t len = 0;
  size_t str_size = 128;
  char *str = malloc(str_size);
  TSCFG_CHECK_MALLOC(str);

  while (true) {
    assert(LEX_PEEK_BATCH_SIZE >= 4); // Look for triple quote plus one

    // Resize more aggressively since strings are often long
    size_t min_size = len + LEX_PEEK_BATCH_SIZE + 1;
    if (str_size < min_size) {
      size_t new_size = str_size * 2;
      new_size = (new_size > min_size) ? new_size : min_size;
      void *tmp = realloc(str, new_size);
      TSCFG_CHECK_MALLOC_GOTO(str, cleanup, rc);
      str = tmp;
      str_size = new_size;
    }

    char *pos = &str[len];

    size_t got = 0;
    rc = lex_peek(lex, pos, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    size_t to_append = 0;
    if (got < 3) {
      lex_report_err(lex, "Unterminated \"\"\" string");
      return TSCFG_ERR_SYNTAX;
    } else if (memcmp(pos, "\"\"\"", 3) == 0) {
      // Need to match last """ according to HOCON
      if (got == 4 && pos[3] == '"') {
        to_append = 1; // First " is part of string
      } else {
        // Last """ in string
        lex_eat(lex, 3);
        break;
      }
    } else {
      // First character plus any non-quotes are definitely in string
      to_append = 1;
      while (to_append < LEX_PEEK_BATCH_SIZE && pos[to_append] != '"') {
        to_append++;
      }
    }

    assert(to_append != 0);
    lex_eat(lex, to_append);
    len += to_append;
  }

  tok->tag = TSCFG_TOK_UNQUOTED;
  str[len] = '\0';
  tok->length = len;
  tok->str = str;
  return TSCFG_OK;

cleanup:
  free(str);
  return rc;
}

/*
 * Extract unquoted text according to HOCON rules
 */
static tscfg_rc extract_hocon_unquoted(tscfg_lex_state *lex, tscfg_tok *tok) {
  tscfg_rc rc;

  size_t str_size = 32;
  char *str = malloc(str_size);
  TSCFG_CHECK_MALLOC(str);
  size_t len = 0;


  bool end_of_tok = false;
  do {
    size_t min_size = len + LEX_PEEK_BATCH_SIZE + 1;
    if (str_size < min_size) {
      str = realloc(str, min_size);
      TSCFG_CHECK_MALLOC_GOTO(str, cleanup, rc);
      str_size = min_size;
    }

    size_t got;
    char *pos = &str[len];

    assert(LEX_PEEK_BATCH_SIZE >= 2); // Need lookahead of at least two chars
    rc = lex_peek(lex, pos, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    size_t to_append = 0;

    // Cannot append last character b/c need to check not a comment
    while (to_append < got) {
      if (!is_hocon_unquoted_char(pos[to_append]) &&
          is_hocon_whitespace(pos[to_append])) {
        // Cases where unquoted text definitely terminates
        end_of_tok = true;
        break;
      }

      if (to_append < got - 1) {
        // Can check for comment with lookahead two
        if (is_comment_start(&pos[to_append], 2)) {
          end_of_tok = true;
          break;
        }
      } else {
        /* Last character, may not be able to decide whether to append yet */
        if (got < LEX_PEEK_BATCH_SIZE) {
          // End of file, only check one char comments
          if (is_comment_start(&pos[to_append], 1)) {
            end_of_tok = true;
            break;
          }
        } else {
          // Need to read more before deciding
          end_of_tok = false;
          break;
        }
      }

      // Next character is part of unquoted text
      to_append++;
    }

    if (to_append > 0) {
      lex_eat(lex, to_append);
      len += to_append;
    }

    if (got == 0) {
      // End of input
      end_of_tok = true;
    }
  } while (!end_of_tok);

  tok->tag = TSCFG_TOK_UNQUOTED;
  str[len] = '\0';
  tok->length = len;
  tok->str = str;

  return TSCFG_OK;

cleanup:
  free(str);
  return rc;
}

/*
 * Extract keyword or unquoted string.
 * c: first character of keyword
 */
static tscfg_rc extract_keyword_or_hocon_unquoted(tscfg_lex_state *lex, char c,
                                                  tscfg_tok *tok) {
  tscfg_rc rc;

  const char *kw;
  size_t kwlen;
  tscfg_tok_tag kwtag;

  switch (c) {
  case 't':
    kw = "true";
    kwlen = 4;
    kwtag = TSCFG_TOK_TRUE;
    break;
  case 'f':
    kw = "false";
    kwlen = 5;
    kwtag = TSCFG_TOK_FALSE;
  case 'n':
    kw = "null";
    kwlen = 4;
    kwtag = TSCFG_TOK_NULL;
  default:
    assert(false);
    return TSCFG_ERR_UNKNOWN;
  }

  char buf[kwlen];
  size_t got;
  rc = lex_peek(lex, buf, kwlen, &got);
  TSCFG_CHECK(rc);

  if (got == kwlen && memcmp(buf, kw, kwlen) == 0) {
    tok->tag = kwtag;
    tok->str = NULL;
    tok->length = 0;

    lex_eat(lex, kwlen);

    return TSCFG_OK;
  } else {
    return extract_hocon_unquoted(lex, tok);
  }
}

/*
 * Check if whitespace
 */
static bool is_hocon_whitespace(char c) {
  switch (c) {
    CASE_HOCON_WHITESPACE:
      return true;
    default:
      return false;
  }
}

/*
 * Return true if character is to be treated semantically as newline\
 * according to the HOCON spec
 */
static inline bool is_hocon_newline(char c) {
  return (c == '\n');
}

/*
 * Return true if this is a char that can appear in an unquoted string.
 * Note that because it's valid to appear in unquoted text doesn't mean
 * that it should be greedily appended - special cases are handled elsewhere.
 */
static bool is_hocon_unquoted_char(char c) {
  switch (c) {
    case '$':
    case '"':
    case '{':
    case '}':
    case '[':
    case ']':
    case ':':
    case '=':
    case ',':
    case '+':
    case '#':
    case '`':
    case '^':
    case '?':
    case '!':
    case '@':
    case '*':
    case '&':
    case '\\':
      // Forbidden characters according to HOCON spec
      return false;
    default:
      if (is_hocon_whitespace(c)) {
        // Whitespace is also forbidden
        return false;
      } else{
        // All others can appear
        return true;
      }
  }
}

/*
 * Return true if string is start of comment
 */
static bool is_comment_start(const char *buf, size_t len) {
  if (len >= 1 && buf[0] == '#') {
    return true;
  } else if (len >= 2 && buf[0] == '/' &&
             (buf[1] == '/' || buf[1] == '*')) {
    return true;
  } else {
    return false;
  }
}

static void lex_report_err(tscfg_lex_state *lex, const char *fmt, ...) {
  // TODO: include context, e.g. line number, in errors.
  va_list args;
  va_start(args, fmt);
  tscfg_report_err_v(fmt, args);
  va_end(args);
}
