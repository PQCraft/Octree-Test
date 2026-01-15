#include "compiler.h"
#include "crc.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

struct compiler_shape {
    char name[32];
    unsigned name_crc;
    struct map_node_geom_shape data;
};
struct compiler_vis_sib {
    unsigned index;
    float dist;
};
struct compiler {
    float size;
    float min_vis_size;
    struct VLB(struct map_node) nodes;
    struct VLB(unsigned) vis_nodes;
    struct VLB(unsigned) vis_sibs;
    struct VLB(struct compiler_shape) geom_shapes;
    char text_buf[256];
    unsigned max_vis_depth;
    unsigned size_set : 1;
    unsigned min_vis_size_set : 1;
    unsigned tree_set : 1;
};

struct tree_stack_elem {
    struct vec3 pos;
    float size;
};
struct tree {
    struct compiler* compiler;
    FILE* f;
    struct VLB(struct tree_stack_elem) stack;
};

/*static void err_bad_char(char c);*/ /* Unused */
static void err_want_name(void);
static void err_want_number(void);
static void err_want_char(char c);
static void err_mem(void);
static unsigned parser_read_whitespace(FILE* f);
static unsigned parser_read_name(FILE* f, char* buf, unsigned buflen);
static int parser_read_float(FILE* f, char* buf, unsigned buflen, float* out);
static void parser_skip_line(FILE* f);

static unsigned tree_add_vis_node(struct tree* state) {
    unsigned depth = state->stack.len - 1;
    unsigned index = state->compiler->nodes.len;
    struct tree_stack_elem* elem = &state->stack.data[depth];
    struct map_node* node;

    VLB_NEXTPTR(state->compiler->nodes, node, 2, 1, err_mem(); return -1;);
    VLB_ADD(state->compiler->vis_nodes, index, 2, 1, err_mem(); return -1;);

    node->type = MAP_NODE_VIS;
    node->pos = elem->pos;
    node->size = elem->size;
    node->data.vis.depth = depth;

    return index;
}
static unsigned tree_read_node(struct tree* state, const char* type) {
    unsigned depth = state->stack.len - 1;
    unsigned index = state->compiler->nodes.len; /* Get the index that the next node will be created at */
    struct map_node* node;

    /*
        If the max depth for 'vis' nodes has been reached, add one and set the
        child to the node that is about to be read in
    */ 
    if (depth == state->compiler->max_vis_depth) {
        unsigned vis_index = tree_add_vis_node(state);
        if (vis_index == -1U) return -1; /* Failed to add, return error */
        state->compiler->nodes.data[vis_index].data.vis.child = state->compiler->nodes.len;
    }

    /* If it is a 'parent' node */
    if (!strcasecmp(type, "parent")) {
        unsigned i, tmp;
        struct tree_stack_elem* elem;
        struct tree_stack_elem* sub_elem;
        float sub_size;

        /* Used to restore the 'node' pointer after a realloc */
        unsigned node_index;

        if (!parser_read_whitespace(state->f) || fgetc(state->f) != '(') {
            err_want_char('(');
            return -1;
        }

        /* Add a stack elem */
        VLB_NEXTPTR(state->stack, sub_elem, 2, 1, err_mem(); return -1;);
        elem = &state->stack.data[depth];
        sub_size = elem->size * 0.5f;

        /* Add this 'parent' node to the node list */
        node_index = state->compiler->nodes.len;
        VLB_NEXTPTR(state->compiler->nodes, node, 2, 1, err_mem(); return -1;);
        node->type = MAP_NODE_PARENT;
        node->pos = elem->pos;
        node->size = elem->size;

        /* Read in each child */
        for (i = 0; i < 8; ++i) {
            /* Offset the position of the child in the stack */
            sub_elem->pos = elem->pos;
            sub_elem->pos.x += sub_size * ((!(i & 1)) ? 0.5f : -0.5f);
            sub_elem->pos.y += sub_size * ((!(i & 4)) ? 0.5f : -0.5f);
            sub_elem->pos.z += sub_size * ((!(i & 2)) ? 0.5f : -0.5f);
            sub_elem->size = sub_size;

            /* Read in the child */
            if (!parser_read_whitespace(state->f)) {
                err_want_name();
                return -1;
            }
            tmp = parser_read_name(state->f, state->compiler->text_buf, 256);
            if (tmp == -1U) return -1;
            /* If given 'none' */
            if (!tmp || !strcasecmp(state->compiler->text_buf, "none")) {
                if (depth >= state->compiler->max_vis_depth) {
                    /*
                        If the depth is greater than or equal to the max vis
                        depth, a 'vis' node would have been created already,
                        so no need to create one. Just set the child to -1
                        'none'.
                    */
                    node->data.parent.children[i] = -1;
                } else {
                    /*
                        If the depth is less than the max vis depth, a 'vis'
                        node will have not been created yet, so create one and
                        set its child to -1 'none'.
                    */
                    unsigned vis_index = tree_add_vis_node(state);
                    if (vis_index == -1U) return -1;
                    node = &state->compiler->nodes.data[node_index];
                    node->data.parent.children[i] = vis_index;
                    state->compiler->nodes.data[vis_index].data.vis.child = -1;
                }
            /* Otherwise */
            } else {
                /* Read in and add the node */
                tmp = tree_read_node(state, state->compiler->text_buf);
                if (tmp == -1U) return -1;
                node = &state->compiler->nodes.data[node_index];
                node->data.parent.children[i] = tmp; /* Write down the index */
            }

            if (i < 7 && (!parser_read_whitespace(state->f) || fgetc(state->f) != ',')) {
                err_want_char(',');
                return -1;
            }
        }

        if (!parser_read_whitespace(state->f) || fgetc(state->f) != ')') {
            err_want_char(')');
            return -1;
        }

        --state->stack.len;
    /* If it is a 'geom' node */
    } else if (!strcasecmp(type, "geom")) {
        unsigned crc, i, tmp;
        struct tree_stack_elem* elem;

        if (!parser_read_whitespace(state->f) || fgetc(state->f) != '(') {
            err_want_char('(');
            return -1;
        }
        /* Get the name of the shape */
        if (!parser_read_whitespace(state->f) || !(tmp = parser_read_name(state->f, state->compiler->text_buf, 32))) {
            err_want_name();
            return -1;
        }
        if (tmp == -1U) return -1;

        /* Find the shape in the list */
        crc = strcasecrc32(state->compiler->text_buf);
        i = 0;
        while (1) {
            struct compiler_shape* shape;
            if (i == state->compiler->geom_shapes.len) {
                /* If the end of the list has been reached, the shape could not be found */
                fprintf(stderr, "Could not find shape '%s'\n", state->compiler->text_buf);
                return -1;
            }
            shape = &state->compiler->geom_shapes.data[i];
            if (shape->name_crc == crc && !strcasecmp(shape->name, state->compiler->text_buf)) break;
            ++i;
        }

        /*
            If the depth is less than the max vis depth, a 'vis' node will have
            not been created yet. Create it, and set the child to the index this
            'geom' node will be on.
        */
        if (depth < state->compiler->max_vis_depth) {
            unsigned vis_index = tree_add_vis_node(state);
            if (vis_index == -1U) return -1;
            state->compiler->nodes.data[vis_index].data.vis.child = state->compiler->nodes.len;
        }

        elem = &state->stack.data[depth];

        /* Create the 'geom' node */
        VLB_NEXTPTR(state->compiler->nodes, node, 2, 1, err_mem(); return -1;);
        node->type = MAP_NODE_GEOM;
        node->pos = elem->pos;
        node->size = elem->size;
        node->data.geom.shape = i;

        if (!parser_read_whitespace(state->f) || fgetc(state->f) != ')') {
            err_want_char(')');
            return -1;
        }
    } else {
        fprintf(stderr, "Unknown node type '%s'\n", type);
        return -1;
    }

    return index;
}

static int sort_vis_sibs(const void* a_ptr, const void* b_ptr) {
    const struct compiler_vis_sib* a = a_ptr;
    const struct compiler_vis_sib* b = b_ptr;
    return (a->dist > b->dist) - (a->dist < b->dist); /* Sort by low to high */
}

unsigned compile_map(FILE* f, struct map* map) {
    unsigned retval = 1;
    struct compiler state = {0};
    VLB_INIT(state.nodes, 256, err_mem(); goto reterr;);
    VLB_INIT(state.vis_nodes, 256, err_mem(); goto reterr;);
    VLB_INIT(state.vis_sibs, 1024, err_mem(); goto reterr;);
    VLB_INIT(state.geom_shapes, 256, err_mem(); goto reterr;);
    state.min_vis_size = 8;

    /* Evaluate the map file */
    while (1) {
        unsigned namelen;
        if (!parser_read_whitespace(f)) break; /* EOF */
        namelen = parser_read_name(f, state.text_buf, 256);
        if (!namelen) {
            err_want_name();
            goto reterr;
        }
        if (!strcasecmp(state.text_buf, "size")) {
            /* Set the size^3 of the map */

            float size;

            if (state.size_set) {
                fputs("There can only be one 'size' directive\n", stderr);
                goto reterr;
            }
            if (!parser_read_whitespace(f) || parser_read_float(f, state.text_buf, 256, &size) != 1) {
                err_want_number();
                goto reterr;
            }
            if (!parser_read_whitespace(f) || fgetc(f) != ';') {
                err_want_char(';');
                goto reterr;
            }
            if (size <= 0.0f) {
                fputs("Value for 'size' directive must be greater than 0\n", stderr);
                goto reterr;
            }

            state.size = size;
            /*
                Figure out the lowest level of the tree 'vis' nodes should be
                added at given the requested min size
            */
            state.max_vis_depth = 0;
            while (1) {
                if (size <= state.min_vis_size) break;
                size *= 0.5f;
                ++state.max_vis_depth;
            }

            state.size_set = 1;
        } else if (!strcasecmp(state.text_buf, "min_vis_size")) {
            /* Set the minimum size of 'vis' nodes */

            if (state.min_vis_size_set) {
                fputs("There can only be one 'min_vis_size' directive\n", stderr);
                goto reterr;
            }
            if (state.size_set) {
                fputs("The 'min_vis_size' directive cannot be used after the 'size' directive\n", stderr);
                goto reterr;
            }
            if (state.tree_set) {
                fputs("The 'min_vis_size' directive cannot be used after the 'tree' directive\n", stderr);
                goto reterr;
            }
            if (!parser_read_whitespace(f) || parser_read_float(f, state.text_buf, 256, &state.min_vis_size) != 1) {
                err_want_number();
                goto reterr;
            }
            if (!parser_read_whitespace(f) || fgetc(f) != ';') {
                err_want_char(';');
                goto reterr;
            }
            /*
                Stop the min 'vis' node size from becoming too small which would
                cause problems when figuring out the max depth
            */
            if (state.min_vis_size <= 1.0f) {
                fputs("Value for 'min_vis_size' directive must be greater than 1\n", stderr);
                goto reterr;
            }

            state.min_vis_size_set = 1;
        } else if (!strcasecmp(state.text_buf, "shape")) {
            /* Read in a shape */
            /* It takes in 3 numbers (an X, Y, and Z coordinate) 8 times (for the 8 points of the hull) */

            unsigned i;
            struct compiler_shape* shape;

            if (!parser_read_whitespace(f) || !(i = parser_read_name(f, state.text_buf, 32))) {
                err_want_name();
                goto reterr;
            }
            if (i == -1U) goto reterr;

            if (!parser_read_whitespace(f) || fgetc(f) != '{') {
                err_want_char('{');
                goto reterr;
            }

            /* Add the shape to the list */
            VLB_NEXTPTR(state.geom_shapes, shape, 2, 1, err_mem(); goto reterr;);
            strcpy(shape->name, state.text_buf);
            shape->name_crc = strcasecrc32(state.text_buf);
            for (i = 0; i < 8; ++i) {
                shape->data.points[i].x = (!(i & 1)) ? 1.0f : -1.0f;
                shape->data.points[i].y = (!(i & 4)) ? 1.0f : -1.0f;
                shape->data.points[i].z = (!(i & 2)) ? 1.0f : -1.0f;
            }
            for (i = 0; i < 8; ++i) {
                /* Read X */
                if (!parser_read_whitespace(f)) {
                    err_want_number();
                    goto reterr;
                }
                if (parser_read_float(f, state.text_buf, 256, &shape->data.points[i].x) == -1) goto reterr;
                if (!parser_read_whitespace(f) || fgetc(f) != ',') {
                    err_want_char(',');
                    goto reterr;
                }
                /* Read Y */
                if (!parser_read_whitespace(f)) {
                    err_want_number();
                    goto reterr;
                }
                if (parser_read_float(f, state.text_buf, 256, &shape->data.points[i].y) == -1) goto reterr;
                if (!parser_read_whitespace(f) || fgetc(f) != ',') {
                    err_want_char(',');
                    goto reterr;
                }
                /* Read Z */
                if (!parser_read_whitespace(f)) {
                    err_want_number();
                    goto reterr;
                }
                if (parser_read_float(f, state.text_buf, 256, &shape->data.points[i].z) == -1) goto reterr;
                if (i < 7 && (!parser_read_whitespace(f) || fgetc(f) != ',')) {
                    err_want_char(',');
                    goto reterr;
                }
            }

            if (!parser_read_whitespace(f) || fgetc(f) != '}') {
                err_want_char('}');
                goto reterr;
            }
            if (!parser_read_whitespace(f) || fgetc(f) != ';') {
                err_want_char(';');
                goto reterr;
            }
        } else if (!strcasecmp(state.text_buf, "tree")) {
            /* Read in the node tree */

            struct tree tree;
            struct tree_stack_elem* elem;
            struct tree_stack_elem initelem = {0};
            unsigned tree_ret;

            initelem.size = state.size;

            if (!state.size_set) {
                fputs("There needs to be one 'size' directive\n", stderr);
                goto reterr;
            }
            if (state.tree_set) {
                fputs("There can only be one 'tree' directive\n", stderr);
                goto reterr;
            }
            if (!parser_read_whitespace(f) || fgetc(f) != '{') {
                err_want_char('{');
                goto reterr;
            }

            /* Init the tree reader state */
            tree.compiler = &state;
            tree.f = f;
            VLB_INIT(tree.stack, 256, err_mem(); goto reterr;);
            VLB_NEXTPTR(tree.stack, elem, 2, 1, VLB_FREE(tree.stack); err_mem(); goto reterr;);
            *elem = initelem;

            /* Read in the root (first) node */
            if (!parser_read_whitespace(f) || !(tree_ret = parser_read_name(f, state.text_buf, 32))) {
                err_want_name();
                goto reterr;
            }
            if (tree_ret == -1U) goto reterr;
            tree_ret = tree_read_node(&tree, state.text_buf);

            /* Deinit the tree reader state */
            VLB_FREE(tree.stack);
            if (tree_ret == -1U) goto reterr;

            if (!parser_read_whitespace(f) || fgetc(f) != '}') {
                err_want_char('}');
                goto reterr;
            }
            if (!parser_read_whitespace(f) || fgetc(f) != ';') {
                err_want_char(';');
                goto reterr;
            }

            state.tree_set = 1;
        } else {
            fprintf(stderr, "Unknown directive '%s'\n", state.text_buf);
            goto reterr;
        }
    }

    /*
        Generate the sibling list for each 'vis' node.
        Siblings must be sorted from near to far to eliminate overdraw.
    */
    {
        unsigned i;
        struct compiler_vis_sib* sib_sort_data = malloc((state.vis_nodes.len - 1) * sizeof(*sib_sort_data));

        /* For each 'vis' node */
        for (i = 0; i < state.vis_nodes.len; ++i) {
            unsigned index = state.vis_nodes.data[i];
            struct map_node* node = &state.nodes.data[index];
            unsigned j;

            /* Prepare for sorting by populating the sort data with the indices and distances of all the other 'vis' nodes */
            {
                struct compiler_vis_sib* sib_sort_cur = sib_sort_data;
                for (j = 0; j < state.vis_nodes.len; ++j) {
                    unsigned sub_index = state.vis_nodes.data[j];
                    struct map_node* sub_node = &state.nodes.data[sub_index];

                    if (sub_index == index) continue; /* Make it so the 'vis' node doesn't list itself as a sibling */

                    /*
                        TODO: Occlusion culling
                        If there is a node completely obstructing the view to
                        nodes behind it, those obstructed nodes should be
                        skipped and not added to the sibling list.
                    */

                    sib_sort_cur->index = sub_index;
                    sib_sort_cur->dist = vec3_dist(&node->pos, &sub_node->pos);
                    ++sib_sort_cur;
                }
            }

            /* Sort from near to far */
            qsort(sib_sort_data, state.vis_nodes.len - 1, sizeof(sib_sort_data), sort_vis_sibs);

            /* Copy out the sorted indices */
            node->data.vis.first_sibling = state.vis_sibs.len;
            node->data.vis.sibling_count = state.vis_nodes.len - 1;
            VLB_EXPANDBY(state.vis_sibs, state.vis_nodes.len - 1, 2, 1, err_mem(); free(sib_sort_data); goto reterr;);
            {
                unsigned* first_sib_ptr = state.vis_sibs.data + node->data.vis.first_sibling;
                for (j = 0; j < state.vis_nodes.len - 1; ++j) {
                    first_sib_ptr[j] = sib_sort_data[j].index;
                }
            }
        }

        free(sib_sort_data);
    }

    /* Write out the map data */
    map->size = state.size;
    VLB_SHRINK(state.nodes, VLB_OOM_NOP);
    map->nodes = state.nodes.data;
    VLB_SHRINK(state.vis_sibs, VLB_OOM_NOP);
    map->vis_sibs = state.vis_sibs.data;
    {
        unsigned i;
        map->geom_shapes = malloc(state.geom_shapes.len * sizeof(*map->geom_shapes));
        for (i = 0; i < state.geom_shapes.len; ++i) {
            map->geom_shapes[i] = state.geom_shapes.data[i].data;
        }
    }

    ret_no_set:
    VLB_FREE(state.vis_nodes);
    VLB_FREE(state.geom_shapes);
    return retval;

    reterr:
    retval = 0;
    VLB_FREE(state.nodes);
    VLB_FREE(state.vis_sibs);
    goto ret_no_set;
}

void free_map(struct map* map) {
    free(map->nodes);
    free(map->vis_sibs);
    free(map->geom_shapes);
}

#if 0 /* Unused */
static void err_bad_char(char c) {
    if (isprint(c)) {
        fprintf(stderr, "Unexpected '%c'\n", c);
    } else if (c == EOF) {
        fputs("Unexpected EOF\n", stderr);
    } else {
        fprintf(stderr, "Unexpected '\\x%02X'\n", (unsigned)c);
    }
}
#endif
static void err_want_name(void) {
    fputs("Expected a name\n", stderr);
}
static void err_want_number(void) {
    fputs("Expected a number\n", stderr);
}
static void err_want_char(char c) {
    fprintf(stderr, "Expected '%c'\n", c);
}
static void err_mem(void) {
    fputs("Memory error\n", stderr);
}

static unsigned parser_read_whitespace_simple(FILE* f) {
    int c = fgetc(f);
    if (c != ' ' && c != '\t' && c != '\n') {
        ungetc(c, f);
        return 0;
    }
    do {
        c = fgetc(f);
    } while (c == ' ' || c == '\t' || c == '\n');
    ungetc(c, f);
    return 1;
}
static unsigned parser_read_whitespace(FILE* f) {
    while (1) {
        int c;
        parser_read_whitespace_simple(f);
        c = fgetc(f);
        if (c == EOF) return 0;
        if (c == '#') {
            parser_skip_line(f);
        } else {
            ungetc(c, f);
            return 1;
        }
    }
}
static unsigned parser_read_name(FILE* f, char* buf, unsigned buflen) {
    unsigned oldbuflen = buflen;
    while (1) {
        int c = fgetc(f);
        if (!isalnum(c) && c != '_') {
            ungetc(c, f);
            break;
        }
        if (buflen == 1) {
            fputs("Name too long\n", stderr);
            return -1;
        }
        *buf++ = c;
        --buflen;
    }
    *buf = '\0';
    return oldbuflen - buflen;
}
static int parser_read_float(FILE* f, char* buf, unsigned buflen, float* out) {
    unsigned decpt = 0;
    char* oldptr = buf;
    int c = fgetc(f);
    if (isdigit(c) || c == '-') {
        goto is_valid;
    } else if (c == '.') {
        decpt = 1;
        goto is_valid;
    }
    ungetc(c, f);
    return 0;
    while (1) {
        c = fgetc(f);
        if (!isdigit(c)) {
            if (c == '.') {
                if (decpt) {
                    fputs("Too many '.' in number\n", stderr);
                    return -1;
                }
                decpt = 1;
            } else {
                ungetc(c, f);
                break;
            }
        }
        is_valid:
        if (buflen == 1) {
            fputs("Number too long\n", stderr);
            return -1;
        }
        *buf++ = c;
        --buflen;
    }
    *buf = '\0';
    *out = atof(oldptr);
    return 1;
}
static void parser_skip_line(FILE* f) {
    int c;
    do {
        c = fgetc(f);
    } while (c != '\n' && c != EOF);
}
