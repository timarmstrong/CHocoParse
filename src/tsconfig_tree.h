
/*
 * Tree for HOCON properties file.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 11 July 2014
 * All rights reserved.
 */

#ifndef __TSCONFIG_TREE_H
#define __TSCONFIG_TREE_H

#include "tsconfig_tok.h"

typedef enum {
  TSCFG_NODE_OBJ,
  TSCFG_NODE_ARR,
} tscfg_node_kind;

typedef struct {
  // TODO
  // * single token
  // * list of unconcatenated tokens
  // * object or array
  int dummy;
} tscfg_obj_val;

typedef struct {
  char *key;
  int orig_ix;
  // TODO: type of seperator
  tscfg_obj_val val;
} tscfg_obj_kv;

typedef struct {
  tscfg_obj_kv *elems;
  int nelems;
} tscfg_obj;

typedef struct {
  tscfg_obj_val *elems;
  int nelems;
} tscfg_arr;

typedef union {
  tscfg_obj *obj;
  tscfg_arr *arr;
} tscfg_node;

typedef struct {
  tscfg_node_kind root_kind;
  tscfg_node root;
} tscfg_tree;

#endif // __TSCONFIG_TREE_H
