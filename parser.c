#include <stdio.h>
#include <assert.h>

typedef struct {

} typesafe_config;

typedef enum {
  TSCFG_OK,
  TSCFG_ERR_UNKNOWN,
} tscfg_rc;

typedef enum {
  TSCFG_HOCON,
} tscfg_fmt;

tscfg_rc tscfg_report_error(tscfg_rc err_code, const char *fmt, ...);

tscfg_rc parse_ts_config(FILE *in, tscfg_fmt fmt, ts_config *cfg);

int main(int argc, const char **argv) {

  assert(argc == 1);
  // TODO: cmdline args
  
  tscfg_rc rc;
  ts_config cfg;
  FILE *config_stream = stdin;
  rc = parse_ts_config(config_stream, TSCFG_HOCON, &cfg);
  if (rc != TSCFG_OK) { 
    fprintf(stderr, "Error during parsing\n");
    return 1;
  }

  fprintf(stderr, "Success!\n");
  return 0;
}

tscfg_rc parse_ts_config(FILE *in, tscfg_fmt fmt, ts_config *cfg) {
  if (fmt == TSCFG_HOCON) {
  return parse_hocon(in, cfg);
  else {
    return tscfg_report_err(TSCFG_ERR_ARG, "Invalid file format code %i",
                            (int)fmt);
  }
}

/*
 * Put an error message in the appropriate place and return
 * code.
 */
tscfg_rc tscfg_report_error(tscfg_rc err_code, const char *fmt, ...)
{
  va_list args;
  va_start(args, format);
  // TODO: more sophisticated logging facilities.
  vfprintf(stderr, fmt, args);
  va_end(args);
  return err_code; 
}
