/*
 * Reader that build a tree.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 21 July 2014
 * All rights reserved.
 */

#include "tsconfig_tree_reader.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "tsconfig_err.h"

struct tscfg_treeread_state {
  tsconfig_tree tree;
};

/*
 * Reader functions
 */
static bool tread_obj_start(void *s);
static bool tread_obj_end(void *s);
static bool tread_arr_start(void *s);
static bool tread_arr_end(void *s);
static bool tread_key_val_start(void *s, tscfg_tok *key_toks, int nkey_toks,
                                tscfg_tok_tag sep);
static bool tread_key_val_end(void *s);
static bool tread_val_start(void *s);
static bool tread_val_end(void *s);
static bool tread_token(void *s, tscfg_tok *tok);

tscfg_rc
tscfg_tree_reader_init(tscfg_reader *reader, tscfg_treeread_state **state) {
  *state = malloc(sizeof(tscfg_treeread_state));
  TSCFG_CHECK_MALLOC(*state);

  reader->obj_start = tread_obj_start;
  reader->obj_end = tread_obj_end;
  reader->arr_start = tread_arr_start;
  reader->arr_end = tread_arr_end;
  reader->key_val_start = tread_key_val_start;
  reader->key_val_end = tread_key_val_end;
  reader->val_start = tread_val_start;
  reader->val_end = tread_val_end;
  reader->token = tread_token;

  return TSCFG_OK;
}

tscfg_rc
tscfg_tree_reader_done(tscfg_treeread_state *state, tsconfig_tree *tree) {
  *tree = state->tree; // Shallow copy

  free(state);
  return TSCFG_OK;
}

static bool tread_obj_start(void *s) {
  // TODO: implement
  fprintf(stderr, "obj_start\n");
  return true;
}

static bool tread_obj_end(void *s) {
  // TODO: implement
  fprintf(stderr, "obj_end\n");
  return true;
}

static bool tread_arr_start(void *s) {
  // TODO: implement
  return true;
}

static bool tread_arr_end(void *s) {
  // TODO: implement
  return true;
}

static bool tread_key_val_start(void *s, tscfg_tok *key_toks, int nkey_toks,
                                tscfg_tok_tag sep) {
  // TODO: implement
  fprintf(stderr, "key_val_start: key=[");

  for (int i = 0; i < nkey_toks; i++) {
    if (i != 0) {
      fprintf(stderr, ", ");
    }
    fprintf(stderr, "%s \"%.*s\"", tscfg_tok_tag_name(key_toks[i].tag),
                (int)key_toks[i].len, key_toks[i].str);
  }

  fprintf(stderr, "] sep=%s\n", tscfg_tok_tag_name(sep));
  return true;
}

static bool tread_key_val_end(void *s) {
  // TODO: implement
  return true;
}

static bool tread_val_start(void *s) {
  // TODO: implement
  return true;
}

static bool tread_val_end(void *s) {
  // TODO: implement
  return true;
}

static bool tread_token(void *s, tscfg_tok *tok) {
  fprintf(stderr, "tok: %s \"%.*s\"\n", tscfg_tok_tag_name(tok->tag),
              (int)tok->len, tok->str);
  return true;
}

