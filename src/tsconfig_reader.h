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
 * Created: 14 July 2014
 */

/*
 * An interface for processing parser events as they happen.
 *
 * All functions have bool return value.  Returning false will halt parsing
 * and cause the parser to return TSCFG_ERR_READER.
 *
 * All functions are passed a void *s pointer for any state.
 *
 * Ownership of memory associated with tokens and any non-const strings
 * is passed to the reader, and must be freed even if not used.
 *
 * TODO: document error handling
 */

#ifndef __TSCONFIG_READER_H
#define __TSCONFIG_READER_H

#include <stdbool.h>

#include "tsconfig_tok.h"

typedef struct {

  /* Start of new object in current context */
  bool (*obj_start)(void *s);

  /* End of current object */
  bool (*obj_end)(void *s);

  /* Start of new array in current context */
  bool (*arr_start)(void *s);

  /* End of current array */
  bool (*arr_end)(void *s);

  /*
   * Start key/value pair in an object.  This is followed by some number
   * of tokens, arrays, and objects.
   * key_toks: these can be true/false/null, quoted/unquoted strings,
   *   numbers, and whitespace tokens. Whitespace is never the first
   *   or last token.  Ownership of array is passed in
   */
  bool (*key_val_start)(void *s, tscfg_tok *key_toks, int nkey_toks,
                         tscfg_tok_tag sep);

  /* End of current key-value pair */
  bool (*key_val_end)(void *s);

  /*
   * Start an array value element.  This is followed by the same
   * tokens that are valid after key_val_start().
   */
  bool (*val_start)(void *s);

  /*
   * End an array value element.
   */
  bool (*val_end)(void *s);

  /*
   * A token comprising part of a value.
   * Tokens: true/false/null, quoted/unquoted strings, numbers,
   * whitespace (if between other tokens).
   */
  bool (*token)(void *s, tscfg_tok *tok);

  /*
   * A variable substitution expression.
   * toks: path expression for variable substitution
   * optional: if true, optional substitution, i.e. ${?
   */
  bool (*var_sub)(void *s, tscfg_tok *toks, int ntoks, bool optional);

} tscfg_reader;

#endif // __TSCONFIG_READER_H
