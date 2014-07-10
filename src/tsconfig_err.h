/*
 * Error handling utilities.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 9 July 2014
 * All rights reserved.
 */

#ifndef __TSCONFIG_ERR_H
#define __TSCONFIG_ERR_H

#include <stdarg.h>

void tscfg_report_err(const char *fmt, ...);
void tscfg_report_err_v(const char *fmt, va_list args);

#define TSCFG_CHECK(rc) { \
  tscfg_rc __rc = (rc);               \
  if (__rc != TSCFG_OK) return __rc; }

#define TSCFG_CHECK_GOTO(rc, label) { \
  if ((rc) != TSCFG_OK) goto label; }

#define TSCFG_CHECK_MALLOC(ptr) { \
  if ((ptr) == NULL) return TSCFG_ERR_OOM; }

#endif // __TSCONFIG_ERR_H
