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
 *
 * Created: 14 July 2014
 */

/*
 * Data model for HOCON/JSON.
 */

#include "tsconfig_tree.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "tsconfig_err.h"

static int key_cmp(const char *s1, size_t l1, const char *s2, size_t l2);
static int obj_kv_cmp(const void *p1, const void *p2);

static void tscfg_val_invalidate(tscfg_val *v);

/*
 * TODO: this approach cannot handle self-referential substitutions.
 * Needed approach:
 * - Resolve non-self-referential subs after final merge.
 * - Detect self-referential subs before merge, handle specially
 */
tscfg_rc tscfg_obj_merge_keys(tscfg_obj *obj) {
  tscfg_rc rc;

  int prev = 0; /* Previous (merged) key pos */
  int curr = 1; /* Current (unmerged) key pos */

  for (; curr < obj->nelems; curr++) {
    tscfg_obj_kv *prev_kv = &obj->elems[prev];
    tscfg_obj_kv *curr_kv = &obj->elems[curr];
    if (key_cmp(prev_kv->key, prev_kv->key_len,
                curr_kv->key, curr_kv->key_len) == 0) {
      // Matching keys: needs merge
      if (curr_kv->assignment == TSCFG_KV_APPEND) {
        // Attempt to concatenate
        rc = tscfg_val_concat(&prev_kv->val, &curr_kv->val);
        TSCFG_CHECK(rc);
      } else {
        assert(curr_kv->assignment == TSCFG_KV_ASSIGN);
        rc = tscfg_val_overwrite(&prev_kv->val, &curr_kv->val);
        TSCFG_CHECK(rc);
      }
      free(curr_kv->key);
      curr_kv->key = NULL;
    } else {
      // Non-matching keys: no merge, just relocate
      prev++;
      obj->elems[prev] = *curr_kv;
      curr_kv->key = NULL;
    }
  }

  return TSCFG_OK;
}

tscfg_rc tscfg_obj_sort(tscfg_obj *obj) {
  if (obj == NULL || obj->elems == NULL || obj->nelems < 0) {
    REPORT_ERR("Invalid key-value object: %p", obj);
    return TSCFG_ERR_ARG;
  }

  qsort(obj->elems, (size_t)obj->nelems, sizeof(obj->elems[0]), obj_kv_cmp);

  return TSCFG_OK;
}

/*
 * Compare keys with explicit lengths.
 */
static int key_cmp(const char *s1, size_t l1, const char *s2, size_t l2) {
  int result;
  size_t min_len = (l1 < l2) ?  l1 : l2;
  if ((result = memcmp(s1, s2, min_len)) != 0) {
    return result;
  } else if (l1 == l2) {
    // Identical
    return 0;
  } else {
    // One has trailing chars
    return (l1 < l2) ? -1 : 1;
  }
}

static int obj_kv_cmp(const void *p1, const void *p2) {
  const tscfg_obj_kv *kv1 = p1, *kv2 = p2;

  int result;
  if ((result = key_cmp(kv1->key, kv1->key_len, kv2->key, kv2->key_len)) != 0) {
    // Difference in corresponding chars
    return result;
  } else if (kv1->orig_ix != kv2->orig_ix) {
    // Break ties by original index
    return (kv1->orig_ix < kv2->orig_ix) ? -1 : 1;
  } else {
    return 0;
  }
}

tscfg_rc tscfg_val_concat(tscfg_val *v1, tscfg_val *v2) {
  // TODO
  // * Types must match
  // * Value concat: append tokens
  // * Array concat: append elements
  // * Obj concat: merge objects
  // * TODO: do we assume sub-objects already merged?
  return TSCFG_ERR_UNIMPL;
}

tscfg_rc tscfg_obj_merge(tscfg_obj *o1, tscfg_obj *o2) {
  // TODO
  return TSCFG_ERR_UNIMPL;
}

bool val_is_obj(tscfg_val *v1) {
  // TODO: implement
  return true;
}

tscfg_rc tscfg_val_overwrite(tscfg_val *v1, tscfg_val *v2) {
  tscfg_rc rc;
  if (val_is_obj(v1) && val_is_obj(v2)) {
    // TODO
    tscfg_obj *o1 = NULL, *o2 = NULL;
    rc = tscfg_obj_merge(o1, o2);
    TSCFG_CHECK(rc);
  } else {
    tscfg_val_free(v1);
    *v1 = *v2;
    tscfg_val_invalidate(v2);
  }
  return TSCFG_ERR_UNIMPL;
}

void tscfg_val_free(tscfg_val *v) {
  // TODO: implemnent
}

static void tscfg_val_invalidate(tscfg_val *v) {
  // TODO: set to invalid value
}
