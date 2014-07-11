

/*
 *
 * All functions have bool return value.  Returning false will halt parsing
 * and cause the parser to return TSCFG_ERR_READER.
 *
 * Ownership of memory associated with tokens and any non-const strings
 * is passed to the reader, and must be freed even if not used.
 */

#include <stdbool.h>

#include "tsconfig_tok.h"

typedef struct {
  /*
   * TODO:
   * - obj_start(void *s);
   * - obj_end(void *s);
   * - arr_start(void *s);
   * - arr_end(void *s);
   * Key/Value start follow by some number of tokens, arrays, and objects
   * Key tokens: true/false/null, quoted/unquoted strings, numbers,
   *  and whitespace (if between other tokens).
   * - key_val_start(void *s, tscfg_tok *key_toks, int nkey_toks,
   *                 tscfg_tok_tag sep);
   * - key_val_end(void *s);
   * - val_start(void *s); // Array value
   * - val_end(void *s);
   * Tokens: true/false/null, quoted/unquoted strings, numbers,
   *  whitespace (if between other tokens), and variables.
   * - token(void *s, tscfg_tok tok);
} tscfg_reader;
