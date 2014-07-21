/*
 * Common definitions for header files.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 14 July 2014
 * All rights reserved.
 */

#ifndef __TSCONFIG_COMMON_H
#define __TSCONFIG_COMMON_H

typedef enum {
  TSCFG_OK,
  TSCFG_ERR_ARG, /* Invalid argument to function */
  TSCFG_ERR_OOM, /* Out of memory */
  TSCFG_ERR_SYNTAX, /* Invalid syntax in input */
  TSCFG_ERR_INVALID, /* Invalid input */
  TSCFG_ERR_IO, /* I/O error */
  TSCFG_ERR_READER, /* Error caused by reader */
  TSCFG_ERR_UNKNOWN,
  TSCFG_ERR_UNIMPL,
} tscfg_rc;

#endif // __TSCONFIG_COMMON_H
