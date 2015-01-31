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
 * Created: 11 July 2014
 */

/*
 * Tree for HOCON properties file.
 */

#ifndef __TSCONFIG_TREE_H
#define __TSCONFIG_TREE_H

#include "tsconfig_common.h"
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
} tscfg_val;

typedef enum {
  TSCFG_KV_ASSIGN, // E.g. from the = operator
  TSCFG_KV_APPEND, // E.g. from the += operator
} tscfg_kv_assignment;

typedef struct {
  char *key;
  size_t key_len;
  int orig_ix;
  /** Type of separator used to create KV */
  tscfg_kv_assignment assignment;
  tscfg_val val;
} tscfg_obj_kv;

typedef struct {
  tscfg_obj_kv *elems;
  int nelems;
} tscfg_obj;

typedef struct {
  tscfg_val *elems;
  int nelems;
} tscfg_arr;

typedef union {
  tscfg_obj *obj;
  tscfg_arr *arr;
} tscfg_node;

typedef struct {
  tscfg_node_kind root_kind;
  tscfg_node root;
} tsconfig_tree;

/*
 * Sort object keys by (key, orig_ix).
 */
tscfg_rc tscfg_obj_sort(tscfg_obj *obj);

/*
 * Merge duplicate keys according to HOCON config rules.
 * Before, keys should be sorted.
 * After, no duplicate keys will exist.
 * Any unused memory from duplicate keys will be freed.
 */
tscfg_rc tscfg_obj_merge_keys(tscfg_obj *obj);

/*
 * Concatenate v2 onto v1, freeing any unused memory for v2.
 * After returning, v1 contains concatenated value and v2 is empty.
 */
tscfg_rc tscfg_val_concat(tscfg_val *v1, tscfg_val *v2);

/*
 * Implement HOCON rules for overwritten values.
 */
tscfg_rc tscfg_val_overwrite(tscfg_val *v1, tscfg_val *v2);

void tscfg_val_free(tscfg_val *v);

/*
 * Merge o2 into o1. Definitions from o2 take precedence over o1.
 * Before, both o2 and o1 must have sorted keys.
 *
 * After return, o2 is empty and o1 contains keys from both.
 * This doesn't merge any duplicate keys.
 * TODO: should this return a flag indicating duplicates?
 * TODO: should this do some preliminary merging, e.g. overwrites?
 */
tscfg_rc tscfg_obj_merge(tscfg_obj *o1, tscfg_obj *o2);

#endif // __TSCONFIG_TREE_H
