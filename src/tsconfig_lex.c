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

static tscfg_rc eat_hocon_comm_ws(tscfg_lex_state *lex, size_t *read,
                                  bool *newline);
static tscfg_rc eat_hocon_comment(tscfg_lex_state *lex, size_t *read);
static tscfg_rc eat_rest_of_line(tscfg_lex_state *lex, size_t *read);
static tscfg_rc eat_until_comm_end(tscfg_lex_state *lex, size_t *read);
static tscfg_rc eat_hocon_whitespace(tscfg_lex_state *lex, size_t *read,
                                  bool *newline);

static tscfg_rc extract_json_number(tscfg_lex_state *lex, char c, tscfg_tok *tok);
static tscfg_rc extract_hocon_str(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_json_str(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_hocon_multiline_str(tscfg_lex_state *lex,
                                            tscfg_tok *tok);
static tscfg_rc extract_hocon_unquoted(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_keyword_or_hocon_unquoted(tscfg_lex_state *lex, char c,
                                                  tscfg_tok *tok);

static bool is_hocon_whitespace(char c);
static bool is_hocon_newline(char c);
static bool is_hocon_unquoted_char(char c);
static bool is_comment_start(const char *buf, size_t len);

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

tscfg_rc tscfg_read_tok(tscfg_lex_state *lex, tscfg_tok *tok) {
  assert(lex != NULL);
  assert(tok != NULL);

  tscfg_rc rc;

  // First consume any comments or whitespace
  size_t read = 0;
  bool saw_newline = false;
  rc = eat_hocon_comm_ws(lex, &read, &saw_newline);
  TSCFG_CHECK(rc);

  // TODO: need to accumulate whitespace into token
  // TODO: do we need to have comment tokens??
  if (saw_newline) {
    set_nostr_tok(TSCFG_TOK_WS_NEWLINE, tok);

    return TSCFG_OK;
  }

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
    case '+':
      // TODO: must be += operator
      return TSCFG_ERR_UNIMPL;

    case '-': case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
      // TODO: number token
      return extract_json_number(lex, c, tok);

    case 't':
    case 'f':
    case 'n':
      // try to parse as keyword, otherwise unquoted string
      return extract_keyword_or_hocon_unquoted(lex, c, tok);

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

/*
 * Consume comments and whitespace.
 * newline: set to true if we saw at least one newline
 */
static tscfg_rc eat_hocon_comm_ws(tscfg_lex_state *lex, size_t *read,
                                  bool *newline) {
  tscfg_rc rc;
  *newline = false;
  size_t total_read = 0, iter_read;
  do {
    size_t ws_read = 0, comm_read = 0;
    bool newline2;
    // Follows JSON whitespace rules
    rc = eat_hocon_whitespace(lex, &ws_read, &newline2);
    TSCFG_CHECK(rc);

    if (newline2) {
      *newline = true;
    }

    rc = eat_hocon_comment(lex, &comm_read);
    TSCFG_CHECK(rc);

    iter_read = ws_read + comm_read;
    total_read += iter_read;
  } while (iter_read > 0);

  *read = total_read;

  return TSCFG_OK;
}

static tscfg_rc eat_hocon_comment(tscfg_lex_state *lex, size_t *read) {
  tscfg_rc rc;

  *read = 0;

  /*
   * Look ahead two characters for / followed by / or * or one for #
   */
  char c[2];
  size_t got;
  rc = lex_peek(lex, c, 2, &got);
  TSCFG_CHECK(rc);

  if (got >= 1 && c[0] == '#') {
    lex_eat(lex, 1);
    (*read)++;

    size_t line_read = 0;
    rc = eat_rest_of_line(lex, &line_read);
    TSCFG_CHECK(rc);
    (*read) += line_read;
  } else if (got == 2 &&
      c[0] == '/' &&
      (c[1] == '/' || c[1] == '*')) {

    lex_eat(lex, 2);
    (*read) += 2;

    if (c[1] == '/') {
      size_t line_read;
      rc = eat_rest_of_line(lex, &line_read);
      TSCFG_CHECK(rc);
      (*read) += line_read;
    } else {
      assert(c[1] == '*');
      size_t comm_read = 0;
      rc = eat_until_comm_end(lex, &comm_read);
      TSCFG_CHECK(rc);
      (*read) += comm_read;
    }
  }

  return TSCFG_OK;
}

static tscfg_rc eat_rest_of_line(tscfg_lex_state *lex, size_t *read) {
  tscfg_rc rc;

  *read = 0;

  bool end_of_line = false;

  while (!end_of_line) {
    char buf[LEX_PEEK_BATCH_SIZE];
    size_t got;
    rc = lex_peek(lex, buf, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK(rc);

    if (got == 0) {
      break;
    }

    size_t pos = 0;
    while (pos < got) {
      // Make sure to consume newline
      if (buf[pos++] == '\n') {
        end_of_line = true;
        break;
      }
    }

    lex_eat(lex, pos);
    (*read) += pos;
  }

  return TSCFG_OK;
}

/*
 * Search for end of multiline comment
 */
static tscfg_rc eat_until_comm_end(tscfg_lex_state *lex, size_t *read) {
  tscfg_rc rc;

  bool in_comment = true;

  *read = 0;

  while (in_comment) {
    char buf[LEX_PEEK_BATCH_SIZE];
    size_t got;
    rc = lex_peek(lex, buf, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK(rc);

    if (got < 2) {
      // Cannot be comment close, therefore unclosed comment

      lex_eat(lex, got);
      (*read) += got;

      lex_report_err(lex, "/* comment without matching */");
      return TSCFG_ERR_SYNTAX;
    }

    size_t pos = 0;
    while (pos < got - 1) {
      if (buf[pos] == '*' && buf[pos + 1] == '/') {
        pos += 2;
        in_comment = false;
        break;
      } else {
        // May not need to check next position
        int advance = (buf[pos + 1] == '*') ? 1 : 2;
        pos += advance;
      }
    }

    lex_eat(lex, pos);
    (*read) += pos;
  }

  return TSCFG_OK;
}


/*
 * Remove any leading whitespace characters from input.
 * newline: set to true if saw at least one newline
 */
static tscfg_rc eat_hocon_whitespace(tscfg_lex_state *lex, size_t *read,
                                  bool *newline) {
  tscfg_rc rc;

  *read = 0;
  *newline = false;

  while (true) {
    char buf[LEX_PEEK_BATCH_SIZE];
    size_t got;
    rc = lex_peek(lex, buf, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK(rc);

    assert(got <= LEX_PEEK_BATCH_SIZE);

    size_t ws_chars = 0;
    while (ws_chars < got &&
           is_hocon_whitespace(buf[ws_chars])) {
      if (is_hocon_newline(buf[ws_chars])) {
        *newline = true;
      }
      ws_chars++;
    }

    if (ws_chars > 0) {
      lex_eat(lex, ws_chars);
      (*read) += ws_chars;
    }

    if (ws_chars < got || got == 0) {
      // End of whitespace or file
      break;
    }
  }

  return TSCFG_OK;
}

/*
 * Extract JSON/HOCON number token
 * c: first character of string
 * TODO: doesn't handle exponentials
 */
static tscfg_rc extract_json_number(tscfg_lex_state *lex, char c,
                                    tscfg_tok *tok) {
  tscfg_rc rc;
  // TODO: cleanup on error
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
      TSCFG_CHECK_MALLOC(str);
      str_size = min_size;
    }

    char *pos = &str[len];
    size_t got;
    rc = lex_peek(lex, pos, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK(rc);

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
  tok->length = len;
  tok->str = str;
  tok->str[tok->length] = '\0';
  return TSCFG_OK;
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
    size_t min_size = len + LEX_PEEK_BATCH_SIZE + 1;
    if (str_size < min_size) {
      size_t new_size = str_size * 2;
      new_size = (new_size > min_size) ? new_size : min_size;
      void *tmp = realloc(str, new_size);
      TSCFG_CHECK_MALLOC_GOTO(str, cleanup, rc);
      str = tmp;
      str_size = new_size;
    }

    size_t got;
    char *pos = &str[len];

    rc = lex_peek(lex, pos, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    size_t to_append = 0;

    while (to_append < got) {

      // Next character is part of string
      to_append++;
      // TODO: implement, factoring in escape codes, etc
      rc = TSCFG_ERR_UNIMPL;
      goto cleanup;
    }

    if (to_append > 0) {
      lex_eat(lex, to_append);
      len += to_append;
    }

    if (got == 0) {
      // End of input
      end_of_string = true;
    }
  } while (!end_of_string);

  tok->tag = TSCFG_TOK_UNQUOTED;
  tok->length = len;
  tok->str = str;
  tok->str[tok->length] = '\0';
  return TSCFG_OK;

cleanup:
  free(str);
  return rc;
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
  tok->length = len;
  tok->str = str;
  tok->str[tok->length] = '\0';
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

  // TODO: cleanup on error
  size_t str_size = 32;
  char *str = malloc(str_size);
  TSCFG_CHECK_MALLOC(str);


  bool end_of_tok = false;
  size_t len = 0;
  do {
    size_t min_size = len + LEX_PEEK_BATCH_SIZE + 1;
    if (str_size < min_size) {
      str = realloc(str, min_size);
      TSCFG_CHECK_MALLOC(str);
      str_size = min_size;
    }

    size_t got;
    char *pos = &str[len];

    assert(LEX_PEEK_BATCH_SIZE >= 2); // Need lookahead of at least two chars
    rc = lex_peek(lex, pos, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK(rc);

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
  tok->length = len;
  tok->str = str;
  tok->str[tok->length] = '\0';
  return TSCFG_OK;
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
 * TODO: doesn't match HOCON spec
 */
static bool is_hocon_whitespace(char c) {
  switch (c) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
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
