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
 * Lexer for HOCON properties file.
 */

#ifndef __TSCONFIG_LEX_H
#define __TSCONFIG_LEX_H

#include <stdbool.h>

#include "tsconfig.h"
#include "tsconfig_tok.h"

typedef struct {
  bool include_ws_str : 1; // Include whitespace string
  bool include_comm_str : 1; // Include comment strings
} tscfg_lex_opts;

/*
 * Lexing state.
 */
typedef struct {
  // Raw input
  tsconfig_input in;

  // Buffer for lookahead
  unsigned char *buf;
  size_t buf_size; // Allocated size in bytes
  size_t buf_pos; // Position in buffer
  size_t buf_len; // Length of valid bytes beyond buf_pos

  int line;
  int line_char;
} tscfg_lex_state;

tscfg_rc tscfg_lex_init(tscfg_lex_state *lex, tsconfig_input in);
void tscfg_lex_finalize(tscfg_lex_state *lex);

/*
  Read the next token from the input stream.

  tok: unless error, filled in with next token.  Special tag used for EOF.
 */
tscfg_rc tscfg_read_tok(tscfg_lex_state *lex, tscfg_tok *tok,
                        tscfg_lex_opts opts);

/*
 * Take ownership of string from token.
 */
static inline char *tscfg_own_token(tscfg_tok *tok, size_t *len) {
  char *str = tok->str;
  *len = tok->len;

  tok->str = NULL;
  tok->len = 0;
  tok->tag = TSCFG_TOK_INVALID;

  return str;
}

#endif // __TSCONFIG_LEX_H
