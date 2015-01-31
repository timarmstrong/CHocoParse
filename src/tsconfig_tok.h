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
 * Created: 11 July 2014
 */

/*
 * Lexer tokens for HOCON properties file.
 */

#ifndef __TSCONFIG_TOK_H
#define __TSCONFIG_TOK_H

#include <stdbool.h>
#include <stddef.h>

#include "tsconfig_common.h"

typedef enum {
  /* Special tokens: don't include string */
  TSCFG_TOK_INVALID = 0, // Special tag for invalid token
  TSCFG_TOK_EOF, // Special token for end of file

  /* Special whitespace tokens: include string */
  TSCFG_TOK_WS, // Whitespace without newline
  TSCFG_TOK_WS_NEWLINE, // Whitespace with newline

  /* Comment: don't include string */
  TSCFG_TOK_COMMENT,

  /* Paired punctuation tokens: don't include string */
  TSCFG_TOK_OPEN_BRACE,
  TSCFG_TOK_CLOSE_BRACE,
  TSCFG_TOK_OPEN_PAREN,
  TSCFG_TOK_CLOSE_PAREN,
  TSCFG_TOK_OPEN_SQUARE,
  TSCFG_TOK_CLOSE_SQUARE,

  /* Punctuation: don't include string */
  TSCFG_TOK_COMMA,
  TSCFG_TOK_EQUAL,
  TSCFG_TOK_PLUSEQUAL,
  TSCFG_TOK_COLON,
 
  /* Variable substitution start: don't include string */
  TSCFG_TOK_OPEN_SUB,
  TSCFG_TOK_OPEN_OPT_SUB,

  /* Keywords: don't include string */
  TSCFG_TOK_TRUE,
  TSCFG_TOK_FALSE,
  TSCFG_TOK_NULL,

  /* Literals and variables: include string */
  TSCFG_TOK_NUMBER, // Numeric token, text is stored in str
  TSCFG_TOK_UNQUOTED, // Unquoted text, text is stored in str
  TSCFG_TOK_STRING, // Quoted text, string contents after escaping, etc
} tscfg_tok_tag;

/*
 * Lexer token
 */
typedef struct {
  tscfg_tok_tag tag;
  /* String, if any.  Null terminated, but may have nulls in string if
     they were in input. */
  char *str;
  size_t len;

  /* Location in file (both start at 1) */
  int line;
  int line_char; // UTF-8 character index in line
} tscfg_tok;

/*
 * Dynamically sized array of tokens
 */
typedef struct {
  tscfg_tok *toks;
  int size;
  int len;
} tscfg_tok_array;

static const tscfg_tok_array TSCFG_EMPTY_TOK_ARRAY = {
  .toks = NULL, .size = 0, .len = 0
};

const char *tscfg_tok_tag_name(tscfg_tok_tag tag);
void tscfg_tok_free(tscfg_tok *tok);

tscfg_rc tscfg_tok_array_expand(tscfg_tok_array *toks, int min_size);
tscfg_rc tscfg_tok_array_append(tscfg_tok_array *arr, tscfg_tok *tok);
tscfg_rc tscfg_tok_array_concat(tscfg_tok_array *dst, tscfg_tok_array *src);
void tscfg_tok_array_free(tscfg_tok_array *toks, bool free_array);

#endif // __TSCONFIG_TOK_H
