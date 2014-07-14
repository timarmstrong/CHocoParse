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

#ifndef __TSCONFIG_H
#define __TSCONFIG_H

#include <stdio.h>

#include "tsconfig_common.h"
#include "tsconfig_reader.h"
#include "tsconfig_tree.h"

typedef enum {
  TSCFG_HOCON,
} tscfg_fmt;

/*
 * Tagged union to represent possible input types for parser.
 */
typedef enum {
  TS_CONFIG_IN_NONE,
  TS_CONFIG_IN_FILE,
  TS_CONFIG_IN_STR,
} tsconfig_input_kind;

typedef struct {
  // TODO: tagged enum
  tsconfig_input_kind kind;
  union {
    FILE* f;
    struct {
      const char *str;
      size_t len;
      size_t pos;
    } s;
  } data;
} tsconfig_input;

/*
 * Parse a typesafe config file to a tree using the specified format.
 */
tscfg_rc tsconfig_parse_tree(tsconfig_input in, tscfg_fmt fmt,
                        tsconfig_tree *cfg);

/*
 * Parse a typesafe config file with a custom reader.
 */
tscfg_rc tsconfig_parse(tsconfig_input in, tscfg_fmt fmt,
      tscfg_reader reader, void *reader_state);

#endif // __TSCONFIG_H
