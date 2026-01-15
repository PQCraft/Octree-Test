#ifndef OCTEST_COMPILER_H
#define OCTEST_COMPILER_H

#include "map.h"

#include <stdio.h>

unsigned compile_map(FILE* in, struct map* out);
void free_map(struct map* map);

#endif
