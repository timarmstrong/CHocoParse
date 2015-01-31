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

#include "tsconfig_err.h"

#include <stdio.h>

/*
 * Put an error message in the appropriate place and return
 * code.
 */
void tscfg_report_err(const char *file, int line, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  // TODO: may need va_copy
  tscfg_report_err_v(file, line, fmt, args);
  va_end(args);
}

void tscfg_report_err_v(const char *file, int line, const char *fmt,
                        va_list args) {
  // TODO: more sophisticated logging facilities.
  if (TSCFG_DEBUG) {
    fprintf(TSCFG_ERR_FILE, "%s:%i: ", file, line);
  }
  vfprintf(TSCFG_ERR_FILE, fmt, args);
  fputc('\n', TSCFG_ERR_FILE);
}
