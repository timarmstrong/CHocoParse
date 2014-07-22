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
#include "tsconfig_tree_reader.h"

typedef struct {
  tscfg_reader reader;
  void *reader_state;

  tscfg_lex_state lex_state;

  tscfg_tok *toks;
  int toks_size;
  int ntoks;
} ts_parse_state;

static tscfg_rc ts_parse_state_init(ts_parse_state *state, tsconfig_input in,
  tscfg_reader reader, void *reader_state);
static void ts_parse_state_finalize(ts_parse_state *state);
static void ts_parse_report_err(ts_parse_state *state, const char *fmt, ...);

static tscfg_rc parse_hocon(tsconfig_input in, tscfg_reader reader,
                            void *reader_state);
static tscfg_rc parse_hocon_obj_body(ts_parse_state *state);
static tscfg_rc parse_hocon_arr_body(ts_parse_state *state);

static tscfg_rc kv_sep(ts_parse_state *state, tscfg_tok_tag *tag);
static tscfg_rc concat_or_item_sep(ts_parse_state *state, bool *saw_sep,
                   bool *saw_comment, tscfg_tok **ws_toks, int *ws_tok_count,
                   tscfg_tok *next_tok);

static tscfg_rc key(ts_parse_state *state, tscfg_tok **toks, int *tok_count);
static tscfg_rc value(ts_parse_state *state);

static tscfg_rc peek_tok(ts_parse_state *state, tscfg_tok *toks,
                       int count, int *got);
static tscfg_rc peek_tok_skip_ws(ts_parse_state *state, tscfg_tok *toks,
                       int count, int *got);
static tscfg_rc peek_tok_impl(ts_parse_state *state, tscfg_tok *toks,
                       int count, int *got, bool include_ws);
static tscfg_rc peek_tag(ts_parse_state *state, tscfg_tok_tag *tag);
static tscfg_rc peek_tag_skip_ws(ts_parse_state *state, tscfg_tok_tag *tag);

static void free_toks(tscfg_tok *toks, int count);
static void pop_toks(ts_parse_state *state, int count);
static char *pop_tok_str(ts_parse_state *state, size_t *len);

static tscfg_rc skip_whitespace(ts_parse_state *state, bool *newline);

tscfg_rc tsconfig_parse_tree(tsconfig_input in, tscfg_fmt fmt,
                              tsconfig_tree *cfg) {
  tscfg_reader reader;
  tscfg_treeread_state *reader_state;
  tscfg_rc rc = tscfg_tree_reader_init(&reader, &reader_state);
  TSCFG_CHECK(rc);

  rc = tsconfig_parse(in, fmt, reader, (void*)reader_state);
  TSCFG_CHECK(rc);

  tsconfig_tree tree;
  rc = tscfg_tree_reader_done(reader_state, &tree);
  TSCFG_CHECK(rc);

  // TODO: additional processing to merge values, etc

  *cfg = tree;
  return TSCFG_OK;
}


tscfg_rc tsconfig_parse(tsconfig_input in, tscfg_fmt fmt,
      tscfg_reader reader, void *reader_state) {
  if (fmt == TSCFG_HOCON) {
    return parse_hocon(in, reader, reader_state);
  } else {
    tscfg_report_err("Invalid file format code %i", (int)fmt);
    return TSCFG_ERR_ARG;
  }
}

static tscfg_rc parse_hocon(tsconfig_input in, tscfg_reader reader,
    void *reader_state) {
  ts_parse_state state;
  tscfg_rc rc = TSCFG_ERR_UNKNOWN;

  rc = ts_parse_state_init(&state, in, reader, reader_state);
  TSCFG_CHECK_GOTO(rc, cleanup);

  tscfg_tok_tag open_tag; // E.g. open brace

  rc = peek_tag_skip_ws(&state, &open_tag);
  TSCFG_CHECK_GOTO(rc, cleanup);

  if (open_tag == TSCFG_TOK_OPEN_BRACE ||
      open_tag == TSCFG_TOK_OPEN_SQUARE) {
    pop_toks(&state, 1);
  } else {
    // No initial punctuation
    open_tag = TSCFG_TOK_INVALID;
  }

  if (open_tag == TSCFG_TOK_OPEN_SQUARE) {
    // Array
    rc = parse_hocon_arr_body(&state);
    TSCFG_CHECK_GOTO(rc, cleanup);
  } else {
    // Explicit or implicit object
    rc = parse_hocon_obj_body(&state);
    TSCFG_CHECK_GOTO(rc, cleanup);
  }

  if (open_tag != TSCFG_TOK_INVALID) {
    tscfg_tok_tag close_tag;

    // Whitespace should be all consumed before here
    rc = peek_tag(&state, &close_tag);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if ((open_tag == TSCFG_TOK_OPEN_BRACE &&
          close_tag == TSCFG_TOK_CLOSE_BRACE) ||
        (open_tag == TSCFG_TOK_OPEN_SQUARE &&
          close_tag == TSCFG_TOK_CLOSE_SQUARE)) {
      pop_toks(&state, 1);
    } else {
      const char *msg = (open_tag == TSCFG_TOK_OPEN_BRACE) ?
              "Expected closing brace to match initial open" :
              "Expected closing square bracket to match initial open";
      ts_parse_report_err(&state, msg);
      rc = TSCFG_ERR_SYNTAX;
      goto cleanup;
    }
  }

  tscfg_tok tok;
  int got;
  rc = peek_tok_skip_ws(&state, &tok, 1, &got);
  assert(got == 1); // Should get at least end of file
  if (tok.tag != TSCFG_TOK_EOF) {
    // TODO: include token tag
    ts_parse_report_err(&state, "Trailing tokens, starting with: %.*s",
                             (int)tok.len, tok.str);
  }

  rc = TSCFG_OK;
cleanup:
  ts_parse_state_finalize(&state);
  return rc;
}

/*
 * Parse object body, returning when we hit } or EOF
 */
static tscfg_rc parse_hocon_obj_body(ts_parse_state *state) {
  tscfg_rc rc;
  bool ok;

  rc = skip_whitespace(state, NULL);
  TSCFG_CHECK(rc);

  ok = state->reader.obj_start(state->reader_state);
  TSCFG_COND(ok, TSCFG_ERR_READER);

  while (true) {
    // Check for close brace or EOF.
    // Whitespace should already be consumed.
    tscfg_tok_tag tag;
    rc = peek_tag(state, &tag);
    TSCFG_CHECK(rc);
    if (tag == TSCFG_TOK_CLOSE_BRACE ||
        tag == TSCFG_TOK_EOF) {
      break;
    }

    // TODO: check for include file and merge object

    tscfg_tok *key_toks;
    int nkey_toks;
    rc = key(state, &key_toks, &nkey_toks);
    TSCFG_CHECK(rc);

    // Separator before value
    tscfg_tok_tag sep;
    rc = kv_sep(state, &sep);
    TSCFG_CHECK(rc);

    ok = state->reader.key_val_start(state->reader_state,
                                    key_toks, nkey_toks, sep);
    TSCFG_COND(ok, TSCFG_ERR_READER);

    // Parse value
    rc = value(state);
    TSCFG_CHECK(rc);

    ok = state->reader.key_val_end(state->reader_state);
    TSCFG_COND(ok, TSCFG_ERR_READER);

  }

  ok = state->reader.obj_end(state->reader_state);
  TSCFG_COND(ok, TSCFG_ERR_READER);
  return TSCFG_OK;
}

/*
 * Parse array body, returning when we hit } or EOF
 */
static tscfg_rc parse_hocon_arr_body(ts_parse_state *state) {
  tscfg_rc rc;
  bool ok;

  rc = skip_whitespace(state, NULL);
  TSCFG_CHECK(rc);

  ok = state->reader.arr_start(state->reader_state);
  TSCFG_COND(ok, TSCFG_ERR_READER);

  while (true) {
    // Check for close square bracket or EOF.
    // Whitespace should already be consumed.
    tscfg_tok_tag tag;
    rc = peek_tag(state, &tag);
    TSCFG_CHECK(rc);
    if (tag == TSCFG_TOK_CLOSE_SQUARE ||
        tag == TSCFG_TOK_EOF) {
      break;
    }

    ok = state->reader.val_start(state->reader_state);
    TSCFG_COND(ok, TSCFG_ERR_READER);

    // Parse value
    rc = value(state);
    TSCFG_CHECK(rc);

    ok = state->reader.val_end(state->reader_state);
    TSCFG_COND(ok, TSCFG_ERR_READER);

  }

  ok = state->reader.arr_end(state->reader_state);
  TSCFG_COND(ok, TSCFG_ERR_READER);
  return TSCFG_OK;
}

/*
 * Look for key/value separator.
 *
 * separators: ':', '=', implied before  open brace
 * Consume tokens unless the separator.  Skips whitespace.
 *
 * Fails if key/value separator not found.
 */
static tscfg_rc kv_sep(ts_parse_state *state, tscfg_tok_tag *tag) {
  tscfg_rc rc;

  tscfg_tok_tag maybe_tag;

  rc = peek_tag_skip_ws(state, &maybe_tag);
  TSCFG_CHECK(rc);

  switch (maybe_tag) {
    case TSCFG_TOK_EQUAL:
    case TSCFG_TOK_COLON:
    case TSCFG_TOK_PLUSEQUAL:
      pop_toks(state, 1);
      if (tag != NULL) {
        *tag = maybe_tag;
      }
      return TSCFG_OK;
    case TSCFG_TOK_OPEN_BRACE:
      if (tag != NULL) {
        *tag = maybe_tag;
      }
      return TSCFG_OK;
    case TSCFG_TOK_EOF:
      tscfg_report_err("End of input before value matching key");
      return TSCFG_ERR_SYNTAX;
    default:
      tscfg_report_err("Expected key/value separator or open brace, but "
                       "got something else.");
      return TSCFG_ERR_SYNTAX;
  }

}

/*
 * Handle case where we could have a value concatenation or item separator.
 *
 * Consumes all whitespace tokens and separators.
 *
 * Syntax error on double separator
 *
 * saw_sep: if saw item separator (newline or comma)
 * saw_comment: if there was a comment token
 * ws_toks/ws_tok_count: array of whitespace/separator tokens consumed
 * next_tok: next token
 */
static tscfg_rc concat_or_item_sep(ts_parse_state *state, bool *saw_sep,
                   bool *saw_comment, tscfg_tok **ws_toks, int *ws_tok_count,
                   tscfg_tok *next_tok) {
  // TODO
  return TSCFG_ERR_UNIMPL;
}

/*
 * Parse a key comprised of at least one token.
 * If there is not a valid key at the current position, return TSCFG_ERR_SYNTAX.
 */
static tscfg_rc key(ts_parse_state *state, tscfg_tok **toks, int *tok_count) {
  // TODO: parse as value concatentation
  *toks = NULL;
  *tok_count = 0;
  return TSCFG_ERR_UNIMPL;
}

/*
 * Parse a value: nested object, array or concatenated tokens.
 * Call the appropriate methods on the reader object as tokens, etc are
 * encountered.
 * If there is not a valid value at the current position,
 * return TSCFG_ERR_SYNTAX.
 */
static tscfg_rc value(ts_parse_state *state) {
  tscfg_rc rc;

  tscfg_tok tok;
  int got;
  rc = peek_tok_skip_ws(state, &tok, 1, &got);
  TSCFG_CHECK(rc);

  // Whether to expect another value
  bool another_val = true;

  if (got == 0 ||
      tok.tag == TSCFG_TOK_CLOSE_BRACE ||
      tok.tag == TSCFG_TOK_EOF) {
    // Handle valid but no element cases
    another_val = false;
  }

  // Loop until end of value
  while (another_val) {
    // Check for value element
    switch (tok.tag) {
      case TSCFG_TOK_OPEN_BRACE:
        state->reader.obj_start(state->reader_state);
        state->reader.obj_end(state->reader_state);
        // TODO: object
        return TSCFG_ERR_UNIMPL;

      case TSCFG_TOK_OPEN_SQUARE:
        state->reader.arr_start(state->reader_state);
        state->reader.arr_end(state->reader_state);
        // TODO: array
        return TSCFG_ERR_UNIMPL;

      case TSCFG_TOK_TRUE:
      case TSCFG_TOK_FALSE:
      case TSCFG_TOK_NULL:
      case TSCFG_TOK_NUMBER:
      case TSCFG_TOK_UNQUOTED:
      case TSCFG_TOK_STRING:
      case TSCFG_TOK_VAR:
        // Plain tokens with or without string
        state->reader.token(state->reader_state, &tok);
        pop_toks(state, 1);
        break;
      default:
        // Attempt to parse value concatenation
        return TSCFG_ERR_UNIMPL;
    }

    // Check for separator: comma or newline
    bool sep = false, comment = false;
    // Need to track whitespace tokens in case they're part of
    // concatenation.
    // TODO: need to conditionally append below
    tscfg_tok *ws_toks;
    int ws_tok_count;

    rc = concat_or_item_sep(state, &sep, &comment, &ws_toks,
                            &ws_tok_count, &tok);
    TSCFG_CHECK(rc);

    if (sep) {
      // Ready for next item
      free_toks(ws_toks, ws_tok_count);
      another_val = false;
    } else if (tok.tag == TSCFG_TOK_CLOSE_BRACE ||
               tok.tag == TSCFG_TOK_EOF) {
      free_toks(ws_toks, ws_tok_count);
      another_val = false;
    } else if (comment) {
      // TODO: I think comments are invalid in value concatenation
      free_toks(ws_toks, ws_tok_count);
      tscfg_report_err("Comments not allowed between tokens here");
      return TSCFG_ERR_SYNTAX;
    } else {
      // Next tok should be part of value
      another_val = true;
    }
  }

  return TSCFG_OK;
}

static tscfg_rc ts_parse_state_init(ts_parse_state *state, tsconfig_input in,
  tscfg_reader reader, void *reader_state) {
  tscfg_rc rc;

  if (reader.obj_start == NULL || reader.obj_end == NULL ||
      reader.arr_start == NULL || reader.arr_end == NULL ||
      reader.key_val_start == NULL || reader.key_val_end == NULL ||
      reader.val_start == NULL || reader.val_end == NULL ||
      reader.token == NULL) {
    tscfg_report_err("Reader has NULL function");
    return TSCFG_ERR_ARG;
  }

  state->reader = reader;
  state->reader_state = reader_state;

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
  bool include_ws = true;
  return peek_tok_impl(state, toks, count, got, include_ws);
}

static tscfg_rc peek_tok_skip_ws(ts_parse_state *state, tscfg_tok *toks,
                       int count, int *got) {
  tscfg_rc rc;
  rc = skip_whitespace(state, NULL);
  TSCFG_CHECK(rc);

  bool include_ws = false;
  return peek_tok_impl(state, toks, count, got, include_ws);
}

static tscfg_rc peek_tok_impl(ts_parse_state *state, tscfg_tok *toks,
                       int count, int *got, bool include_ws) {
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

    tscfg_lex_opts opts = { .include_ws_str = include_ws,
                            .include_comm_str = false };
    rc = tscfg_read_tok(&state->lex_state, &state->toks[state->ntoks], opts);
    TSCFG_CHECK(rc);

    state->ntoks++;
  }

  *got = state->ntoks < count ? state->ntoks : count;
  memcpy(toks, state->toks, sizeof(toks[0]) * (size_t)*got);
  return TSCFG_OK;
}

/*
 * Peek at tag of next token
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

static tscfg_rc peek_tag_skip_ws(ts_parse_state *state, tscfg_tok_tag *tag){
  tscfg_tok tok;
  int got;
  tscfg_rc rc = peek_tok_skip_ws(state, &tok, 1, &got);
  TSCFG_CHECK(rc);

  if (got == 0) {
    *tag = TSCFG_TOK_EOF;
  } else {
    *tag = tok.tag;
  }

  return TSCFG_OK;
}

/*
 * Free strings of any tokens.
 */
static void free_toks(tscfg_tok *toks, int count) {
  for (int i = 0; i < count; i++) {
    tscfg_tok *tok = &toks[i];
    if (tok->str != NULL) {
      free(tok->str);
    }
  }
}

/*
 * Remove leading tokens from input.
 *
 * Caller must ensure that there are at least count tokens.
 *
 * This will free any strings not set to NULL.
 */
static void pop_toks(ts_parse_state *state, int count) {
  assert(count >= 0);
  assert(count <= state->ntoks);

  // Cleanup memory first
  free_toks(state->toks, count);

  memmove(&state->toks[0], &state->toks[count], (size_t)(state->ntoks - count));
  state->ntoks -= count;
}

/*
 * Remove any leading whitespace tokens.
 * newline: if non-null, set to whether saw newline token
 */
static tscfg_rc skip_whitespace(ts_parse_state *state, bool *newline) {
  tscfg_rc rc;

  if (newline != NULL) {
    *newline = false;
  }

  while (true) {
    tscfg_tok tok;
    int got = 0;
    rc = peek_tok_impl(state, &tok, 1, &got, false);
    TSCFG_CHECK(rc);

    if (got == 1 && (
        tok.tag == TSCFG_TOK_WS ||
        tok.tag == TSCFG_TOK_WS_NEWLINE ||
        tok.tag == TSCFG_TOK_COMMENT)) {
      pop_toks(state, 1);

      if (newline != NULL &&
          tok.tag == TSCFG_TOK_WS_NEWLINE) {
        *newline = true;
      }
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
