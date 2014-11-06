/*
 * Path manipulation functions.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 6 November 2014
 * All rights reserved.
 */

#include "tsconfig_paths.h"

#include <assert.h>

#include "tsconfig_err.h"

tscfg_rc tscfg_path_parse(tscfg_tok_array *toks, tscfg_tok_array *path) {
  assert(toks != NULL);
  assert(path != NULL);

  tscfg_rc rc;

  *path = TSCFG_EMPTY_TOK_ARRAY;

  for (int i = 0; i < toks->len; i++) {
    tscfg_tok *tok = &toks->toks[i];

    switch (tok->tag) {
      case TSCFG_TOK_TRUE:
      case TSCFG_TOK_FALSE:
      case TSCFG_TOK_NULL:
      case TSCFG_TOK_NUMBER:
      case TSCFG_TOK_UNQUOTED:
      case TSCFG_TOK_STRING:
        // TODO: process accordingly.
        // TODO: need to track if we're in middle of token
        REPORT_ERR("Invalid token for path expression: %s",
                   tscfg_tok_tag_name(tok->tag));
        rc = TSCFG_ERR_UNIMPL;
        goto cleanup;

        break;
      default:
        // Something not part of key
        REPORT_ERR("Invalid token for path expression: %s",
                   tscfg_tok_tag_name(tok->tag));
        rc = TSCFG_ERR_INVALID;
        goto cleanup;
    }
  }

  rc = TSCFG_OK;
cleanup:
  if (rc != TSCFG_OK) {
    tscfg_tok_array_free(path, true);
  }
  return rc;
}
