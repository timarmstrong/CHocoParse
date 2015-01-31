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
 * Error handling utilities.
 */

#ifndef __TSCONFIG_ERR_H
#define __TSCONFIG_ERR_H

#include <stdarg.h>

// TODO: disable by default
#define TSCFG_DEBUG 1

#define TSCFG_ERR_FILE stderr

#define REPORT_ERR(...) \
  tscfg_report_err(__FILE__, __LINE__, __VA_ARGS__)

#ifdef TSCFG_DEBUG
#define PRINT_ERR_TRACE() REPORT_ERR("Check failed");
#else
#define PRINT_ERR_TRACE()
#endif

void tscfg_report_err(const char *file, int line, const char *fmt, ...);
void tscfg_report_err_v(const char *file, int line, const char *fmt,
                        va_list args);

#define TSCFG_CHECK(rc) { \
  tscfg_rc __rc = (rc);               \
  if (__rc != TSCFG_OK) {             \
    PRINT_ERR_TRACE();                \
    return __rc;                      \
  }}

#define TSCFG_CHECK_GOTO(rc, label) { \
  if ((rc) != TSCFG_OK) {             \
    PRINT_ERR_TRACE();                \
    goto label; }}

#define TSCFG_CHECK_MALLOC(ptr) { \
  if ((ptr) == NULL) {            \
    PRINT_ERR_TRACE();            \
    return TSCFG_ERR_OOM; }}

#define TSCFG_CHECK_MALLOC_GOTO(ptr, label, rc_var) { \
  if ((ptr) == NULL) {                \
    PRINT_ERR_TRACE();                \
    (rc_var) = TSCFG_ERR_OOM;         \
    goto label; }}

#define TSCFG_COND(cond, err_rc) { \
  if (!(cond)) {                   \
    PRINT_ERR_TRACE();             \
    return (err_rc); }}


#define TSCFG_COND_GOTO(cond, rc_var, rc_val, label) { \
  if (!(cond)) {                   \
    PRINT_ERR_TRACE();             \
    (rc_var) = (rc_val);           \
    goto label; }}

#endif // __TSCONFIG_ERR_H
