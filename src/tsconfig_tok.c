/*
 * Lexer tokens for HOCON properties file.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 27 July 2014
 * All rights reserved.
 */

#include "tsconfig_tok.h"

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
    case TSCFG_TOK_INCLUDE:
      return "TOK_INCLUDE";
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
