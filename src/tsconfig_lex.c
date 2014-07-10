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

#include "tsconfig_err.h"

static tscfg_rc eat_hocon_comm_ws(ts_config_input *in, int *read);
static tscfg_rc eat_hocon_comments(ts_config_input *in, int *read);
static tscfg_rc eat_json_whitespace(ts_config_input *in, int *read);
static bool is_json_whitespace(char c);

/*
  Read the next token from the input stream.
 */
tscfg_rc tscfg_read_tok(ts_config_input *in, ts_tok *tok, bool *eof) {
  // TODO: implement lexing logic here
  tscfg_rc rc;
  int read;
  rc = eat_hocon_comm_ws(in, &read);
  TSCFG_CHECK(rc);

  return TSCFG_ERR_UNIMPL;
}

/*
 * Consume comments and whitespace.
 */
static tscfg_rc eat_hocon_comm_ws(ts_config_input *in, int *read) {
  tscfg_rc rc;
  int total_read = 0;
  while (true) {
    int ws_read = 0;
    // Follows JSON whitespace rules
    rc = eat_json_whitespace(in, &ws_read);
    TSCFG_CHECK(rc);

    total_read += ws_read;

    int comm_read = 0;
    rc = eat_hocon_comments(in, &comm_read);
    TSCFG_CHECK(rc);

    if (ws_read == 0 && comm_read == 0) {
      break;
    }
  }

  return TSCFG_OK;
}

static tscfg_rc eat_hocon_comments(ts_config_input *in, int *read) {
  // TODO: implement comment lexing
  return TSCFG_ERR_UNIMPL;
}

/*
 * Remove any leading whitespace characters from input.
 */
static tscfg_rc eat_json_whitespace(ts_config_input *in, int *read) {
  *read = 0;

  while (true) {
    // TODO: other inputs
    assert(in->kind == TS_CONFIG_IN_FILE);

    // TODO: better approach to reading
    int c = getc(in->data.f);

    if (c == EOF) {
      // TODO: check for other error
      // End of file
      break;
    }

    if (!is_json_whitespace((char)c)) {
      int tmp = ungetc(c, in->data.f);
      if (tmp != c) {
        tscfg_report_err("Error pushing char back on stream.");
        return TSCFG_ERR_UNKNOWN;
      }
      break;
    }

    (*read)++;
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
