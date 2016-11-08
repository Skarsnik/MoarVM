#include "moar.h"

typedef struct {
    MVMint32 key;
    MVMint32 num_defs, num_uses;
    MVMint32 live_range_idx;
} UnionFind;

typedef struct {
    MVMint32 num_defs;
    MVMint32 *defs;
    MVMint32 num_uses;
    MVMint32 *uses;

    MVMint32 spilled_to; /* location of value in memory */
    /* unchanging location of value in register (otherwise we need
       more live ranges, or something...) */
    MVMJitStorageClass reg_cls;
    MVMint32 reg_num;
} LiveRange;

typedef struct {
    /* Sets of values */
    UnionFind *value_sets;
    /* single buffer for uses, definitions */
    MVMint32 *use_defs_buf;

    /* All values ever defined by the register allcoator */
    MVM_VECTOR_DECL(LiveRange, values);

    /* 'Currently' active values */
    MVMint32 active_top;
    MVMint32 active[MAX_ACTIVE];

    /* which live-set inhabits a constant register */
    MVMint32 prefered_register[NUM_GPR];

    /* Values still left to do (heap) */
    MVM_VECTOR_DECL(MVMint32, worklist);
    /* Retired values (to be assigned registers) (heap) */
    MVM_VECTOR_DECL(MVMint32, retired);

    /* Register handout ring */
    MVMint8 reg_buf[NUM_GPR];
    MVMint32 reg_give, reg_take;

    MVMint32 spill_top;
} RegisterAllocator;


UnionFind * value_set_find(UnionFind *sets, MVMint32 key) {
    while (sets[key].key != key) {
        key = sets[key].key;
    }
    return sets + key;
}


MVMint32 value_set_union(UnionFind *sets, MVMint32 a, MVMint32 b) {
    if (sets[a].num_defs < sets[b].num_defs) {
        MVMint32 t = a; a = b; b = t;
    }
    sets[b].key = a; /* point b to a */
    sets[a].num_defs += sets[b].num_defs;
    sets[a].num_uses += sets[b].num_uses;
    return a;
}

MVMint32 live_range_init(RegisterAllocator *alc, MVMint32 *defs, MVMint32 *uses) {
    LiveRange *range;
    MVMint32 idx = alc->values_top++;
    MVM_VECTOR_ENSURE_SIZE(alc->values, idx);
    range = &alc->values[idx];
    range->defs = defs;
    range->uses = uses;
    range->num_defs = 0;
    range->num_uses = 0;
    return idx;
}

/* quick accessors for common checks */
static inline MVMint32 first_def(LiveRange *range) {
    return range->defs[0];
}

static inline MVMint32 last_use(LiveRange *v) {
    return (v->uses[v->num_uses-1]);
}


/* Functions to maintain a heap of references to the live ranges */
void live_range_heap_down(LiveRange *values, MVMint32 *heap, MVMint32 top, MVMint32 item) {
    while (item < top) {
        MVMint32 left = item * 2 + 1;
        MVMint32 right = left + 1;
        MVMint32 swap;
        if (right < top) {
            swap = first_def(&values[heap[left]]) < first_def(&values[heap[right]]) ? left : right;
        } else if (left < top) {
            swap = left;
        } else {
            break;
        }
        if (first_def(&values[heap[swap]]) < first_def(&values[heap[item]])) {
            MVMint32 temp = heap[swap];
            heap[swap] = heap[item];
            heap[item] = temp;
            item = swap;
        } else {
            break;
        }
    }
}

void live_range_heap_up(LiveRange *values, MVMint32 *heap, MVMint32 item) {
    while (item > 0) {
        MVMint32 parent = (item-1)/2;
        if (first_def(&values[heap[parent]]) < first_def(&values[heap[item]])) {
            MVMint32 temp = heap[parent];
            heap[parent] = heap[item];
            heap[item]   = temp;
            item = parent;
        } else {
            break;
        }
    }
}

MVMint32 live_range_heap_pop(LiveRange *values, MVMint32 *heap, MMVint32 *top) {
    MVMint32 v = heap[0];
    MVMint32 t = --(*top);
    /* pop by swap and heap-down */
    heap[0]    = heap[t];
    live_range_heap_down(values, heap, t, 0);
    return v;
}

void live_range_heapify(LiveRange *values, MVMint32 *heap, MVMint32 top) {
    MVMint32 i = top, mid = top/2;
    while (i-- > mid) {
        live_range_heap_up(values, heap, i);
    }
}


static void determine_live_ranges(MVMThreadContext *tc, MVMJitTileList *list, RegisterAllocator *alc) {
    MVMint32 i, j;
    MVMint32 num_use = 0, num_def = 0, num_live_range = 0;
    MVMint32 tile_nodes[16];
    MVMint32 *use_buf, *def_buf;

    for (i = 0; i < list->items_num; i++) {
        MVMJitTile *tile = list->items[i];
        MVMint32 node = list->tree[tile->node];
        /* Each of the following counts as either a copy or as a PHI (in case of
         * IF), and thus these are not actual definitions */
        if (node == MVM_JIT_COPY) {
            MVMint32 ref        = list->tree[tile->node + 1];
            alc->sets[node].key = ref; /* point directly to actual definition */
        } else if (node == MVM_JIT_DO && TILE_YIELDS_VALUE(tile)) {
            MVMint32 nchild     = list->tree[tile->node + 1];
            MVMint32 ref        = list->tree[tile->node + nchild];
            alc->sets[node].key = ref;
        } else if (node == MVM_JIT_IF) {
            MVMint32 left_cond   = list->tree[tile->node + 2];
            MVMint32 right_cond  = list->tree[tile->node + 3];
            alc->sets[node].key  = value_set_union(alc->sets, left_cond, right_cont);
            num_live_range--;      /* the union of the left and right side
                                    * reduces the number of live ranges */
        } else if (TILE_YIELDS_VALUE(tile)) {
            /* define this value */
            alc->sets[node].num_defs   = 1;
            alc->sets[node].num_uses   = 0;
            alc->sets[node].key        = node;
            alc->sets[node].live_range = -1;

            /* count totals so we can correctly allocate the buffers */
            num_def++;
            num_use += tile->num_values;
            num_live_range++;
        }
        /* NB - need to kill this bit! */
        if (tile->template) {
            /* read the tile */
            MVM_jit_expr_tree_get_nodes(tc, list->tree, tile->node, tile->template->path, tile_nodes);
            for (j = 0; j < tile->num_values; j++) {
                if ((tile->template->value_bitmap & (1 << j)) == 0)
                    continue; /* is a constant parameter to the tile, not a reference */
                used_node = tile->nodes[j];
                /* account its use */
                value_set_find(alc->sets, used_tile)->num_uses++;
            }
        }
        /* I don't think we have inserted things before that actually refer to
         * tiles, just various jumps to implement IF/WHEN/ANY/ALL handling */
    }

    /* Initialize buffers. Live range buffer can grow, uses-and-definitions
     * buffer never needs to, because any split can just reuse the buffers */
    MVM_VECTOR_INIT(alc->values, num_live_range);
    MVM_VECTOR_INIT(alc->worklist, num_live_range);
    MVM_VECTOR_INIT(alc->retired, num_live_range);
    alc->use_defs_buf = MVM_malloc(sizeof(MVMint32) * (num_defs + num_uses));

    /* split buf in two */
    use_buf = alc->use_defs_buf;
    def_buf = alc->use_defs_buf + num_use;

    /* live range allocation cursor */
    k = 0;
    /* second pass, translate the found sets and used nodes to live ranges.
     * because we iterate in ascending order over tiles, uses and defs are
     * automatically ordered too. TODO: figure out a way to represent register
     * preferences! */
    for (i = 0; i < list->items_num; i++) {
        MVMJitTile * tile;
        if (TILE_YIELDS_VALUE(tile)) {
            UnionFind *value_set = value_set_find(alc->sets, tile->node);
            LiveRange *value_range;
            if (value_set->live_range < 0) {
                /* first definition, allocate the live range for this block */
                value_set->live_range = live_range_init(alc, def_buf, use_buf);
                /* bump pointers */
                def_buf += value_set->num_defs;
                use_buf += value_set->num_uses;
                /* add to the work list (which is automatically in first-definition order) */
                MVM_VECTOR_PUSH(alc->worklist, value_set->live_range);
            }
            value_range = alc->values[value_set->live_range];
            /* add definition */
            value_range->defs[value_range->num_defs++] = i;
        }
        /* Add uses */
        for (j = 0; j < tile->num_values; j++) {
            /* NB - this is a good place to translate between nodes and live range idxs */
            UnionFind *use_set = value_set_find(alc->sets, tile->nodes[j]);
            LiveRange *use_range = alc->values[use_set->live_range];
            use_range[use_range->num_uses++] = i;
        }
    }
}

/* The code below needs some thinking... */
static void active_set_add(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 a) {
    /* the original linear-scan heuristic for spilling is to take the last value
     * in the set to expire, freeeing up the largest extent of code... that is a
     * reasonably good heuristic, albeit not essential to the concept of linear
     * scan. It makes sense to keep the stack ordered at all times (simplest by
     * use of insertion sort). Although insertion sort is O(n^2), n is never
     * large in this case (32 for RISC architectures, maybe, if we ever support
     * them; 7 for x86-64. So the time spent on insertion sort is always small
     * and bounded by a constant, hence O(1). Yes, algorithmics works this way
     * :-) */
    MVMint32 i;
    for (i = 0; i < alc->live_set_top; i++) {
        MVMint32 b = alc->active[i];
        if (last_use(&alc->values[b]) > last_use(&alc->values[a])) {
            /* insert a before b */
            memmove(alc->active + i + 1, alc->active + i, sizeof(MVMint32)*(alc->active_top - i));
            alc->active[i] = b;
            alc->active_top++;
            return;
        }
    }
    /* append at the end */
    alc->active[alc->active_top++] = value;
}

static void active_set_expire(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 position) {
    MVMint32 i;
    for (i = 0; i < alc->live_set_top; i++) {
        MVMint32 v = alc->active[i];
        if (last_use(&alc->values[v]) > position) {
            break;
        }
        /* retire this live range */
        MVM_VECTOR_PUSH(alc->retired, v);
    }

    /* shift off the first x values from the live set. */
    if (i > 0) {
        alc->active_top -= i;
        memmove(alc->active, alc->active + i, alc->active_top * sizeof(MVMint32));
    }
}


static void spill_live_range(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list, MVMint32 position) {
    /* Spilling involves the following:
       - choosing a live range from the active set to spill
       - finding a place where to spill it
       - choosing whether to split this live range in a pre-spill and post-spill part
          - potentially spill only part of it
       - for each definition (in the spilled range),
          - make a new live range that
          - reuses the use and def pointer for the definition
          - insert a store just after the defintion
          - and if it lies in the future, put it on worklist, if it lies in the past, put it on the retired list
          - and update the definition to point to the newly created live range
       - for each use (in the spilled range)
          - make a new live range that reuses the use and def pointer for the use
          - insert a load just before the use
          - if it lies in the future, put it on the worklist, if it lies in the past, put it on the retired list
          - update the using tile to point to the newly created live range
       - remove it from the active set
    */
}

static void split_live_range(MVMThreadContext *tc, RegisterAllocator *alc, MVMint32 from, MVMint32 to) {
}

static void linear_scan(MVMThreadContext *tc, RegisterAllocator *alc, MVMJitTileList *list) {
    MVMint32 i, j;
    while (alc->worklist_top > 0) {
        MVMint32 v = live_range_heap_pop(alc->values, alc->worklist, &alc->worklist_top);
        MVMint32 pos = first_def(&alc->values[v]);
        active_set_expire(tc, alc, pos);
        if (alc->active_top >= MAX_ACTIVE) {
            spill_live_range(tc, alc, list, pos);
        }
        active_set_add(tc, alc, v);
    }
    /* flush active live ranges */
    for (i = 0; i < alc->active_top; i++) {
        MVM_VECTOR_PUSH(alc->retired, alc->active[i]);
    }
    live_range_heapify(tc, alc->retired, alc->retired_top);
    while (alc->retired_top > 0) {
        MVMint32 v = live_range_heap_pop(alc->values, &alc->retired, alc->retired_top);
        /* assign registers, wants some thinking on tile structure as well */
    }
}