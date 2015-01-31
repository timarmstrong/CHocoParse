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
 *
 * If the reader returns an error, this returns TSCFG_ERR_READER
 */
tscfg_rc tsconfig_parse(tsconfig_input in, tscfg_fmt fmt,
      tscfg_reader reader, void *reader_state);

#endif // __TSCONFIG_H
