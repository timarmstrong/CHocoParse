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
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tsconfig_lex.h"

typedef struct {
  ts_config_input in; 

  ts_tok *toks;
  int toks_size;
  int ntoks;
} ts_parse_state;

void tscfg_report_err(const char *fmt, ...);
void tscfg_report_err_v(const char *fmt, va_list args);

#define TSCFG_CHECK(rc) { \
  tscfg_rc __rc = (rc);               \
  if (__rc != TSCFG_OK) return __rc; }

#define TSCFG_CHECK_GOTO(rc, label) { \
  if ((rc) != TSCFG_OK) goto label; }

#define TSCFG_CHECK_MALLOC(ptr) { \
  if ((ptr) == NULL) return TSCFG_ERR_OOM; }

tscfg_rc ts_parse_state_init(ts_parse_state *state, ts_config_input in);
void ts_parse_state_finalize(ts_parse_state *state);
void ts_parse_report_err(ts_parse_state *state, const char *fmt, ...);

tscfg_rc parse_hocon_body(ts_parse_state *state, ts_config *cfg);

tscfg_rc ts_parse_peek(ts_parse_state *state, ts_tok *toks,
                       int count, int *got);
tscfg_rc ts_parse_next_matches(ts_parse_state *state, const char *str,
                               size_t len, bool *match);

static tscfg_rc pop_toks(ts_parse_state *state, int count);


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
  
  bool open_brace;
  rc = ts_parse_next_matches(&state, "{", 1, &open_brace);
  TSCFG_CHECK_GOTO(rc, cleanup);

  if (open_brace) {
    rc = pop_toks(&state, 1);
    TSCFG_CHECK_GOTO(rc, cleanup);
  }

  // body of JSON
  rc = parse_hocon_body(&state, cfg);
  TSCFG_CHECK_GOTO(rc, cleanup);

  if (open_brace) {
    bool close_brace;
    rc = ts_parse_next_matches(&state, "}", 1, &close_brace);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (!close_brace) {
      ts_parse_report_err(&state, "Expected close brace to match initial "
                                    "open");
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }
    
    rc = pop_toks(&state, 1);
    TSCFG_CHECK_GOTO(rc, cleanup);
  }

  ts_tok tok;
  int got;
  rc = ts_parse_peek(&state, &tok, 1, &got);
  if (got != 0) {
    assert(got == 1);
    ts_parse_report_err(&state, "Trailing tokens, starting with: %.*s",
                             (int)tok.length, tok.str); 
  }

  rc = TSCFG_OK;
cleanup:
  ts_parse_state_finalize(&state);

  if (rc != TSCFG_OK) {
    // TODO: cleanup partially built config
  }
  return rc;
}

/*
 * Parse contents between { and }.
 */
tscfg_rc parse_hocon_body(ts_parse_state *state, ts_config *cfg) {
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

tscfg_rc ts_parse_state_init(ts_parse_state *state, ts_config_input in) {
  state->in = in;
  // TODO: additional buffering?
  
  state->toks_size = 1;
  state->toks = malloc(sizeof(state->toks[0]) * (size_t)state->toks_size);
  TSCFG_CHECK_MALLOC(state->toks);
  state->ntoks = 0;

  return TSCFG_OK;
}

void ts_parse_state_finalize(ts_parse_state *state) {
  // Invalidate input
  state->in.kind = TS_CONFIG_IN_NONE;

  // Free memory
  free(state->toks);
  state->toks = NULL;
  state->toks_size = 0;
  state->ntoks = 0;
}

void ts_parse_report_err(ts_parse_state *state, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  tscfg_report_err_v(fmt, args);
  va_end(args);
}

/*
 * Peek ahead into tokens without removing
 * count: number of tokens to look ahead
 * got: number of tokesn got, less than len iff end of file
 */
tscfg_rc ts_parse_peek(ts_parse_state *state, ts_tok *toks,
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
    bool eof;
    rc = tscfg_read_tok(&state->in, &state->toks[state->ntoks], &eof);
    TSCFG_CHECK(rc);

    if (eof) {
      break;
    }

    state->ntoks++;
  }

  *got = state->ntoks < count ? state->ntoks : count;
  memcpy(toks, state->toks, sizeof(toks[0]) * (size_t)*got);
  return TSCFG_OK;
}

/*
 * Check if next token matches.
 */
tscfg_rc ts_parse_next_matches(ts_parse_state *state, const char *str,
                               size_t len, bool *match) {
  int got;
  ts_tok tok;
  tscfg_rc rc = ts_parse_peek(state, &tok, 1, &got);
  TSCFG_CHECK(rc);

  if (got == 0) {
    *match = false;
  } else if (len == tok.length &&
          memcmp(tok.str, match, len) != 0) {
    *match = false;
  } else {
    *match = true;
  }

  return TSCFG_OK;
}

/*
 * Remove leading tokens from input.
 */
static tscfg_rc pop_toks(ts_parse_state *state, int count) {
  // TODO: implement
  assert(count >= 0);

  if (count > state->ntoks) {
    tscfg_report_err("Popping more tokens than present");
    return TSCFG_ERR_ARG;
  }

  memmove(&state->toks[0], &state->toks[count], state->ntoks - count);
  state->ntoks -= count;
  return TSCFG_OK;
}

/*
 * Put an error message in the appropriate place and return
 * code.
 */
void tscfg_report_err(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  tscfg_report_err_v(fmt, args);
  va_end(args);
}

void tscfg_report_err_v(const char *fmt, va_list args) {
  // TODO: more sophisticated logging facilities.
  vfprintf(stderr, fmt, args);
  fputc('\n', stderr);
}
