/*
 * Basic test program for HOCON properties parser
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 9 July 2014
 * All rights reserved.
 */

#include <assert.h>
#include <stdio.h>

#include "tsconfig.h"

int main(int argc, const char **argv) {

  assert(argc == 1);
  // TODO: cmdline args

  tscfg_rc rc;
  tsconfig_tree cfg;
  tsconfig_input config_in;
  config_in.kind = TS_CONFIG_IN_FILE;
  config_in.data.f = stdin;

  rc = tsconfig_parse_tree(config_in, TSCFG_HOCON, &cfg);
  if (rc != TSCFG_OK) {
    fprintf(stderr, "Error during parsing\n");
    return 1;
  }

  fprintf(stderr, "Success!\n");
  return 0;
}
