#ifndef OCTEST_COMPILER_H
#define OCTEST_COMPILER_H

#include "map.h"

#include <stdio.h>

struct map_compiler_opts {
    float min_vis_node_size;
};
extern struct map_compiler_opts map_compiler_default_opts;
/*
    Defaults:
        min_vis_node_size = 8
*/

unsigned compile_map(FILE* in, const struct map_compiler_opts* opts, struct map* out);
void free_map(struct map* map);

#endif
