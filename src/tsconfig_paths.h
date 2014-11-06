/*
 * Path manipulation functions.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 6 November 2014
 * All rights reserved.
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
