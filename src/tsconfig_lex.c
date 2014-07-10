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

static tscfg_rc lex_peek(tscfg_lex_state *lex, char *buf, int len, int *got);
static tscfg_rc lex_read_more(tscfg_lex_state *lex, int bytes);
static tscfg_rc lex_read(ts_config_input *in, unsigned char *buf, int bytes,
                         int *read_bytes);
static tscfg_rc lex_eat(tscfg_lex_state *lex, int bytes);

static tscfg_rc eat_hocon_comm_ws(tscfg_lex_state *lex, int *read);
static tscfg_rc eat_hocon_comment(tscfg_lex_state *lex, int *read);
static tscfg_rc eat_rest_of_line(tscfg_lex_state *lex, int *read);
static tscfg_rc eat_until_comm_end(tscfg_lex_state *lex, int *read);
static tscfg_rc eat_json_whitespace(tscfg_lex_state *lex, int *read);

static bool is_json_whitespace(char c);

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

/*
  Read the next token from the input stream.
 */
tscfg_rc tscfg_read_tok(tscfg_lex_state *lex, tscfg_tok *tok, bool *eof) {
  tscfg_rc rc;
  int read;
  rc = eat_hocon_comm_ws(lex, &read);
  TSCFG_CHECK(rc);

  // TODO: implement lexing logic here

  return TSCFG_ERR_UNIMPL;
}

static tscfg_rc lex_peek(tscfg_lex_state *lex, char *buf, int len, int *got) {
  assert(len >= 0);
  tscfg_rc rc;

  if (lex->buf_len < len) {
    // Attempt to read more
    int toread = len - lex->buf_len;
    rc = lex_read_more(lex, toread);
    TSCFG_CHECK(rc);
  }

  *got = lex->buf_len >= len ? len : (int)lex->buf_len;
  memcpy(buf, lex->buf, (size_t)*got);
  return TSCFG_OK;
}

/*
 * Read additional bytes into buffers.
 */
static tscfg_rc lex_read_more(tscfg_lex_state *lex, int bytes) {
  tscfg_rc rc;
  size_t size_needed = (size_t)bytes + lex->buf_len;

  if (size_needed >= lex->buf_size) {
    // TODO: resize buffer
    return TSCFG_ERR_UNIMPL;
  }

  int read_bytes = 0;
  rc = lex_read(&lex->in, &lex->buf[lex->buf_len], bytes, &read_bytes);
  TSCFG_CHECK(rc);

  lex->buf_len += read_bytes;

  return TSCFG_OK;
}

static tscfg_rc lex_read(ts_config_input *in, unsigned char *buf, int bytes,
                         int *read_bytes) {
  // TODO: other inputs
  assert(in->kind == TS_CONFIG_IN_FILE);
  assert(bytes >= 0);

  size_t read = fread(buf, 1, (size_t)bytes, in->data.f);
  if (read != bytes) {
    // TODO: better error handling
    tscfg_report_err("Error reading for input file");
    return TSCFG_ERR_IO;
  }

  return TSCFG_OK;
}

static tscfg_rc lex_eat(tscfg_lex_state *lex, int bytes) {
  assert(bytes >= lex->buf_len);

  // Bump data forward
  // TODO: ring buffer would be more efficient
  size_t remaining = lex->buf_len - bytes;
  if (remaining > 0) {
    memmove(&lex->buf[0], &lex->buf[bytes], remaining);
    lex->buf_len = remaining;
  }

  return TSCFG_OK;
}

/*
 * Consume comments and whitespace.
 */
static tscfg_rc eat_hocon_comm_ws(tscfg_lex_state *lex, int *read) {
  tscfg_rc rc;
  int total_read = 0;
  while (true) {
    int ws_read = 0;
    // Follows JSON whitespace rules
    rc = eat_json_whitespace(lex, &ws_read);
    TSCFG_CHECK(rc);

    total_read += ws_read;

    int comm_read = 0;
    rc = eat_hocon_comment(lex, &comm_read);
    TSCFG_CHECK(rc);

    if (ws_read == 0 && comm_read == 0) {
      break;
    }
  }

  return TSCFG_OK;
}

static tscfg_rc eat_hocon_comment(tscfg_lex_state *lex, int *read) {
  tscfg_rc rc;

  *read = 0;

  /*
   * Look ahead two characters for // or /* or one for #
   */
  char c[2];
  int got;
  rc = lex_peek(lex, c, 2, &got);
  TSCFG_CHECK(rc);

  if (got >= 1 && c[0] == '#') {
    rc = lex_eat(lex, 1);
    TSCFG_CHECK(rc);
    (*read)++;

    int line_read = 0;
    rc = eat_rest_of_line(lex, &line_read);
    TSCFG_CHECK(rc);
    (*read) += line_read;
  } else if (got == 2 &&
      c[0] == '/' &&
      (c[1] == '/' || c[1] == '*')) {

    rc = lex_eat(lex, 2);
    TSCFG_CHECK(rc);
    (*read) += 2;

    if (c[1] == '/') {
      int line_read;
      rc = eat_rest_of_line(lex, &line_read);
      TSCFG_CHECK(rc);
      (*read) += line_read;
    } else {
      assert(c[1] == '*');
      int comm_read = 0;
      rc = eat_until_comm_end(lex, &comm_read);
      TSCFG_CHECK(rc);
      (*read) += comm_read;
    }
  }

  return TSCFG_OK;
}

static tscfg_rc eat_rest_of_line(tscfg_lex_state *lex, int *read) {
  tscfg_rc rc;

  *read = 0;

  while (true) {
    char c;
    int got;
    rc = lex_peek(lex, &c, 1, &got);
    TSCFG_CHECK(rc);


    if (got == 0) {
      break;
    }

    rc = lex_eat(lex, 1);
    TSCFG_CHECK(rc);

    (*read)++;

    if (c == '\n') {
      (*read)++;
      break;
    }
  }

  return TSCFG_OK;
}

/*
 * Search for end of /* comment
 */
static tscfg_rc eat_until_comm_end(tscfg_lex_state *lex, int *read) {
  tscfg_rc rc;

  *read = 0;

  while (true) {
    char c[2];
    int got;
    rc = lex_peek(lex, c, 2, &got);
    TSCFG_CHECK(rc);

    if (got == 2) {
      if (c[0] == '*' && c[1] == '/') {
        rc = lex_eat(lex, 2);
        TSCFG_CHECK(rc);

        (*read) += 2;

        break;
      } else {
        // Need to check next position
        int advance = c[1] == '*' ? 1 : 2;
        rc = lex_eat(lex, advance);
        TSCFG_CHECK(rc);

        (*read) += advance;
      }
    } else {
      // TODO: better error report with line number, etc
      tscfg_report_err("/* comment without matching */");
      return TSCFG_ERR_SYNTAX;
    }
  }

  return TSCFG_OK;
}


/*
 * Remove any leading whitespace characters from input.
 */
static tscfg_rc eat_json_whitespace(tscfg_lex_state *lex, int *read) {
  tscfg_rc rc;

  *read = 0;
  ts_config_input *in = &lex->in;

  while (true) {
    char c;
    int got;
    rc = lex_peek(lex, &c, 1, &got);
    TSCFG_CHECK(rc);

    if (got == 0) {
      // End of file
      break;
    }

    assert(got == 0);

    if (is_json_whitespace(c)) {
      rc = lex_eat(lex, 1);
      TSCFG_CHECK(rc);

      (*read)++;
    } else {
      break;
    }
  }

  return TSCFG_OK;
}

static bool is_json_whitespace(char c) {
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
