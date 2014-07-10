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


typedef enum {
  TSCFG_TOK_EOF, // Special token for end of file

  TSCFG_TOK_OPEN_BRACE,
  TSCFG_TOK_CLOSE_BRACE,
  TSCFG_TOK_OPEN_PAREN,
  TSCFG_TOK_CLOSE_PAREN,
  TSCFG_TOK_OPEN_SQUARE,
  TSCFG_TOK_CLOSE_SQUARE,
  TSCFG_TOK_COMMA,
  TSCFG_TOK_EQUAL,
  TSCFG_TOK_PLUSEQUAL,
  TSCFG_TOK_COLON,
  TSCFG_TOK_TRUE,
  TSCFG_TOK_FALSE,
  TSCFG_TOK_NULL,
  TSCFG_TOK_NUMBER, // Numeric token, text is stored in str
  TSCFG_TOK_UNQUOTED, // Unquoted text, text is stored in str
  TSCFG_TOK_STRING, // Quoted text, text is stored in str
  TSCFG_TOK_VAR, // Variable name, name store in string
} tscfg_tok_tag;

/*
 * Lexer token
 */
typedef struct {
  tscfg_tok_tag tag;
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

/*
  Read the next token from the input stream.

  tok: unless error, filled in with next token.  Special tag used for EOF.
 */
tscfg_rc tscfg_read_tok(tscfg_lex_state *lex, tscfg_tok *tok);

#endif // __TSCONFIG_LEX_H
