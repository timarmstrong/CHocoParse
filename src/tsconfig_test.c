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
 * Basic test program for HOCON properties parser
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
