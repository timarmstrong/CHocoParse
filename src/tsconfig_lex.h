/*
 * Lexer for HOCON properties file.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 9 July 2014
 * All rights reserved.
 */

#ifndef __TSCONFIG_LEX_H
#define __TSCONFIG_LEX_H

#include <stdbool.h>

#include "tsconfig.h"

/*
 * Lexer token
 */
typedef struct {
  char *str;
  size_t length;
} tscfg_tok;

/*
 * Lexing state.
 */
typedef struct {
  // Raw input
  ts_config_input in;

  // Buffer for lookahead
  unsigned char *buf;
  size_t buf_size;
  size_t buf_len;
} tscfg_lex_state;

tscfg_rc tscfg_lex_init(tscfg_lex_state *lex, ts_config_input in);
void tscfg_lex_finalize(tscfg_lex_state *lex);

tscfg_rc tscfg_read_tok(tscfg_lex_state *lex, tscfg_tok *tok, bool *eof);

#endif // __TSCONFIG_LEX_H
