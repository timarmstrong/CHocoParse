/*
 * Lexer tokens for HOCON properties file.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 11 July 2014
 * All rights reserved.
 */

#ifndef __TSCONFIG_TOK_H
#define __TSCONFIG_TOK_H

#include <stddef.h>

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

  /* Keywords: don't include string */
  TSCFG_TOK_TRUE,
  TSCFG_TOK_FALSE,
  TSCFG_TOK_NULL,

  /* Literals and variables: include string */
  TSCFG_TOK_NUMBER, // Numeric token, text is stored in str
  TSCFG_TOK_UNQUOTED, // Unquoted text, text is stored in str
  TSCFG_TOK_STRING, // Quoted text, string contents after escaping, etc
  TSCFG_TOK_VAR, // Variable name, name stored in string
} tscfg_tok_tag;

/*
 * Lexer token
 */
typedef struct {
  tscfg_tok_tag tag;
  char *str;
  size_t length;
} tscfg_tok;

#endif // __TSCONFIG_TOK_H
