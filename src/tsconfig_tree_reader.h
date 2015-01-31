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
 * Created: 21 July 2014
 */

/*
 * Reader that build a tree.
 */

#ifndef __TSCONFIG_TREE_READER_H
#define __TSCONFIG_TREE_READER_H

#include "tsconfig_reader.h"
#include "tsconfig_tree.h"

// Opaque pointer type
typedef struct tscfg_treeread_state tscfg_treeread_state;

/*
 * Initialize tree reader.
 */
tscfg_rc
tscfg_tree_reader_init(tscfg_reader *reader, tscfg_treeread_state **state);

/*
 * Extract tree and finalize tree reader, including freeing memory.
 * tree: output variable for final tree.  Ownership of all memory handed to
 *       to caller.
 */
tscfg_rc
tscfg_tree_reader_done(tscfg_treeread_state *state, tsconfig_tree *tree);

#endif // __TSCONFIG_TREE_READER_H
