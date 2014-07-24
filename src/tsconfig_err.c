/*
 * Error handling utilities.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 9 July 2014
 * All rights reserved.
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
