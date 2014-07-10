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

typedef struct {
  char *str;
  size_t length;
} ts_tok;

tscfg_rc tscfg_read_tok(ts_config_input *in, ts_tok *tok, bool *eof);

#endif // __TSCONFIG_LEX_H
