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
 *
 * Created: 27 July 2014
 */

/*
 * Lexer tokens for HOCON properties file.
 */

#include "tsconfig_tok.h"

#include "tsconfig_err.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

const char *tscfg_tok_tag_name(tscfg_tok_tag tag) {
  switch (tag) {
    case TSCFG_TOK_INVALID:
      return "TOK_INVALID";
    case TSCFG_TOK_EOF:
      return "TOK_EOF";
    case TSCFG_TOK_WS:
      return "TOK_WS";
    case TSCFG_TOK_WS_NEWLINE:
      return "TOK_WS_NEWLINE";
    case TSCFG_TOK_COMMENT:
      return "TOK_COMMENT";
    case TSCFG_TOK_OPEN_BRACE:
      return "TOK_OPEN_BRACE";
    case TSCFG_TOK_CLOSE_BRACE:
      return "TOK_CLOSE_BRACE";
    case TSCFG_TOK_OPEN_PAREN:
      return "TOK_OPEN_PAREN";
    case TSCFG_TOK_CLOSE_PAREN:
      return "TOK_CLOSE_PAREN";
    case TSCFG_TOK_OPEN_SQUARE:
      return "TOK_OPEN_SQUARE";
    case TSCFG_TOK_CLOSE_SQUARE:
      return "TOK_CLOSE_SQUARE";
    case TSCFG_TOK_COMMA:
      return "TOK_COMMA";
    case TSCFG_TOK_EQUAL:
      return "TOK_EQUAL";
    case TSCFG_TOK_PLUSEQUAL:
      return "TOK_PLUSEQUAL";
    case TSCFG_TOK_COLON:
      return "TOK_COLON";
    case TSCFG_TOK_TRUE:
      return "TOK_TRUE";
    case TSCFG_TOK_FALSE:
      return "TOK_FALSE";
    case TSCFG_TOK_NULL:
      return "TOK_NULL";
    case TSCFG_TOK_NUMBER:
      return "TOK_NUMBER";
    case TSCFG_TOK_UNQUOTED:
      return "TOK_UNQUOTED";
    case TSCFG_TOK_STRING:
      return "TOK_STRING";
    case TSCFG_TOK_OPEN_SUB:
      return "TOK_OPEN_SUB";
    case TSCFG_TOK_OPEN_OPT_SUB:
      return "TOK_OPEN_OPT_SUB";
    default:
      return "(unknown)";
  }
}

void tscfg_tok_free(tscfg_tok *tok) {
  if (tok->str != NULL) {
    free(tok->str);
  }
  tok->tag = TSCFG_TOK_INVALID;
  tok->str = NULL;
  tok->len = 0;
}

tscfg_rc tscfg_tok_array_expand(tscfg_tok_array *toks, int min_size) {
  if (min_size > toks->size) {
    int new_size = toks->size * 2;
    if (new_size < min_size) {
      new_size = min_size;
    }

    void *tmp = realloc(toks->toks, sizeof(toks->toks[0]) * (size_t)new_size);
    TSCFG_CHECK_MALLOC(tmp);

    toks->toks = tmp;
    toks->size = new_size;
  }

  return TSCFG_OK;
}

tscfg_rc tscfg_tok_array_append(tscfg_tok_array *arr, tscfg_tok *tok) {
  assert(arr != NULL);
  assert(tok != NULL);
  tscfg_rc rc = tscfg_tok_array_expand(arr, arr->len + 1);
  TSCFG_CHECK(rc);
  
  arr->toks[arr->len] = *tok;
  arr->len++;

  tok->tag = TSCFG_TOK_INVALID;
  tok->str = NULL;
  tok->len = 0;
}

/*
 * Append tokens from src to dst and clear src
 */
tscfg_rc tscfg_tok_array_concat(tscfg_tok_array *dst, tscfg_tok_array *src) {
  assert(dst != src);
  tscfg_rc rc = tscfg_tok_array_expand(dst, dst->len + src->len);
  TSCFG_CHECK(rc);

  memcpy(&dst->toks[dst->len], &src->toks[0],
         sizeof(src->toks[0]) * (size_t)src->len);

  dst->len += src->len;
  src->len = 0;
  return TSCFG_OK;
}

/*
 * Free strings of any tokens.
 */
void tscfg_tok_array_free(tscfg_tok_array *toks, bool free_array) {
  for (int i = 0; i < toks->len; i++) {
    tscfg_tok_free(&toks->toks[i]);
  }
  toks->len = 0;

  if (free_array && toks->toks != NULL) {
    free(toks->toks);
    toks->toks = NULL;
    toks->size = 0;
  }
}
