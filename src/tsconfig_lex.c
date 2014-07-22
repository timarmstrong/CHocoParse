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

// Unicode escape code length (hex digits)
#define UNICODE_ESCAPE_LEN 4

typedef struct {
  char *str;
  size_t size; // Allocated size in bytes
  size_t len; // Data length in bytes
} tscfg_strbuf;

static inline void tok_file_line(const tscfg_lex_state *lex, tscfg_tok *tok);
static inline void set_str_tok(tscfg_tok_tag tag, tscfg_strbuf *sb,
                               tscfg_tok *tok);
static inline void set_nostr_tok(tscfg_tok_tag tag, tscfg_tok *tok);
static inline tscfg_tok_tag tag_from_char(tscfg_char_t c);

static tscfg_rc lex_peek(tscfg_lex_state *lex, tscfg_char_t *chars,
                         int nchars, int *got);
static tscfg_rc lex_peek_bytes(tscfg_lex_state *lex, char *buf,
                               size_t len, size_t *got);
static tscfg_rc lex_read_more(tscfg_lex_state *lex, size_t bytes);
static tscfg_rc lex_read(tscfg_lex_state *lex, unsigned char *buf, size_t bytes,
                         size_t *read_bytes);
static void lex_eat(tscfg_lex_state *lex, int chars);
static void lex_eat_ascii(tscfg_lex_state *lex, size_t bytes);
static void lex_update_line(tscfg_lex_state *lex, unsigned char b);

static tscfg_rc lex_copy_char(tscfg_lex_state *lex, tscfg_strbuf *sb,
                            bool aggressive_resize);

static tscfg_rc extract_hocon_ws(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str);
static tscfg_rc extract_comment_or_hocon_unquoted(tscfg_lex_state *lex,
                                tscfg_tok *tok, bool include_comm_str);
static tscfg_rc extract_line_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str);
static tscfg_rc extract_multiline_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str);

static tscfg_rc extract_var(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_json_number(tscfg_lex_state *lex, tscfg_char_t c,
                                    tscfg_tok *tok);
static tscfg_rc extract_hocon_str(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_json_str(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_json_str_escape(tscfg_lex_state *lex,
                                        tscfg_char_t *result);
static tscfg_rc extract_unicode_escape(tscfg_lex_state *lex, tscfg_char_t *c);

static tscfg_rc extract_hocon_multiline_str(tscfg_lex_state *lex,
                                            tscfg_tok *tok);
static tscfg_rc extract_hocon_unquoted(tscfg_lex_state *lex, tscfg_tok *tok);
static tscfg_rc extract_keyword_or_hocon_unquoted(tscfg_lex_state *lex,
                                        tscfg_char_t c, tscfg_tok *tok);

static tscfg_rc extract_until(tscfg_lex_state *lex, tscfg_strbuf *sb,
                              tscfg_char_t match, bool *found);

static tscfg_rc eat_until(tscfg_lex_state *lex, tscfg_char_t match,
                          bool *found);

static bool is_hocon_whitespace(tscfg_char_t c);
static bool is_hocon_newline(tscfg_char_t c);
static bool is_hocon_unquoted_char(tscfg_char_t c);
static bool is_comment_start(const tscfg_char_t *buf, int len);

static void strbuf_init_empty(tscfg_strbuf *sb);
static tscfg_rc strbuf_init(tscfg_strbuf *sb, size_t init_size);
static tscfg_rc strbuf_expand(tscfg_strbuf *sb, size_t min_size,
                              bool aggressive);
static void strbuf_finalize(tscfg_strbuf *sb);
static void strbuf_free(tscfg_strbuf *sb);
static tscfg_rc strbuf_append_utf8(tscfg_strbuf *sb, tscfg_char_t c,
                              bool aggressive);

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

  lex->line = 1;
  lex->line_char = 1;
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

  // Token starts at current position
  tok_file_line(lex, tok);

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

    case '$':
      lex_eat(lex, 1);
      return extract_var(lex, tok);

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
 * Token starts at current file/line
 */
static inline void tok_file_line(const tscfg_lex_state *lex, tscfg_tok *tok) {
  tok->line = lex->line;
  tok->line_char = lex->line_char;
}

/*
 * Set token without string
 */
static inline void set_nostr_tok(tscfg_tok_tag tag, tscfg_tok *tok) {
  tok->tag = tag;
  tok->str = NULL;
  tok->len = 0;
}

/*
 * Set token with string from string buffer.
 * sb: assume enough room allocated for null terminator.
 *     sb is invalidated by call.
 */
static inline void set_str_tok(tscfg_tok_tag tag, tscfg_strbuf *sb, tscfg_tok *tok) {
  tok->tag = tag;

  strbuf_finalize(sb);
  tok->str = sb->str;
  tok->len = sb->len;

  // Invalidate string buffer
  strbuf_init_empty(sb);
}

/*
 * Translate single character token into tag.
 */
static inline tscfg_tok_tag tag_from_char(tscfg_char_t c) {
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
  // Ensure we have read all needed data
  size_t max_bytes = (size_t)nchars * UTF8_MAX_BYTES;

  if (lex->buf_len < max_bytes) {
    // Attempt to read more
    size_t toread = max_bytes - lex->buf_len;
    rc = lex_read_more(lex, toread);
    TSCFG_CHECK(rc);
  }

  // Decode utf-8
  int read_chars = 0;
  size_t buf_pos = lex->buf_pos;
  size_t buf_end = lex->buf_pos + lex->buf_len;

  while (read_chars < nchars && buf_pos < buf_end) {
    unsigned char b = lex->buf[buf_pos];
    size_t enc_len;
    tscfg_char_t c;

    rc = tscfg_decode_byte1(b, &enc_len, &c);
    TSCFG_CHECK(rc);

    // If valid UTF-8, should be enough bytes left in buffer
    if (enc_len > (buf_end - buf_pos)) {
      lex_report_err(lex, "Incomplete UTF-8 character at end of input");
      return TSCFG_ERR_INVALID;
    }

    buf_pos++; // Move past first byte of char
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
  memcpy(buf, &lex->buf[lex->buf_pos], *got);
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

  // TODO: read extra?

  // Move data back when needed to free space at end
  if (lex->buf_pos + size_needed > lex->buf_size) {
    memmove(lex->buf, &lex->buf[lex->buf_pos], lex->buf_len);
    lex->buf_pos = 0;
  }

  size_t read_bytes = 0;
  rc = lex_read(lex, &lex->buf[lex->buf_pos + lex->buf_len], bytes, &read_bytes);
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
  for (int char_pos = 0; char_pos < chars; char_pos++) {
    tscfg_char_t c;
    size_t enc_len;
    unsigned char b = lex->buf[lex->buf_pos];

    rc = tscfg_decode_byte1(b, &enc_len, &c);
    assert(rc == TSCFG_OK); // Check caller was correct

    assert(lex->buf_len >= enc_len);
    lex->buf_pos += enc_len;
    lex->buf_len -= enc_len;

    lex_update_line(lex, b);
  }
}

/*
 * Eat a certain number of single-byte ascii characters.
 * Assertion error max occur if char isn't ascii
 */
static void lex_eat_ascii(tscfg_lex_state *lex, size_t bytes) {
  for (size_t i = 0; i < bytes; i++) {
    unsigned char b = lex->buf[lex->buf_pos + i];
    assert(b <= 127);

    lex_update_line(lex, b);
  }

  lex->buf_pos += bytes;
}

/*
 * Update line position based on first byte of consumed character
 */
static void lex_update_line(tscfg_lex_state *lex, unsigned char b) {
  if (is_hocon_newline(b)) {
    lex->line++;
    lex->line_char = 1;
  } else {
    lex->line_char++;
  }
}

/*
 * Copy first UTF-8 character.
 * Assumes that it has been validated already.
 */
static tscfg_rc lex_copy_char(tscfg_lex_state *lex, tscfg_strbuf *sb,
                            bool aggressive_resize) {
  tscfg_rc rc;

  size_t enc_len;
  tscfg_char_t c;
  unsigned char b = lex->buf[0];
  rc = tscfg_decode_byte1(b, &enc_len, &c);

  // Check caller called when in valid state
  assert(rc == TSCFG_OK);

  assert(enc_len <= lex->buf_len);

  rc = strbuf_expand(sb, sb->len + enc_len, aggressive_resize);
  memcpy(&sb->str[sb->len], lex->buf, enc_len);
  sb->len += enc_len;

  lex_update_line(lex, b);
  lex->buf_pos += enc_len;

  return TSCFG_OK;
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

/*
 * Extract up to but not including newline.
 */
static tscfg_rc extract_line_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str) {
  tscfg_rc rc;

  tscfg_strbuf sb;
  bool found_nl;
  if (include_str) {
    rc = strbuf_init(&sb, 32);
    TSCFG_CHECK(rc);

    // Don't care if we found newline
    rc = extract_until(lex, &sb, '\n', &found_nl);
    TSCFG_CHECK_GOTO(rc, cleanup);
  } else {
    strbuf_init_empty(&sb);

    // Don't care if we found newline
    rc = eat_until(lex, '\n', &found_nl);
    TSCFG_CHECK_GOTO(rc, cleanup);
  }

  // Don't care if we found newline

  if (include_str) {
    set_str_tok(TSCFG_TOK_COMMENT, &sb, tok);
  } else {
    set_nostr_tok(TSCFG_TOK_COMMENT, tok);
  }

  return TSCFG_OK;

cleanup:
  strbuf_free(&sb);
  return rc;
}

/*
 * Search for matching char and consume up to, but not including,
 * that character.
 * sb: append characters consumed to this buffer
 * match: char to search for
 * found: set to true/false if successfully completed
 */
static tscfg_rc extract_until(tscfg_lex_state *lex, tscfg_strbuf *sb,
                              tscfg_char_t match, bool *found) {
  tscfg_rc rc;

  while (true) {
    tscfg_char_t c;
    int got;
    rc = lex_peek(lex, &c, 1, &got);
    TSCFG_CHECK(rc);

    if (got == 0) {
      *found = false;
      return TSCFG_OK;
    }

    if (c == match) {
      *found = true;
      return TSCFG_OK;
    }

    rc = lex_copy_char(lex, sb, true);
    TSCFG_CHECK(rc);
  }
}

/*
 * Search for matching char and consume up to, but not including,
 * that character.
 * match: char to search for
 * found: set to true/false if successfully completed
 */
static tscfg_rc eat_until(tscfg_lex_state *lex, tscfg_char_t match,
                          bool *found) {
  tscfg_rc rc;

  while (true) {
    tscfg_char_t c;
    int got;
    rc = lex_peek(lex, &c, 1, &got);
    TSCFG_CHECK(rc);

    if (got == 0) {
      *found = false;
      return TSCFG_OK;
    }

    if (c == match) {
      *found = true;
      return TSCFG_OK;
    }

    lex_eat(lex, 1);
  }
}

/*
 * Search for end of multiline comment, i.e * then /
 */
static tscfg_rc extract_multiline_comment(tscfg_lex_state *lex, tscfg_tok *tok,
                                     bool include_str) {
  tscfg_rc rc;

  tscfg_strbuf sb;
  if (include_str) {
    rc = strbuf_init(&sb, 64);
    TSCFG_CHECK(rc);
  } else {
    strbuf_init_empty(&sb);
  }

  while (true) {
    bool found_ast;

    if (include_str) {
      rc = extract_until(lex, &sb, '*', &found_ast);
      TSCFG_CHECK_GOTO(rc, cleanup);
    } else {
      rc = eat_until(lex, '*', &found_ast);
      TSCFG_CHECK_GOTO(rc, cleanup);
    }

    int got;
    tscfg_char_t buf[2];
    rc = lex_peek(lex, buf, 2, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got < 2) {
      // Cannot be comment close, therefore unclosed comment
      lex_report_err(lex, "/* comment without matching */");
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }

    if (buf[1] == '/') {
      lex_eat(lex, 2);
      break;
    } else {
      // * is part of comment
      if (include_str) {
        rc = lex_copy_char(lex, &sb, true);
        TSCFG_CHECK(rc);
      } else {
        lex_eat(lex, 1);
      }
    }
  }

  if (include_str) {
    set_str_tok(TSCFG_TOK_COMMENT, &sb, tok);
  } else {
    set_nostr_tok(TSCFG_TOK_COMMENT, tok);
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
  if (include_str) {
    rc = strbuf_init(&sb, 128);
    TSCFG_CHECK(rc);
  } else {
    strbuf_init_empty(&sb);
  }

  bool saw_newline = false;

  while (true) {
    tscfg_char_t c;
    int got;
    rc = lex_peek(lex, &c, 1, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got == 0 || !is_hocon_whitespace(c)) {
      break;
    }

    if (is_hocon_newline(c)) {
      saw_newline = true;
    }

    if (include_str) {
      rc = lex_copy_char(lex, &sb, true);
      TSCFG_CHECK_GOTO(rc, cleanup);
    } else {
      lex_eat(lex, 1);
    }
  }

  tscfg_tok_tag tag = saw_newline ? TSCFG_TOK_WS_NEWLINE : TSCFG_TOK_WS;
  if (include_str) {
    set_str_tok(tag, &sb, tok);
  } else {
    set_nostr_tok(tag, tok);
  }

  return TSCFG_OK;

cleanup:
  strbuf_free(&sb);
  return rc;
}

/*
 * Extract HOCON variable.
 * Assume initial $ has been consumed.
 * Store variable name into token.
 */
static tscfg_rc extract_var(tscfg_lex_state *lex, tscfg_tok *tok) {
  // TODO: var lexing
  return TSCFG_ERR_UNIMPL;
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

  // Can assume c is in ascii range, so is same in UTF-8
  assert(c <= 127);
  sb.len = 1;
  sb.str[0] = (char)c;

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

    size_t nbytes = 0;
    while (nbytes < got) {
      char b = pos[nbytes];
      if ((b >= '0' && b <= '9')) {
        nbytes++;
      } else if (!saw_dec_point && b == '.') {
        saw_dec_point = true;
        nbytes++;
      } else {
        break;
      }
    }

    if (nbytes > 0) {
      // Any characters consumed are in ASCII range
      lex_eat_ascii(lex, nbytes);
      sb.len += nbytes;
    }

    if (nbytes < got || got == 0) {
      // End of whitespace or file
      break;
    }
  }

  set_str_tok(TSCFG_TOK_NUMBER, &sb, tok);
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
  lex_eat_ascii(lex, 1);

  char buf[2];
  size_t got = 0;
  rc = lex_peek_bytes(lex, buf, 2, &got);
  TSCFG_CHECK(rc);

  if (got == 2 && memcmp(buf, "\"\"", 2) == 0) {
    lex_eat_ascii(lex, 2);
    // Multiline string
    return extract_hocon_multiline_str(lex, tok);
  } else {
    // Regular JSON string
    return extract_json_str(lex, tok);
  }
}

/*
 * Extract JSON string, processing escape codes.
 * Assume initial " has been consumed.
 */
static tscfg_rc extract_json_str(tscfg_lex_state *lex, tscfg_tok *tok) {
  tscfg_rc rc;
  tscfg_strbuf sb;
  rc = strbuf_init(&sb, 32);
  TSCFG_CHECK(rc);

  bool end_of_string = false;

  do {
    tscfg_char_t c;
    int got;
    rc = lex_peek(lex, &c, 1, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got == 0) {
      lex_report_err(lex, "String missing closing \"");
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }

    if (c == '"') {
      lex_eat(lex, 1);
      end_of_string = true;
    } else if (c == '\\') {
      lex_eat(lex, 1);
      tscfg_char_t escaped;

      rc = extract_json_str_escape(lex, &escaped);
      TSCFG_CHECK_GOTO(rc, cleanup);

      rc = strbuf_append_utf8(&sb, c, true);
      TSCFG_CHECK_GOTO(rc, cleanup);
    } else {
      rc = lex_copy_char(lex, &sb, true);
      TSCFG_CHECK_GOTO(rc, cleanup);
    }
  } while (!end_of_string);

  set_str_tok(TSCFG_TOK_STRING, &sb, tok);
  return TSCFG_OK;

cleanup:
  strbuf_free(&sb);
  return rc;
}

/*
 * Handle a JSON string escape code.
 *
 * Lexer should be positioned just past initial \, and it advanced past
 * end of escape code if valid
 * result: escaped character
 */
static tscfg_rc
extract_json_str_escape(tscfg_lex_state *lex, tscfg_char_t *result) {
  tscfg_rc rc;
  tscfg_char_t first;
  int got;

  rc = lex_peek(lex, &first, 1, &got);
  TSCFG_CHECK(rc);

  if (got == 0) {
    lex_report_err(lex, "\\ without escape code in string");
    return TSCFG_ERR_SYNTAX;
  }

  switch (first) {
    case '\\':
    case '"':
    case '/':
      *result = first;
      lex_eat(lex, 1);
      break;
    case 'b':
      *result = '\b';
      lex_eat(lex, 1);
      break;
    case 'f':
      *result = '\f';
      lex_eat(lex, 1);
      break;
    case 'n':
      *result = '\n';
      lex_eat(lex, 1);
      break;
    case 'r':
      *result = '\r';
      lex_eat(lex, 1);
      break;
    case 't':
      *result = '\t';
      lex_eat(lex, 1);
      break;
    case 'u':
      lex_eat(lex, 1);

      rc = extract_unicode_escape(lex, result);
      TSCFG_CHECK(rc);

      return TSCFG_OK;
    default:
      // TODO: invalid escape codes
      return TSCFG_ERR_UNIMPL;
  }

  return TSCFG_OK;
}

/*
 * Decode unicode escape code at current position.
 * Escape has four hex character digits (e.g. '1', 'A', 'f')
 */
static tscfg_rc extract_unicode_escape(tscfg_lex_state *lex, tscfg_char_t *c) {
  tscfg_rc rc;
  tscfg_char_t unicode_esc[UNICODE_ESCAPE_LEN];
  int got;

  rc = lex_peek(lex, unicode_esc, UNICODE_ESCAPE_LEN, &got);
  TSCFG_CHECK(rc);

  if (got < UNICODE_ESCAPE_LEN) {
    lex_report_err(lex, "Incomplete unicode escape \\uxxxx in string: "
                        "expected four hexadecimal digits");
    return TSCFG_ERR_SYNTAX;
  }

  tscfg_char_t unicode = 0;
  for (int i = 0; i < UNICODE_ESCAPE_LEN; i++) {
    tscfg_char_t hex_c = unicode_esc[i];
    unicode <<= 4;
    if (hex_c >= '0' && hex_c <= '9') {
      unicode += (hex_c - '0');
    } else if (hex_c >= 'A' && hex_c <= 'F') {
      unicode += ((hex_c - 'A') + 10);
    } else if (hex_c >= 'a' && hex_c <= 'f') {
      unicode += ((hex_c - 'a') + 10);
    } else {
      lex_report_err(lex, "Invalid unicode escape: digit %i (%lc):"
                          "hexadecimal digit", i + 1, hex_c);
      return TSCFG_ERR_SYNTAX;
    }
  }

  lex_eat(lex, UNICODE_ESCAPE_LEN);
  *c = unicode;
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

  bool in_string = true;
  while (in_string) {
    bool found_quote;

    rc = extract_until(lex, &sb, '"', &found_quote);
    TSCFG_CHECK_GOTO(rc, cleanup);

    const size_t lookahead_bytes = 4;
    char buf[lookahead_bytes];
    size_t got = 0;
    rc = lex_peek_bytes(lex, buf, lookahead_bytes, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got < 3) {
      lex_report_err(lex, "Unterminated \"\"\" string");
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }

    if (memcmp(buf, "\"\"\"", 3) == 0) {
      // Need to match last """ according to HOCON
      if (buf[3] == '"') {
        // Consume first ", then try again
        rc = lex_copy_char(lex, &sb, true);
        TSCFG_CHECK_GOTO(rc, cleanup);
      } else {
        // Closing """, don't append to string
        lex_eat_ascii(lex, 3);
        in_string = false;
      }
    } else {
      // Not terminating quote, append and move on
      rc = lex_copy_char(lex, &sb, true);
      TSCFG_CHECK_GOTO(rc, cleanup);
    }
  }

  set_str_tok(TSCFG_TOK_STRING, &sb, tok);
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

  while (true) {
    const int UNQUOTED_LOOKAHEAD = 2;
    // Need to interpret as unicode
    tscfg_char_t buf[UNQUOTED_LOOKAHEAD];
    int got;
    rc = lex_peek(lex, buf, UNQUOTED_LOOKAHEAD, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got == 0) {
      break;
    }

    if (!is_hocon_unquoted_char(buf[0]) &&
        is_hocon_whitespace(buf[0])) {
      // Cases where unquoted text definitely terminates
      break;
    }

    // Can check for comment with lookahead two
    if (is_comment_start(buf, got)) {
      break;
    }

    // Append first character and advance
    rc = lex_copy_char(lex, &sb, true);
    TSCFG_CHECK_GOTO(rc, cleanup);

  }

  set_str_tok(TSCFG_TOK_UNQUOTED, &sb, tok);

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
    lex_eat_ascii(lex, kwlen);

    set_nostr_tok(kwtag, tok);
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
static bool is_comment_start(const tscfg_char_t *buf, int len) {
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

/*
 * Initialise to empty state
 */
static void strbuf_init_empty(tscfg_strbuf *sb) {
  sb->str = NULL;
  sb->size = 0;
  sb->len = 0;
}

/*
 * Allocate to be large enough to hold string of init_size
 */
static tscfg_rc strbuf_init(tscfg_strbuf *sb, size_t init_size) {
  if (init_size == 0) {
    sb->str = NULL;
  } else {
    init_size++; // Null term

    sb->str = malloc(init_size);
    TSCFG_CHECK_MALLOC(sb->str);
  }

  sb->size = init_size;
  sb->len = 0;
  return TSCFG_OK;
}

/*
 * Expand to hold string of length min_size, plus null terminator.
 * aggressive: if true, aggressively allocate memory
 */
static tscfg_rc strbuf_expand(tscfg_strbuf *sb, size_t min_size,
                              bool aggressive) {
  min_size++; // Null term

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

static void strbuf_finalize(tscfg_strbuf *sb) {
  if (sb->str == NULL) {
    assert(sb->size == 0 && sb->len == 0);
  } else {
    assert(sb->size > sb->len); // Need space for null term
    sb->str[sb->len] = '\0';
  }
}

static void strbuf_free(tscfg_strbuf *sb) {
  free(sb->str);
}

static tscfg_rc strbuf_append_utf8(tscfg_strbuf *sb, tscfg_char_t c,
                              bool aggressive) {
  size_t enc_len = 0; // TODO: utf8 encoded length function
  size_t min_size = sb->len + enc_len;
  tscfg_rc rc = strbuf_expand(sb, min_size, aggressive);
  TSCFG_CHECK(rc);

  char *pos = &sb->str[sb->len];
  // TODO: append bytes to resized buffer

  sb->len += enc_len;
  return TSCFG_ERR_UNIMPL;
}
