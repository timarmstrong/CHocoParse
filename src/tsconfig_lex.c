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
#include "tsconfig_utf8.h"

// Default amount to buffer when searching ahead
#define LEX_PEEK_BATCH_SIZE 32

// Max bytes per encoded UTF-8 character
#define UTF8_MAX_BYTES 6

typedef struct {
  char *str;
  size_t size; // Allocated size in bytes
  size_t len; // Data length in bytes
} tscfg_strbuf;

static inline void set_nostr_tok(tscfg_tok_tag tag, tscfg_tok *tok);
static inline tscfg_tok_tag tag_from_char(char c);

static tscfg_rc lex_peek(tscfg_lex_state *lex, tscfg_char_t *chars,
                         int nchars, int *got);
static tscfg_rc lex_peek_bytes(tscfg_lex_state *lex, char *buf,
                               size_t len, size_t *got);
static tscfg_rc lex_read_more(tscfg_lex_state *lex, size_t bytes);
static tscfg_rc lex_read(tscfg_lex_state *lex, unsigned char *buf, size_t bytes,
                         size_t *read_bytes);
static void lex_eat(tscfg_lex_state *lex, int chars);
static void lex_eat_bytes(tscfg_lex_state *lex, size_t bytes);

static tscfg_rc extract_hocon_ws(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str);
static tscfg_rc extract_comment_or_hocon_unquoted(tscfg_lex_state *lex,
                                tscfg_tok *tok, bool include_comm_str);
static tscfg_rc extract_line_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str);
static tscfg_rc extract_multiline_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str);

static tscfg_rc extract_json_number(tscfg_lex_state *lex, tscfg_char_t c,
                                    tscfg_tok *tok);
static tscfg_rc extract_hocon_str(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_json_str(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_json_str_escape(tscfg_lex_state *lex, char *escape,
                    size_t len, char *result, size_t *consumed);
static tscfg_rc extract_hocon_multiline_str(tscfg_lex_state *lex,
                                            tscfg_tok *tok);
static tscfg_rc extract_hocon_unquoted(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_keyword_or_hocon_unquoted(tscfg_lex_state *lex,
                                        tscfg_char_t c, tscfg_tok *tok);

static bool is_hocon_whitespace(tscfg_char_t c);
static bool is_hocon_newline(tscfg_char_t c);
static bool is_hocon_unquoted_char(tscfg_char_t c);
static bool is_comment_start(const char *buf, size_t len);

static tscfg_rc strbuf_init(tscfg_strbuf *sb, size_t init_size);
static tscfg_rc strbuf_expand(tscfg_strbuf *sb, size_t min_size,
                              bool aggressive);
static void strbuf_free(tscfg_strbuf *sb);

/* Unicode characters according to database:
  http://www.unicode.org/Public/7.0.0/ucd/PropList.txt */
#define CASE_UNICODE_ZS \
  case 0x0020: case 0x00A0: case 0x1680: case 0x2000: case 0x2001: \
  case 0x2002: case 0x2003: case 0x2004: case 0x2005: case 0x2006: \
  case 0x2007: case 0x2008: case 0x2009: case 0x200A: case 0x202F: \
  case 0x205F: case 0x3000

#define CASE_UNICODE_ZL case 0x2028

#define CASE_UNICODE_ZP case 0x2029

#define CASE_OTHER_ASCII_WHITESPACE \
  case '\t': case '\n': case '\r': case '\v': case '\f': \
  case 0x01C /* file sep */: case 0x01D /* group sep */: \
  case 0x01E /* record sep */: case 0x01F /* unit sep */


#define CASE_HOCON_WHITESPACE \
  CASE_UNICODE_ZS: CASE_UNICODE_ZL: CASE_UNICODE_ZP: case 0xFEFF /* BOM */: \
  CASE_OTHER_ASCII_WHITESPACE

static void lex_report_err(tscfg_lex_state *lex, const char *fmt, ...);

tscfg_rc tscfg_lex_init(tscfg_lex_state *lex, tsconfig_input in) {
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
  tscfg_char_t c;
  int got;
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

/*
 * Read ahead nchars characters (or less if end of input)
 */
static tscfg_rc lex_peek(tscfg_lex_state *lex, tscfg_char_t *chars,
                         int nchars, int *got) {
  tscfg_rc rc;
  assert(nchars >= 0);
  size_t max_bytes = (size_t)nchars * UTF8_MAX_BYTES;

  if (lex->buf_len < max_bytes) {
    // Attempt to read more
    size_t toread = max_bytes - lex->buf_len;
    rc = lex_read_more(lex, toread);
    TSCFG_CHECK(rc);
  }

  // Decode utf-8
  int read_chars = 0;
  size_t buf_pos = 0, buf_len = lex->buf_len;
  while (read_chars < nchars && buf_pos < buf_len) {
    unsigned char b = lex->buf[buf_pos++];
    int enc_len;
    tscfg_char_t c;

    rc = tscfg_decode_byte1(b, &enc_len, &c);
    TSCFG_CHECK(rc);

    // TODO: check enough bytes in buffer
    rc = tscfg_decode_rest(&lex->buf[buf_pos], enc_len - 1, &c);
    TSCFG_CHECK(rc);
    
    buf_pos += (enc_len - 1);
    chars[read_chars++] = c;
  }

  *got = read_chars;
  return TSCFG_OK;
}

/*
 * Peek, interpreting as ASCII, i.e. 1 byte per char.
 *
 */
static tscfg_rc lex_peek_bytes(tscfg_lex_state *lex, char *buf,
                               size_t len, size_t *got) {
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
  tsconfig_input *in = &lex->in;
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

/*
  Consume utf-8 chars from buffer.
  We do this by redecoding the characters.
  Rely on caller having peeked at least this many characters ahead
 */
static void lex_eat(tscfg_lex_state *lex, int chars) {
  tscfg_rc rc;
  size_t byte_pos = 0;
  for (int char_pos = 0; char_pos < chars; char_pos++) {
    tscfg_char_t c;
    int enc_len;
    unsigned char b = lex->buf[byte_pos];
    
    rc = tscfg_decode_byte1(b, &enc_len, &c);
    assert(rc == TSCFG_OK); // Check caller was correct 

    byte_pos += enc_len;
    assert(byte_pos < lex->buf_len);
  }

  lex_eat_bytes(lex, byte_pos);
}

static void lex_eat_bytes(tscfg_lex_state *lex, size_t bytes) {
  assert(bytes <= lex->buf_len);

  /* Bump data forward
   * Note that this is inefficient if we are frequently bumping small portions
   * of a large buffer, since this requires moving the remainder of the buffer.
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

  tscfg_char_t buf[2];
  int got;
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

  tscfg_strbuf sb;
  rc = strbuf_init(&sb, include_str ? 32 : 0);
  TSCFG_CHECK(rc);

  char buf_storage[LEX_PEEK_BATCH_SIZE];

  bool end_of_line = false;

  while (!end_of_line) {

    if (include_str) {
      // Resize more aggressively since strings are often long
      size_t min_size = sb.len + LEX_PEEK_BATCH_SIZE;
      rc = strbuf_expand(&sb, min_size, true);
      TSCFG_CHECK(rc);
    }

    char *buf = include_str ? &sb.str[sb.len] : buf_storage;

    size_t got;
    rc = lex_peek_bytes(lex, buf, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got == 0) {
      break;
    }

    // TODO: do we need to interpret as UTF-8?
    const char *nl = memchr(buf, '\n', got);
    size_t num_chars;
    if (nl == NULL) {
      num_chars = got;
    } else {
      num_chars = (size_t)(nl - buf);
      end_of_line = true;
    }

    lex_eat_bytes(lex, num_chars);
    sb.len += num_chars;
  }

  tok->tag = TSCFG_TOK_COMMENT;
  if (include_str) {
    tok->length = sb.len;
    tok->str = sb.str;
  } else {
    tok->length = 0;
    tok->str = 0;
  }

  return TSCFG_OK;

cleanup:
  strbuf_free(&sb);
  return rc;
}

/*
 * Search for end of multiline comment, i.e * then /
 */
static tscfg_rc extract_multiline_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str) {
  tscfg_rc rc;

  tscfg_strbuf sb;
  rc = strbuf_init(&sb, include_str ? 64 : 0);
  TSCFG_CHECK(rc);

  char buf_storage[LEX_PEEK_BATCH_SIZE];

  bool in_comment = true;

  while (in_comment) {

    if (include_str) {
      // Resize more aggressively since comments are often long
      size_t min_size = sb.len + LEX_PEEK_BATCH_SIZE;
      rc = strbuf_expand(&sb, min_size, true);
      TSCFG_CHECK(rc);
    }

    char *buf = include_str ? &sb.str[sb.len] : buf_storage;

    size_t got;
    rc = lex_peek_bytes(lex, buf, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got < 2) {
      // Cannot be comment close, therefore unclosed comment

      lex_eat_bytes(lex, got);
      sb.len += got;

      lex_report_err(lex, "/* comment without matching */");
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }

    // TODO: do we need to interpret as utf-8?
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

    lex_eat_bytes(lex, num_chars);
    sb.len += num_chars;
  }

  tok->tag = TSCFG_TOK_COMMENT;
  if (include_str) {
    tok->length = sb.len;
    tok->str = sb.str;
  } else {
    tok->length = 0;
    tok->str = NULL;
  }

  return TSCFG_OK;

cleanup:
  strbuf_free(&sb);
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

  tscfg_strbuf sb;
  rc = strbuf_init(&sb, include_str ? 128 : 0);
  TSCFG_CHECK(rc);

  char buf_storage[LEX_PEEK_BATCH_SIZE];
  bool saw_newline = false;

  while (true) {
    bool read_more = false;

    if (include_str) {
      // Resize more aggressively since strings are often long
      size_t min_size = sb.len + LEX_PEEK_BATCH_SIZE;
      rc = strbuf_expand(&sb, min_size, true);
      TSCFG_CHECK(rc);
    }
    char *buf = include_str ? &sb.str[sb.len] : buf_storage;

    size_t got;
    rc = lex_peek_bytes(lex, buf, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    assert(got <= LEX_PEEK_BATCH_SIZE);

    // TODO: need to decode utf-8
    // TODO: set read_more if in middle of char
    size_t ws_chars = 0;
    while (ws_chars < got &&
           is_hocon_whitespace(buf[ws_chars])) {
      if (is_hocon_newline(buf[ws_chars])) {
        saw_newline = true;
      }
      ws_chars++;
    }

    if (ws_chars > 0) {
      lex_eat_bytes(lex, ws_chars);
      sb.len += ws_chars;
    }

    if ((ws_chars < got || got == 0) &&
        !read_more) {
      // End of whitespace or file
      break;
    }
  }

  tok->tag = saw_newline ? TSCFG_TOK_WS_NEWLINE : TSCFG_TOK_WS;
  if (include_str) {
    tok->length = sb.len;
    tok->str = sb.str;
  } else {
    tok->length = 0;
    tok->str = NULL;
  }

  return TSCFG_OK;

cleanup:
  strbuf_free(&sb);
  return rc;
}

/*
 * Extract JSON/HOCON number token
 * c: first character of string
 * TODO: doesn't handle exponentials
 */
static tscfg_rc extract_json_number(tscfg_lex_state *lex, tscfg_char_t c,
                                    tscfg_tok *tok) {
  tscfg_rc rc;
  tscfg_strbuf sb;
  rc = strbuf_init(&sb, 32);
  TSCFG_CHECK(rc);

  sb.len = 1;
  sb.str[0] = c;

  bool saw_dec_point = false;

  while (true) {
    size_t min_size = sb.len + LEX_PEEK_BATCH_SIZE;
    rc = strbuf_expand(&sb, min_size, false);
    TSCFG_CHECK(rc);

    char *pos = &sb.str[sb.len];
    size_t got;
    rc = lex_peek_bytes(lex, pos, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    assert(got <= LEX_PEEK_BATCH_SIZE);

    size_t nchars = 0;
    while (nchars < got) {
      c = pos[nchars];
      if ((c >= '0' && c <= '9')) {
        nchars++;
      } else if (!saw_dec_point && c == '.') {
        saw_dec_point = true;
        nchars++;
      } else {
        break;
      }
    }

    if (nchars > 0) {
      lex_eat(lex, nchars);
      sb.len += nchars;
    }

    if (nchars < got || got == 0) {
      // End of whitespace or file
      break;
    }

  }

  tok->tag = TSCFG_TOK_NUMBER;
  tok->length = sb.len;
  tok->str = sb.str;
  return TSCFG_OK;

cleanup:
  strbuf_free(&sb);
  return rc;
}

/*
 * Extract string according to HOCON rules.
 * Assumes that " is currently first character in lexer.
 */
static tscfg_rc extract_hocon_str(tscfg_lex_state *lex, tscfg_tok *tok) {
  tscfg_rc rc;
  // Remove initial "
  lex_eat_bytes(lex, 1);

  char buf[2];
  size_t got = 0;
  rc = lex_peek_bytes(lex, buf, 2, &got);
  TSCFG_CHECK(rc);

  if (got == 2 && memcmp(buf, "\"\"", 2) == 0) {
    lex_eat_bytes(lex, 2);
    // Multiline string
    return extract_hocon_multiline_str(lex, tok);
  } else {
    // Regular JSON string
    return extract_json_str(lex, tok);
  }
}

static tscfg_rc extract_json_str(tscfg_lex_state *lex, tscfg_tok *tok) {
  tscfg_rc rc;
  tscfg_strbuf sb;
  rc = strbuf_init(&sb, 32);
  TSCFG_CHECK(rc);

  bool end_of_string = false;

  do {
    size_t min_size = sb.len + 2;
    rc = strbuf_expand(&sb, min_size, false);
    TSCFG_CHECK(rc);

    const size_t lookahead = 5; // Enough to handle all escape codes
    char buf[lookahead];

    size_t got;
    rc = lex_peek_bytes(lex, buf, lookahead, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got == 0) {
      lex_report_err(lex, "String missing closing \"");
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }

    char c = buf[0];
    if (c == '"') {
      lex_eat_bytes(lex, 1);
      end_of_string = true;
    } else if (c == '\\') {
      char escaped;
      size_t escape_len;
      rc = extract_json_str_escape(lex, buf, got, &escaped, &escape_len);
      TSCFG_CHECK_GOTO(rc, cleanup);

      sb.len++;
      lex_eat_bytes(lex, escape_len);
    } else {
      lex_eat_bytes(lex, 1);
      sb.str[sb.len++] = buf[0];
    }
  } while (!end_of_string);

  tok->tag = TSCFG_TOK_UNQUOTED;
  tok->length = sb.len;
  tok->str = sb.str;
  return TSCFG_OK;

cleanup:
  strbuf_free(&sb);
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
    default:
      // TODO
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

  tscfg_strbuf sb;
  rc = strbuf_init(&sb, 128);
  TSCFG_CHECK(rc);

  bool end_of_string = false;
  do {
    assert(LEX_PEEK_BATCH_SIZE >= 4); // Look for triple quote plus one

    // Resize more aggressively since strings are often long
    size_t min_size = sb.len + LEX_PEEK_BATCH_SIZE;
    rc = strbuf_expand(&sb, min_size, true);
    TSCFG_CHECK(rc);

    char *buf = &sb.str[sb.len];

    size_t got = 0;
    rc = lex_peek_bytes(lex, buf, LEX_PEEK_BATCH_SIZE, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    size_t to_append = 0;
    if (got < 3) {
      lex_report_err(lex, "Unterminated \"\"\" string");
      return TSCFG_ERR_SYNTAX;
    }

    const char *quote = memchr(buf, '\"', got);
    if (quote == NULL) {
      to_append = got;
    } else {
      size_t before_quote = quote - buf;
      to_append = before_quote;
      if (got - before_quote < 4) {
        // Need to advance further to check quotes
      } else if (memcmp(quote, "\"\"\"", 3) == 0) {
        // Need to match last """ according to HOCON
        if (quote[3] == '"') {
          to_append++; // First " is part of string
        } else {
          // Closing """
          end_of_string = true;
        }
      } else {
        // Last quote could be part of end quote
        to_append += 2;
      }
    }

    assert(to_append != 0);
    lex_eat_bytes(lex, to_append);
    sb.len += to_append;
  } while (!end_of_string);

  tok->tag = TSCFG_TOK_UNQUOTED;
  tok->length = sb.len;
  tok->str = sb.str;
  return TSCFG_OK;

cleanup:
  strbuf_free(&sb);
  return rc;
}

/*
 * Extract unquoted text according to HOCON rules
 */
static tscfg_rc extract_hocon_unquoted(tscfg_lex_state *lex, tscfg_tok *tok) {
  tscfg_rc rc;

  tscfg_strbuf sb;
  rc = strbuf_init(&sb, 32);
  TSCFG_CHECK(rc);


  bool end_of_tok = false;
  do {
    size_t min_size = sb.len + LEX_PEEK_BATCH_SIZE;
    rc = strbuf_expand(&sb, min_size, false);
    TSCFG_CHECK(rc);

    size_t got;
    char *pos = &sb.str[sb.len];

    // TODO: modify to unicode decoding
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
      sb.len += to_append;
    }

    if (got == 0) {
      // End of input
      end_of_tok = true;
    }
  } while (!end_of_tok);

  tok->tag = TSCFG_TOK_UNQUOTED;
  tok->length = sb.len;
  tok->str = sb.str;

  return TSCFG_OK;

cleanup:
  strbuf_free(&sb);
  return rc;
}

/*
 * Extract keyword or unquoted string.
 * c: first character of keyword
 */
static tscfg_rc extract_keyword_or_hocon_unquoted(tscfg_lex_state *lex,
                                        tscfg_char_t c, tscfg_tok *tok) {
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
  // All keywords are 1 byte per character in utf-8
  rc = lex_peek_bytes(lex, buf, kwlen, &got);
  TSCFG_CHECK(rc);

  if (got == kwlen && memcmp(buf, kw, kwlen) == 0) {
    tok->tag = kwtag;
    tok->str = NULL;
    tok->length = 0;

    lex_eat_bytes(lex, kwlen);

    return TSCFG_OK;
  } else {
    return extract_hocon_unquoted(lex, tok);
  }
}

/*
 * Check if whitespace
 */
static bool is_hocon_whitespace(tscfg_char_t c) {
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
static inline bool is_hocon_newline(tscfg_char_t c) {
  return (c == '\n');
}

/*
 * Return true if this is a char that can appear in an unquoted string.
 * Note that because it's valid to appear in unquoted text doesn't mean
 * that it should be greedily appended - special cases are handled elsewhere.
 */
static bool is_hocon_unquoted_char(tscfg_char_t c) {
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

static tscfg_rc strbuf_init(tscfg_strbuf *sb, size_t init_size) {
  if (init_size == 0) {
    sb->str = NULL;
  } else {
    sb->str = malloc(init_size);
    TSCFG_CHECK_MALLOC(sb->str);
  }

  sb->size = init_size;
  sb->len = 0;
  return TSCFG_OK;
}

/*
 *
 * aggressive: if true, aggressively allocate memory
 */
static tscfg_rc strbuf_expand(tscfg_strbuf *sb, size_t min_size,
                              bool aggressive) {
  if (sb->size < min_size) {
    size_t new_size;
    if (aggressive) {
      new_size = sb->size * 2;
    } else {
      new_size = min_size;
    }
    new_size = (new_size > min_size) ? new_size : min_size;
    void *tmp = realloc(sb->str, new_size);
    TSCFG_CHECK_MALLOC(sb->str);
    sb->str = tmp;
    sb->size = new_size;
  }

  return TSCFG_OK;
}

static void strbuf_free(tscfg_strbuf *sb) {
  free(sb->str);
}
