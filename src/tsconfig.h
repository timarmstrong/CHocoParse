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
#include <stdio.h>

typedef struct {
  // TODO: data structure
} ts_config;

typedef enum {
  TSCFG_OK,
  TSCFG_ERR_ARG, /* Invalid argument to function */
  TSCFG_ERR_OOM, /* Out of memory */
  TSCFG_ERR_SYNTAX, /* Invalid syntax in input */
  TSCFG_ERR_UNKNOWN,
  TSCFG_ERR_UNIMPL,
} tscfg_rc;

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
} ts_config_input_kind;

typedef struct {
  // TODO: tagged enum
  ts_config_input_kind kind;
  union {
    FILE* f;
    struct {
      const char *str;
      size_t len;
    } s;
  } data;
} ts_config_input;

/*
 * Parse a typesafe config file using the specified format.
 */
tscfg_rc parse_ts_config(ts_config_input in, tscfg_fmt fmt, ts_config *cfg);

/*
 * Parse a typesafe config file in HOCON format.
 */
tscfg_rc parse_hocon(ts_config_input in, ts_config *cfg);
