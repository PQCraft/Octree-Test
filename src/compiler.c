#include "compiler.h"
#include "crc.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>

struct map_compiler_opts map_compiler_default_opts = {
    8
};

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
    struct VLB(struct map_node) nodes;
    struct VLB(unsigned) vis_nodes;
    struct VLB(unsigned) vis_sibs;
    struct VLB(struct compiler_shape) geom_shapes;
    char text_buf[256];
    unsigned vis_max_depth;
    unsigned size_set : 1;
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
    unsigned index = state->compiler->nodes.len;
    struct map_node* node;

    if (depth == state->compiler->vis_max_depth) {
        unsigned vis_index = tree_add_vis_node(state);
        if (vis_index == -1U) return -1;
        state->compiler->nodes.data[vis_index].data.vis.child = state->compiler->nodes.len;
    }

    if (!strcasecmp(type, "parent")) {
        unsigned i, tmp;
        struct tree_stack_elem* elem;
        struct tree_stack_elem* sub_elem;
        float sub_size;

        if (!parser_read_whitespace(state->f) || fgetc(state->f) != '(') {
            err_want_char('(');
            return -1;
        }

        VLB_NEXTPTR(state->stack, sub_elem, 2, 1, err_mem(); return -1;);
        elem = &state->stack.data[depth];
        sub_size = elem->size * 0.5f;

        VLB_NEXTPTR(state->compiler->nodes, node, 2, 1, err_mem(); return -1;);
        node->type = MAP_NODE_PARENT;
        node->pos = elem->pos;
        node->size = elem->size;

        for (i = 0; i < 8; ++i) {
            sub_elem->pos = elem->pos;
            sub_elem->pos.x += sub_size * ((!(i & 1)) ? 0.5f : -0.5f);
            sub_elem->pos.y += sub_size * ((!(i & 4)) ? 0.5f : -0.5f);
            sub_elem->pos.z += sub_size * ((!(i & 2)) ? 0.5f : -0.5f);
            sub_elem->size = sub_size;

            if (!parser_read_whitespace(state->f)) {
                err_want_name();
                return -1;
            }
            tmp = parser_read_name(state->f, state->compiler->text_buf, 256);
            if (tmp == -1U) return -1;

            if (!tmp || !strcasecmp(state->compiler->text_buf, "none")) {
                if (depth >= state->compiler->vis_max_depth) {
                    node->data.parent.children[i] = -1;
                } else {
                    unsigned vis_index = tree_add_vis_node(state);
                    if (vis_index == -1U) return -1;
                    node->data.parent.children[i] = vis_index;
                    state->compiler->nodes.data[vis_index].data.vis.child = -1;
                }
            } else {
                tmp = tree_read_node(state, state->compiler->text_buf);
                if (tmp == -1U) return -1;
                node->data.parent.children[i] = tmp;
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
    } else if (!strcasecmp(type, "geom")) {
        unsigned crc, i, tmp;
        struct tree_stack_elem* elem;

        if (!parser_read_whitespace(state->f) || fgetc(state->f) != '(') {
            err_want_char('(');
            return -1;
        }
        if (!parser_read_whitespace(state->f) || !(tmp = parser_read_name(state->f, state->compiler->text_buf, 32))) {
            err_want_name();
            return -1;
        }
        if (tmp == -1U) return -1;

        crc = strcasecrc32(state->compiler->text_buf);
        i = 0;
        while (1) {
            struct compiler_shape* shape;
            if (i == state->compiler->geom_shapes.len) {
                fprintf(stderr, "Could not find shape '%s'\n", state->compiler->text_buf);
                return -1;
            }
            shape = &state->compiler->geom_shapes.data[i];
            if (shape->name_crc == crc && !strcasecmp(shape->name, state->compiler->text_buf)) break;
            ++i;
        }

        if (depth < state->compiler->vis_max_depth) {
            unsigned vis_index = tree_add_vis_node(state);
            if (vis_index == -1U) return -1;
            state->compiler->nodes.data[vis_index].data.vis.child = state->compiler->nodes.len;
        }

        elem = &state->stack.data[depth];

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

unsigned compile_map(FILE* f, const struct map_compiler_opts* opts, struct map* map) {
    unsigned retval = 1;
    struct compiler state = {0};
    VLB_INIT(state.nodes, 256, err_mem(); goto reterr;);
    VLB_INIT(state.vis_nodes, 256, err_mem(); goto reterr;);
    VLB_INIT(state.vis_sibs, 1024, err_mem(); goto reterr;);
    VLB_INIT(state.geom_shapes, 256, err_mem(); goto reterr;);
    if (!opts) opts = &map_compiler_default_opts;

    while (1) {
        unsigned namelen;
        if (!parser_read_whitespace(f)) break;
        namelen = parser_read_name(f, state.text_buf, 256);
        if (!namelen) {
            err_want_name();
            goto reterr;
        }
        if (namelen == 4) {
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
                state.vis_max_depth = 0;
                while (1) {
                    if (size <= opts->min_vis_node_size) break;
                    size *= 0.5f;
                    ++state.vis_max_depth;
                }
                state.size_set = 1;
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

                tree.compiler = &state;
                tree.f = f;
                VLB_INIT(tree.stack, 256, err_mem(); goto reterr;);
                VLB_NEXTPTR(tree.stack, elem, 2, 1, VLB_FREE(tree.stack); err_mem(); goto reterr;);
                *elem = initelem;

                if (!parser_read_whitespace(f) || !(tree_ret = parser_read_name(f, state.text_buf, 32))) {
                    err_want_name();
                    goto reterr;
                }
                if (tree_ret == -1U) goto reterr;
                tree_ret = tree_read_node(&tree, state.text_buf);

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
                goto bad_direc;
            }
        } else if (namelen == 5) {
            if (!strcasecmp(state.text_buf, "shape")) {
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
            } else {
                goto bad_direc;
            }
        } else {
            bad_direc:
            fprintf(stderr, "Unknown directive '%s'\n", state.text_buf);
            goto reterr;
        }
    }

    /* Generate the sibling list for each vis node */
    /* It must be sorted from far to near */
    {
        unsigned i;
        struct compiler_vis_sib* sib_sort_data = malloc((state.vis_nodes.len - 1) * sizeof(*sib_sort_data));

        /* For each vis node */
        for (i = 0; i < state.vis_nodes.len; ++i) {
            unsigned index = state.vis_nodes.data[i];
            struct map_node* node = &state.nodes.data[index];
            unsigned j;

            /* Prepare for sorting by populating the sort data with the indices and distances of all the other vis nodes */
            {
                struct compiler_vis_sib* sib_sort_cur = sib_sort_data;
                for (j = 0; j < state.vis_nodes.len; ++j) {
                    unsigned sub_index = state.vis_nodes.data[j];
                    struct map_node* sub_node = &state.nodes.data[sub_index];

                    if (sub_index == index) continue; /* Make it so the vis node doesn't list itself as a sibling */

                    /* TODO: Occlusion culling */

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


    map->size = state.size;
    map->nodes = state.nodes.data;
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
