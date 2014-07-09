#include <assert.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  // TODO: data structure
} ts_config;

typedef enum {
  TSCFG_OK,
  TSCFG_ERR_ARG, /* Invalid argument */
  TSCFG_ERR_SYNTAX,
  TSCFG_ERR_UNKNOWN,
  TSCFG_ERR_UNIMPL,
} tscfg_rc;

typedef enum {
  TSCFG_HOCON,
} tscfg_fmt;

typedef struct {
  FILE* in;
} ts_parse_state;

void tscfg_report_err(const char *fmt, ...);
void tscfg_report_err_v(const char *fmt, va_list args);

#define TSCFG_CHECK(rc) { \
  tscfg_rc __rc = (rc);               \
  if (__rc != TSCFG_OK) return __rc; }

#define TSCFG_CHECK_GOTO(rc, label) { \
  if ((rc) != TSCFG_OK) goto label; }

tscfg_rc parse_ts_config(FILE *in, tscfg_fmt fmt, ts_config *cfg);
tscfg_rc parse_hocon(FILE *in, ts_config *cfg);

tscfg_rc ts_parse_state_init(ts_parse_state *state, FILE* in);
void ts_parse_state_finalize(ts_parse_state *state);
void ts_parse_report_err(ts_parse_state *state, const char *fmt, ...);

tscfg_rc parse_hocon_body(ts_parse_state *state, ts_config *cfg);

tscfg_rc ts_parse_peek(ts_parse_state *state, char *buf, int len, int *got);
tscfg_rc ts_parse_next_matches(ts_parse_state *state, const char *str, int len,                                bool *match);

static tscfg_rc eat(ts_parse_state *state, int len);
static tscfg_rc eat_json_whitespace(ts_parse_state *state);
static bool is_json_whitespace(char c);

int main(int argc, const char **argv) {

  assert(argc == 1);
  // TODO: cmdline args
  
  tscfg_rc rc;
  ts_config cfg;
  FILE *config_stream = stdin;
  rc = parse_ts_config(config_stream, TSCFG_HOCON, &cfg);
  if (rc != TSCFG_OK) { 
    fprintf(stderr, "Error during parsing\n");
    return 1;
  }

  fprintf(stderr, "Success!\n");
  return 0;
}

tscfg_rc parse_ts_config(FILE *in, tscfg_fmt fmt, ts_config *cfg) {
  if (fmt == TSCFG_HOCON) {
    return parse_hocon(in, cfg);
  } else {
    tscfg_report_err("Invalid file format code %i", (int)fmt);
    return TSCFG_ERR_ARG;
  }
}

tscfg_rc parse_hocon(FILE *in, ts_config *cfg) {
  // TODO: buffering
  // TODO: init state
  ts_parse_state state;
  tscfg_rc rc = TSCFG_ERR_UNKNOWN;

  rc = ts_parse_state_init(&state, in);
  TSCFG_CHECK_GOTO(rc, cleanup);
  
  // TODO: probably need to do standard lexer/parser design
  rc = eat_json_whitespace(&state);
  TSCFG_CHECK_GOTO(rc, cleanup);

  bool open_brace;
  rc = ts_parse_next_matches(&state, "{", 1, &open_brace);
  TSCFG_CHECK_GOTO(rc, cleanup);

  if (open_brace) {
    rc = eat(&state, 1);
    TSCFG_CHECK_GOTO(rc, cleanup);
  }

  // TODO: body of JSON
  rc = parse_hocon_body(&state, cfg);
  TSCFG_CHECK_GOTO(rc, cleanup);

  if (open_brace) {
    rc = eat_json_whitespace(&state);
    TSCFG_CHECK_GOTO(rc, cleanup);
      
    bool close_brace;
    rc = ts_parse_next_matches(&state, "}", 1, &close_brace);
    TSCFG_CHECK_GOTO(rc, cleanup);
    if (!close_brace) {
      ts_parse_report_err(&state, "Expected close brace to match initial "
                                    "open");
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }

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
  return TSCFG_ERR_UNIMPL;
}

tscfg_rc ts_parse_state_init(ts_parse_state *state, FILE* in) {
  state->in = in;
  // TODO: setup buffers, etc
  return TSCFG_OK;
}

void ts_parse_state_finalize(ts_parse_state *state) {
  // Invalidate structure
  state->in = NULL;
}

void ts_parse_report_err(ts_parse_state *state, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  tscfg_report_err_v(fmt, args);
  va_end(args);
}

/*
 * Peek ahead into input stream
 * len: number of characters to look ahead
 * got: number of characters got, less than len iff end of file
 */
tscfg_rc ts_parse_peek(ts_parse_state *state, char *buf, int len, int *got) {
  // TODO
  // TODO: WILL NEED TO HANDLE COMMENTS
  return TSCFG_ERR_UNIMPL;
}

/*
 * Check if upcoming part of input stream matches string.
 */
tscfg_rc ts_parse_next_matches(ts_parse_state *state, const char *str, int len,                                bool *match) {
  char buf[len];
  int got;
  tscfg_rc rc = ts_parse_peek(state, buf, len, &got);
  TSCFG_CHECK(rc);

  if (got < len) {
    *match = false;
  } else if (memcmp(str, match, (size_t)len) != 0) {
    *match = false;
  } else {
    *match = true;
  }

  return TSCFG_OK;
}

/*
 * Remove leading characters from input.
 */
static tscfg_rc eat(ts_parse_state *state, int len) {
  // TODO: implement
  return TSCFG_ERR_UNIMPL;
}

/*
 * Remove any leading whitespace characters from input.
 */
static tscfg_rc eat_json_whitespace(ts_parse_state *state) {
  while (true) {
    char c;
    int got;
    tscfg_rc rc = ts_parse_peek(state, &c, 1, &got);
    TSCFG_CHECK(rc);

    if (got == 0) {
      // End of file
      break; 
    }

    assert(got == 1);
    if (is_json_whitespace(c)) {
      rc = eat(state, 1);
      TSCFG_CHECK(rc);
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
}
