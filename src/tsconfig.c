/*
 * C parser for HOCON properties file (Typesafe's configuration format).
 *
 * For HOCON reference: https://github.com/typesafehub/config
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 9 July 2014
 * All rights reserved.
 */

#include "tsconfig.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tsconfig_err.h"
#include "tsconfig_lex.h"

typedef struct {
  tscfg_lex_state lex_state;

  tscfg_tok *toks;
  int toks_size;
  int ntoks;
} ts_parse_state;

static tscfg_rc ts_parse_state_init(ts_parse_state *state, ts_config_input in);
static void ts_parse_state_finalize(ts_parse_state *state);
static void ts_parse_report_err(ts_parse_state *state, const char *fmt, ...);

static tscfg_rc parse_hocon_body(ts_parse_state *state, ts_config *cfg);

static tscfg_rc peek_tok(ts_parse_state *state, tscfg_tok *toks,
                       int count, int *got);
static tscfg_rc peek_tag(ts_parse_state *state, tscfg_tok_tag *tag);
static tscfg_rc next_tok_matches(ts_parse_state *state, tscfg_tok_tag tag,
                       bool *match);

static void pop_toks(ts_parse_state *state, int count);
static char *pop_tok_str(ts_parse_state *state, size_t *len);

static tscfg_rc skip_whitespace(ts_parse_state *state);

tscfg_rc parse_ts_config(ts_config_input in, tscfg_fmt fmt, ts_config *cfg) {
  if (fmt == TSCFG_HOCON) {
    return parse_hocon(in, cfg);
  } else {
    tscfg_report_err("Invalid file format code %i", (int)fmt);
    return TSCFG_ERR_ARG;
  }
}

tscfg_rc parse_hocon(ts_config_input in, ts_config *cfg) {
  ts_parse_state state;
  tscfg_rc rc = TSCFG_ERR_UNKNOWN;

  rc = ts_parse_state_init(&state, in);
  TSCFG_CHECK_GOTO(rc, cleanup);

  rc = skip_whitespace(&state);
  TSCFG_CHECK_GOTO(rc, cleanup);

  // TODO: open square bracket also supported
  bool open_brace;
  rc = next_tok_matches(&state, TSCFG_TOK_OPEN_BRACE, &open_brace);
  TSCFG_CHECK_GOTO(rc, cleanup);

  if (open_brace) {
    pop_toks(&state, 1);
  }

  // body of JSON
  rc = parse_hocon_body(&state, cfg);
  TSCFG_CHECK_GOTO(rc, cleanup);

  if (open_brace) {
    rc = skip_whitespace(&state);
    TSCFG_CHECK_GOTO(rc, cleanup);

    bool close_brace;
    rc = next_tok_matches(&state, TSCFG_TOK_CLOSE_BRACE, &close_brace);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (!close_brace) {
      ts_parse_report_err(&state, "Expected close brace to match initial "
                                    "open");
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }

    pop_toks(&state, 1);
  }

  rc = skip_whitespace(&state);
  TSCFG_CHECK_GOTO(rc, cleanup);

  tscfg_tok tok;
  int got;
  rc = peek_tok(&state, &tok, 1, &got);
  assert(got == 1); // Should get at least end of file
  if (tok.tag == TSCFG_TOK_EOF) {
    pop_toks(&state, 1);
  } else {
    // TODO: include token tag
    ts_parse_report_err(&state, "Trailing tokens, starting with: %.*s",
                             (int)tok.length, tok.str);
  }

  rc = TSCFG_OK;
cleanup:
  ts_parse_state_finalize(&state);

  if (rc != TSCFG_OK) {
    // TODO: cleanup partially built config on error
  }
  return rc;
}

/*
 * Parse contents between { and }.
 */
static tscfg_rc parse_hocon_body(ts_parse_state *state, ts_config *cfg) {
  tscfg_rc rc;

  rc = skip_whitespace(state);
  TSCFG_CHECK(rc)

  // Only handle empty body for now
  tscfg_tok_tag tag;
  rc = peek_tag(state, &tag);
  TSCFG_CHECK(rc);
  if (tag == TSCFG_TOK_CLOSE_BRACE ||
      tag == TSCFG_TOK_EOF) {
    return TSCFG_OK;
  }

  // TODO: implement
  // K/V pair:
  // ---------
  // * quoted or unquoted key token
  // * separator: ':', '=', or not needed if open brace
  // * value: nested object, or token or expression, or multiple tokens
  // * optional comma - not needed if line separator
  // Include:
  // --------
  // * include file and merge object
  return TSCFG_ERR_UNIMPL;
}

static tscfg_rc ts_parse_state_init(ts_parse_state *state, ts_config_input in) {
  tscfg_rc rc;

  rc = tscfg_lex_init(&state->lex_state, in);
  TSCFG_CHECK(rc);

  state->toks_size = 1;
  state->toks = malloc(sizeof(state->toks[0]) * (size_t)state->toks_size);
  TSCFG_CHECK_MALLOC(state->toks);
  state->ntoks = 0;

  return TSCFG_OK;
}

static void ts_parse_state_finalize(ts_parse_state *state) {
  tscfg_lex_finalize(&state->lex_state);

  // Free memory
  free(state->toks);
  state->toks = NULL;
  state->toks_size = 0;
  state->ntoks = 0;
}

static void ts_parse_report_err(ts_parse_state *state, const char *fmt, ...) {
  // TODO: include context in errors.
  va_list args;
  va_start(args, fmt);
  tscfg_report_err_v(fmt, args);
  va_end(args);
}

/*
 * Peek ahead into tokens without removing
 * count: number of tokens to look ahead
 * got: number of tokens got, less than len iff end of file
 */
static tscfg_rc peek_tok(ts_parse_state *state, tscfg_tok *toks,
                       int count, int *got) {
  tscfg_rc rc;

  // Resize token buffer to be large enough
  if (count > state->toks_size) {
    void *tmp = realloc(state->toks,
            sizeof(state->toks[0]) * (size_t)count);
    TSCFG_CHECK_MALLOC(tmp);

    state->toks_size = count;
  }



  while (state->ntoks < count) {
    if (state->ntoks > 0 &&
        state->toks[state->ntoks - 1].tag == TSCFG_TOK_EOF) {
      // Already hit end of file
      break;
    }

    rc = tscfg_read_tok(&state->lex_state, &state->toks[state->ntoks]);
    TSCFG_CHECK(rc);

    state->ntoks++;
  }

  *got = state->ntoks < count ? state->ntoks : count;
  memcpy(toks, state->toks, sizeof(toks[0]) * (size_t)*got);
  return TSCFG_OK;
}

/*
 * tag: set to next tag, TSCFG_TOK_EOF if no more
 */
static tscfg_rc peek_tag(ts_parse_state *state, tscfg_tok_tag *tag) {
  tscfg_tok tok;
  int got;
  tscfg_rc rc = peek_tok(state, &tok, 1, &got);
  TSCFG_CHECK(rc);

  if (got == 0) {
    *tag = TSCFG_TOK_EOF;
  } else {
    *tag = tok.tag;
  }

  return TSCFG_OK;
}

/*
 * Check if next token matches.
 * match: set to false if no next token (EOF) or if token is different type
 */
static tscfg_rc next_tok_matches(ts_parse_state *state, tscfg_tok_tag tag,
                       bool *match) {
  tscfg_tok_tag next_tag;
  tscfg_rc rc = peek_tag(state, &next_tag);
  TSCFG_CHECK(rc);

  *match = (next_tag == tag);

  return TSCFG_OK;
}

/*
 * Remove leading tokens from input.
 *
 * Caller must ensure that there are at least count tokens
 */
static void pop_toks(ts_parse_state *state, int count) {
  assert(count >= 0);
  assert(count <= state->ntoks);

  // Cleanup memory first
  for (int i = 0; i < count; i++) {
    tscfg_tok *tok = &state->toks[i];
    if (tok->str != NULL) {
      free(tok->str);
    }
  }

  memmove(&state->toks[0], &state->toks[count], state->ntoks - count);
  state->ntoks -= count;
}

/*
 * Remove any leading whitespace tokens.
 */
static tscfg_rc skip_whitespace(ts_parse_state *state) {
  tscfg_rc rc;

  while (true) {
    tscfg_tok_tag tag;

    rc = peek_tag(state, &tag);
    TSCFG_CHECK(rc);

    if (tag == TSCFG_TOK_NEWLINE) {
      pop_toks(state, 1);
    } else {
      return TSCFG_OK;
    }
  }
}

/*
 * Remove first token and take ownership of string.
 *
 * Caller must ensure that there is at least one valid token.
 */
static char *pop_tok_str(ts_parse_state *state, size_t *len) {
  assert(state->ntoks >= 1);
  char *str = tscfg_own_token(&state->toks[0], len);

  pop_toks(state, 1);
  return str;
}
