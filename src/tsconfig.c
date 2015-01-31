/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright 2015, Tim Armstrong
 *
 * Author: Tim Armstrong <tim.g.armstrong@gmail.com>
 * Created: 9 July 2014
 */

/*
 * C parser for HOCON properties file (Typesafe's configuration format).
 *
 * For HOCON reference: https://github.com/typesafehub/config
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

/*
  Whether an empty value is accepted, e.g.

    ["", ,] would give a two element array of zero-length strings
    { k = } is equivalent to { k = "" }

 */
#define ALLOW_EMPTY_VALUE true

#define PARSE_REPORT_ERR(state, ...) \
  ts_parse_report_err(__FILE__, __LINE__, state, __VA_ARGS__)


#define PARSE_DEBUG 0

#if PARSE_DEBUG == 1
#define DEBUG(fmt, ...) \
  fprintf(stderr, "%s:%i: " fmt "\n",  \
      __FILE__, __LINE__, __VA_ARGS__)
#else
#define DEBUG(fmt, ...)
#endif

typedef struct {
  tscfg_reader reader;
  void *reader_state;

  tscfg_lex_state lex_state;

  tscfg_tok_array toks;
} ts_parse_state;

static tscfg_rc ts_parse_state_init(ts_parse_state *state, tsconfig_input in,
  tscfg_reader reader, void *reader_state);
static void ts_parse_state_finalize(ts_parse_state *state);
static void ts_parse_report_err(const char *file, int line,
              ts_parse_state *state, const char *fmt, ...);

static tscfg_rc parse_hocon(tsconfig_input in, tscfg_reader reader,
                            void *reader_state);
static tscfg_rc parse_hocon_obj_body(ts_parse_state *state);
static tscfg_rc parse_hocon_arr_body(ts_parse_state *state);

static tscfg_rc kv_sep(ts_parse_state *state, tscfg_tok_tag *tag);
static tscfg_rc accum_whitespace(ts_parse_state *state, bool *newline,
                   bool *comment, tscfg_tok_array *ws_toks);

static tscfg_rc key(ts_parse_state *state, tscfg_tok_array *toks);
static tscfg_rc value(ts_parse_state *state);

static tscfg_rc emit_toks(ts_parse_state *state, tscfg_tok_array *toks,
                          bool check_no_comments);

static tscfg_rc expect_tag(ts_parse_state *state, tscfg_tok_tag expected,
                      const char *errmsg_start);

static tscfg_rc peek_tok(ts_parse_state *state, tscfg_tok *toks,
                       int count, int *got);
static tscfg_rc peek_tok_skip_ws(ts_parse_state *state, tscfg_tok *toks,
                       int count, int *got);
static tscfg_rc peek_tok_impl(ts_parse_state *state, tscfg_tok *toks,
                       int count, int *got, bool include_ws);
static tscfg_rc peek_tag(ts_parse_state *state, tscfg_tok_tag *tag);
static tscfg_rc peek_tag_skip_ws(ts_parse_state *state, tscfg_tok_tag *tag);

static void pop_toks(ts_parse_state *state, int count, bool free_toks);
static tscfg_rc pop_append_toks(ts_parse_state *state, tscfg_tok_array *toks,
                                int count);

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
    REPORT_ERR("Invalid file format code %i", (int)fmt);
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
    pop_toks(&state, 1, true);
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
      pop_toks(&state, 1, true);
    } else {
      const char *msg = (open_tag == TSCFG_TOK_OPEN_BRACE) ?
              "Expected closing brace to match initial open" :
              "Expected closing square bracket to match initial open";
      PARSE_REPORT_ERR(&state, msg);
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
    PARSE_REPORT_ERR(&state, "Trailing tokens, starting with: %.*s",
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
    tscfg_tok tok;
    int got;
    rc = peek_tok(state, &tok, 1, &got);
    TSCFG_CHECK(rc);
    if (got == 0 ||
        tok.tag == TSCFG_TOK_CLOSE_BRACE ||
        tok.tag == TSCFG_TOK_EOF) {
      break;
    }
    
    // Include is handled as a special case of an unquoted string
    if (tok.tag == TSCFG_TOK_UNQUOTED &&
        tok.len == 7 && memcmp("include", tok.str, 7) == 0) {
      pop_toks(state, 1, true);

      // TODO: check for quoted string, or file()/url()/classpath()
      PARSE_REPORT_ERR(state, "HOCON includes not yet supported");
      return TSCFG_ERR_UNIMPL;
    } else {
      tscfg_tok_array key_toks;
      rc = key(state, &key_toks);
      TSCFG_CHECK(rc);

      // Separator before value
      tscfg_tok_tag sep;
      rc = kv_sep(state, &sep);
      TSCFG_CHECK(rc);

      ok = state->reader.key_val_start(state->reader_state,
                        key_toks.toks, key_toks.len, sep);
      key_toks = TSCFG_EMPTY_TOK_ARRAY; // Ownership of array passed in
      TSCFG_COND(ok, TSCFG_ERR_READER);

      // Parse value
      rc = value(state);
      TSCFG_CHECK(rc);

      ok = state->reader.key_val_end(state->reader_state);
      TSCFG_COND(ok, TSCFG_ERR_READER);
    }
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
      pop_toks(state, 1, true);
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
      PARSE_REPORT_ERR(state, "End of input before key/value separator");
      return TSCFG_ERR_SYNTAX;
    default:
      PARSE_REPORT_ERR(state, "Expected key/value separator or open brace, "
                       "but got token: %s", tscfg_tok_tag_name(maybe_tag));
      return TSCFG_ERR_SYNTAX;
  }

}

/*
 * Handle case where we could have a value concatenation or item separator.
 *
 * Consumes all whitespace tokens, accumulating into buffer
 *
 * newline: whether a newline was consumed
 * comment: whether a comment was consumed
 * ws_toks: array of whitespace/separator tokens consumed
 * next_tok: next token
 */
static tscfg_rc accum_whitespace(ts_parse_state *state, bool *newline,
                   bool *comment, tscfg_tok_array *ws_toks) {
  tscfg_rc rc;
  *newline = false;
  *comment = false;

  *ws_toks = TSCFG_EMPTY_TOK_ARRAY;

  while (true) {
    tscfg_tok tok;

    int got;
    rc = peek_tok(state, &tok, 1, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got == 0) {
      break;
    }

    if (tok.tag == TSCFG_TOK_WS) {
      // Regular whitespace
    } else if (tok.tag == TSCFG_TOK_WS_NEWLINE) {
      *newline = true;
    } else if(tok.tag == TSCFG_TOK_COMMENT) {
      *comment = true;
    } else {
      break;
    }

    rc = pop_append_toks(state, ws_toks, 1);
    TSCFG_CHECK_GOTO(rc, cleanup);
  }

  rc = TSCFG_OK;
cleanup:
  if (rc != TSCFG_OK) {
    tscfg_tok_array_free(ws_toks, true);
  }
  return rc;
}

/*
 * Parse a key comprised of 0 or more tokens.
 * Return as soon as it finds something not a valid part of key.
 * May return 0 tokens.
 * Will consume key and leading/trailing whitespace
 */
static tscfg_rc key(ts_parse_state *state, tscfg_tok_array *toks) {
  tscfg_rc rc;

  *toks = TSCFG_EMPTY_TOK_ARRAY;

  rc = skip_whitespace(state, NULL);
  TSCFG_CHECK(rc);

  /* Need to track whitespace tokens in case of concatenation.
   * This array tracks whitespace tokens preceding current token. */
  tscfg_tok_array ws_toks = TSCFG_EMPTY_TOK_ARRAY;

  bool newline = false, comment = false;

  // Loop until end of value
  // TODO: how to handle '.' path separators
  while (true) {
    tscfg_tok tok;
    int got;
    rc = peek_tok(state, &tok, 1, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got == 0) {
      // No more elements possible
      return TSCFG_OK;
    }

    // Check for value element
    switch (tok.tag) {
      case TSCFG_TOK_TRUE:
      case TSCFG_TOK_FALSE:
      case TSCFG_TOK_NULL:
      case TSCFG_TOK_NUMBER:
      case TSCFG_TOK_UNQUOTED:
      case TSCFG_TOK_STRING:
        // Plain tokens with or without string
        if (comment) {
          PARSE_REPORT_ERR(state, "Comments not allowed in key");
          rc = TSCFG_ERR_SYNTAX;
          goto cleanup;
        }

        rc = tscfg_tok_array_concat(toks, &ws_toks);
        TSCFG_CHECK_GOTO(rc, cleanup);

        rc = pop_append_toks(state, toks, 1);
        TSCFG_CHECK_GOTO(rc, cleanup);
        break;
      default:
        // Something not part of key
        rc = TSCFG_OK;
        goto cleanup;
    }

    rc = accum_whitespace(state, &newline, &comment, &ws_toks);
    TSCFG_CHECK_GOTO(rc, cleanup);
  }

  rc = TSCFG_OK;

cleanup:
  tscfg_tok_array_free(&ws_toks, true);
  return rc;
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
  bool ok;

  rc = skip_whitespace(state, NULL);
  TSCFG_CHECK(rc);

  /* Need to track whitespace tokens in case of concatenation.
   * This array tracks whitespace tokens preceding current token. */
  tscfg_tok_array ws_toks = TSCFG_EMPTY_TOK_ARRAY;

  bool first = true;
  bool newline = false;

  // Loop until end of value
  while (true) {
    tscfg_tok tok;
    int got;
    rc = peek_tok(state, &tok, 1, &got);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (got == 0) {
      // No more elements possible
      break;
    }

    // Check for value element
    switch (tok.tag) {
      case TSCFG_TOK_TRUE:
      case TSCFG_TOK_FALSE:
      case TSCFG_TOK_NULL:
      case TSCFG_TOK_NUMBER:
      case TSCFG_TOK_UNQUOTED:
      case TSCFG_TOK_STRING:
        rc = emit_toks(state, &ws_toks, true);
        TSCFG_CHECK_GOTO(rc, cleanup);

        pop_toks(state, 1, false);

        ok = state->reader.token(state->reader_state, &tok);
        TSCFG_COND_GOTO(ok, rc, TSCFG_ERR_READER, cleanup);
        break;

      case TSCFG_TOK_OPEN_SUB:
      case TSCFG_TOK_OPEN_OPT_SUB: {
        rc = emit_toks(state, &ws_toks, true);
        TSCFG_CHECK_GOTO(rc, cleanup);

        pop_toks(state, 1, false);
        
        tscfg_tok_array path_toks;
        rc = key(state, &path_toks);
        TSCFG_CHECK_GOTO(rc, cleanup);

        bool option = (tok.tag == TSCFG_TOK_OPEN_OPT_SUB);

        ok = state->reader.var_sub(state->reader_state, path_toks.toks,
                                   path_toks.len, option);
        path_toks = TSCFG_EMPTY_TOK_ARRAY; // Ownership of array passed in
        TSCFG_COND_GOTO(ok, rc, TSCFG_ERR_READER, cleanup);

        rc = expect_tag(state, TSCFG_TOK_CLOSE_BRACE,
                        "Expected close brace for substitution");
        TSCFG_CHECK_GOTO(rc, cleanup);

        pop_toks(state, 1, true);
        break;
      }

      case TSCFG_TOK_OPEN_BRACE:
        rc = emit_toks(state, &ws_toks, true);
        TSCFG_CHECK_GOTO(rc, cleanup);

        pop_toks(state, 1, true);

        rc = parse_hocon_obj_body(state);
        TSCFG_CHECK(rc);

        rc = expect_tag(state, TSCFG_TOK_CLOSE_BRACE,
                        "Expected close brace");
        TSCFG_CHECK_GOTO(rc, cleanup);

        pop_toks(state, 1, true);
        break;

      case TSCFG_TOK_OPEN_SQUARE:
        rc = emit_toks(state, &ws_toks, true);
        TSCFG_CHECK_GOTO(rc, cleanup);

        pop_toks(state, 1, true);

        rc = parse_hocon_arr_body(state);
        TSCFG_CHECK(rc);

        rc = expect_tag(state, TSCFG_TOK_CLOSE_SQUARE,
                        "Expected close square bracket");
        TSCFG_CHECK_GOTO(rc, cleanup);

        pop_toks(state, 1, true);
        break;

      case TSCFG_TOK_COMMA:
        assert(first); // Should only happen on first iteration
        if (ALLOW_EMPTY_VALUE)  {
          /* Don't emit anything, keep comma as next token*/
          tscfg_tok_array_free(&ws_toks, false);
        } else {
          PARSE_REPORT_ERR(state, "Empty values are not valid syntax");
          rc = TSCFG_ERR_SYNTAX;
          goto cleanup;
        }

      default:
        // Token cannot be part of value, leave
        rc = TSCFG_OK;
        goto cleanup;
    }

    bool comment;
    rc = accum_whitespace(state, &newline, &comment, &ws_toks);
    TSCFG_CHECK_GOTO(rc, cleanup);

    tscfg_tok_tag next_tag;
    rc = peek_tag(state, &next_tag);
    TSCFG_CHECK_GOTO(rc, cleanup);

    if (next_tag == TSCFG_TOK_COMMA) {
      pop_toks(state, 1, true); // Remove comma
      // Explicit separator: ready for next item
      break;
    } else if (newline) {
      // Implicit separator: ready for next item
      break;
    }

    first = false;
  }

  rc = TSCFG_OK;

cleanup:
  tscfg_tok_array_free(&ws_toks, true);
  return rc;
}

/*
 * Emit tokens and clear them from array
 *
 * check_no_comments: if true, no comments allowed
 */
static tscfg_rc emit_toks(ts_parse_state *state, tscfg_tok_array *toks,
                          bool check_no_comments) {
  for (int i = 0; i < toks->len; i++) {
    tscfg_tok *tok = &toks->toks[i];

    if (check_no_comments && tok->tag == TSCFG_TOK_COMMENT) {
      PARSE_REPORT_ERR(state, "Comments not allowed between tokens here");
      return TSCFG_ERR_SYNTAX;
    }

    bool ok = state->reader.token(state->reader_state, tok);

    // We handed over token, so invalidate
    tok->tag = TSCFG_TOK_INVALID;
    tok->str = NULL;
    tok->len = 0;

    TSCFG_COND(ok, TSCFG_ERR_READER);
  }

  toks->len = 0; // No longer own tokens
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
    REPORT_ERR("Reader has NULL function");
    return TSCFG_ERR_ARG;
  }

  state->reader = reader;
  state->reader_state = reader_state;

  rc = tscfg_lex_init(&state->lex_state, in);
  TSCFG_CHECK(rc);

  state->toks = TSCFG_EMPTY_TOK_ARRAY;

  return TSCFG_OK;
}

static void ts_parse_state_finalize(ts_parse_state *state) {
  tscfg_lex_finalize(&state->lex_state);

  // Free memory
  tscfg_tok_array_free(&state->toks, true);
}

static void ts_parse_report_err(const char *file, int line,
              ts_parse_state *state, const char *fmt, ...) {
  // TODO: include context in errors.
  va_list args;
  va_start(args, fmt);
  tscfg_report_err_v(file, line, fmt, args);
  va_end(args);
}

static tscfg_rc expect_tag(ts_parse_state *state, tscfg_tok_tag expected,
                      const char *errmsg_start) {
  tscfg_rc rc;
  tscfg_tok_tag tag;

  rc = peek_tag(state, &tag);
  TSCFG_CHECK(rc);

  if (tag != expected) {
    // TODO: report actual tag
    PARSE_REPORT_ERR(state, "%s. Next token is", errmsg_start,
                    tscfg_tok_tag_name(tag));
    return TSCFG_ERR_SYNTAX;
  }

  return TSCFG_OK;
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
  rc = tscfg_tok_array_expand(&state->toks, count);
  TSCFG_CHECK(rc);

  while (state->toks.len < count) {
    if (state->toks.len > 0 &&
        state->toks.toks[state->toks.len - 1].tag == TSCFG_TOK_EOF) {
      // Already hit end of file
      break;
    }

    tscfg_lex_opts opts = { .include_ws_str = include_ws,
                            .include_comm_str = false };
    rc = tscfg_read_tok(&state->lex_state,
          &state->toks.toks[state->toks.len], opts);
    TSCFG_CHECK(rc);

    state->toks.len++;
  }

  *got = state->toks.len < count ? state->toks.len : count;
  memcpy(toks, state->toks.toks, sizeof(toks[0]) * (size_t)*got);
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
 * Remove leading tokens from input.
 *
 * Caller must ensure that there are at least count tokens.
 *
 * free_toks: if true, free any strings not set to NULL.
 */
static void pop_toks(ts_parse_state *state, int count, bool free_toks) {
  assert(count >= 0);
  assert(count <= state->toks.len);

  for (int i = 0; i < count; i++) {
    tscfg_tok *tok = &state->toks.toks[i];
    DEBUG("pop_toks: tok %i is %s(%.*s)", i, tscfg_tok_tag_name(tok->tag),
          (int)tok->len, tok->str);
  }

  // Cleanup memory first
  if (free_toks) {
    for (int i = 0; i < count; i++) {
      tscfg_tok_free(&state->toks.toks[i]);
    }
  }

  memmove(&state->toks.toks[0], &state->toks.toks[count],
          sizeof(state->toks.toks[0]) * (size_t)(state->toks.len - count));
  state->toks.len -= count;
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
      pop_toks(state, 1, true);

      if (newline != NULL &&
          tok.tag == TSCFG_TOK_WS_NEWLINE) {
        *newline = true;
      }
    } else {
      return TSCFG_OK;
    }
  }
}

static tscfg_rc pop_append_toks(ts_parse_state *state, tscfg_tok_array *toks,
                                int count) {
  tscfg_rc rc;
  assert(state->toks.len >= count);

  if (toks->len == toks->size) {
    rc = tscfg_tok_array_expand(toks, toks->len + count);
    TSCFG_CHECK(rc);
  }

  for (int i = 0; i < count; i++) {
    toks->toks[toks->len++] = state->toks.toks[i];
  }

  pop_toks(state, count, false);
  return TSCFG_OK;
}
