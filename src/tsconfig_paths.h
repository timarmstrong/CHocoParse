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
 * Created: 6 November 2014
 */

/*
 * Path manipulation functions.
 */


#ifndef __TSCONFIG_PATHS_H
#define __TSCONFIG_PATHS_H

#include "tsconfig_tok.h"

/*
 * Convert an array of parser tokens from a value concatenation
 * into a path expression.  The parser tokens will be of varied types,
 * including quoted/unquoted strings, numbers, etc.  The path expression
 * will consist entirely of string tokens, with each token a path element.
 * We parse according to HOCON rules that have . as a path separator.
 *
 * toks: input array with parser tokens.
 * path: output array, initialised by this function.
 *      Path expression elements are appended to this array 
 *
 * Caller is responsible for freeing both arrays.
 */
tscfg_rc tscfg_path_parse(tscfg_tok_array *toks, tscfg_tok_array *path);

#endif // __TSCONFIG_PATHS_H
