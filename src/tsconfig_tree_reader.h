/*
 * Reader that build a tree.
 *
 * Author: Tim Armstrong
 * tim.g.armstrong@gmail.com
 * Created: 21 July 2014
 * All rights reserved.
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
