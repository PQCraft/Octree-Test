#ifndef OCTEST_MAP_H
#define OCTEST_MAP_H

#include "util.h"
#include "vlb.h"

enum map_node_type {
    MAP_NODE_PARENT,
    MAP_NODE_VIS,
    MAP_NODE_GEOM
};
struct map_node_parent {
    unsigned children[8];
    /*
        Children are ordered by:
        (+X, +Y, +Z),
        (-X, +Y, +Z),
        (+X, +Y, -Z),
        (-X, +Y, -Z),
        (+X, -Y, +Z),
        (-X, -Y, +Z),
        (+X, -Y, -Z),
        (-X, -Y, -Z).
        Each child indexes 'map.nodes'.
    */
};
struct map_node_vis {
    unsigned depth;
    unsigned child;         /* Indexes 'map.nodes' */
    unsigned first_sibling; /* Indexes 'map.vis_sibs' */
    unsigned sibling_count;
};
struct map_node_geom {
    unsigned shape; /* Indexes 'map.geom_shapes' */
    #if 0
    struct {
        unsigned char r;
        unsigned char g;
        unsigned char b;
    } color;
    #endif
};
struct map_node {
    enum map_node_type type;
    struct vec3 pos;
    float size;
    union {
        struct map_node_parent parent;
        struct map_node_vis vis;
        struct map_node_geom geom;
    } data;
};
struct map_node_geom_shape {
    struct vec3 points[8];
    /*
        Points are ordered by:
        (+X, +Y, +Z),
        (-X, +Y, +Z),
        (+X, +Y, -Z),
        (-X, +Y, -Z),
        (+X, -Y, +Z),
        (-X, -Y, +Z),
        (+X, -Y, -Z),
        (-X, -Y, -Z).
    */
};
struct map {
    float size;
    struct map_node* nodes;
    unsigned* vis_sibs;
    struct map_node_geom_shape* geom_shapes;
};

#endif
