#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include "render.h"
#include "crc.h"

#include <math.h>
#include <stdio.h>

static enum render_mode mode = RENDER_MODE_NORMAL;
static float projmat[4][4] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, -1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f}
};
static float viewmat[4][4] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f}
};
static const struct map* map;
static struct {
    struct map_node* ptr;
    struct vec3 min;      /* Smallest coord */
    struct vec3 max;      /* Largest coord */
} cur_vis_node;

static void calc_view_mat(struct vec3* pos, struct vec3* rot, float mat[4][4]);

static struct map_node* find_vis_node(const struct map* map, struct vec3* pos) {
    struct map_node* node = &map->nodes[0];
    while (1) {
        if (node->type == MAP_NODE_PARENT) {
            node = &map->nodes[node->data.parent.children[
                (pos->x < node->pos.x) |
                ((pos->y < node->pos.y) << 2) |
                ((pos->z < node->pos.z) << 1)
            ]];
        } else if (node->type == MAP_NODE_VIS) {
            return node;
        } else {
            fputs("Expected node type of PARENT or VIS\n", stderr);
        }
    }
}

void set_render_mode(enum render_mode in) {
    mode = in;
}

/* For px, py, and pz, + means "X/Y/Z is positive", - means "X/Y/Z is negative" */
#define RENDER_NODE_VERT(sx, sy, sz) do {\
    unsigned index = (sx 1 < 0) | ((sy 1 < 0) << 2) | ((sz 1 < 0) << 1);\
    glVertex3f(\
        node->pos.x + shape->points[index].x * offset,\
        node->pos.y + shape->points[index].y * offset,\
        node->pos.z + shape->points[index].z * offset\
    );\
} while (0)
#define RENDER_NODE_COLOR(mul) glColor3f(color[0] * mul, color[1] * mul, color[2] * mul)
static unsigned render_node(const struct map* map, struct map_node* node, struct vec3* pos) {
    if (node->type == MAP_NODE_PARENT) {
        unsigned xor_mask = (pos->x < node->pos.x) | ((pos->y < node->pos.y) << 2) | ((pos->z < node->pos.z) << 1);
        unsigned i;
        for (i = 0; i < 8; ++i) {
            unsigned child = node->data.parent.children[i ^ xor_mask];
            if (child == -1U) continue;
            if (!render_node(map, &map->nodes[child], pos)) return 0;
        }
    } else if (node->type == MAP_NODE_GEOM) {
        struct map_node_geom_shape* shape = &map->geom_shapes[node->data.geom.shape];
        float offset = node->size * 0.5f;

        /* Generate a color off of the index */
        unsigned index;
        unsigned hash;
        static const float mul = 1.0f / 255.0f;
        float color[3];
        switch (mode) {
            case RENDER_MODE_NORMAL:
                index = (node - map->nodes);
                hash = crc32(&index, sizeof(index));
                color[0] = (((hash >> 16) & 255) | 64) * mul;
                color[1] = (((hash >> 8) & 255) | 64) * mul;
                color[2] = ((hash & 255) | 64) * mul;
                break;
            case RENDER_MODE_OVERDRAW:
            case RENDER_MODE_OVERDRAW_NO_DEPTH:
                glColor4f(1.0f, 0.0f, 0.0f, 0.05f);
                break;
        }

        glBegin(GL_QUADS);
            /* Right face */
            if (mode == RENDER_MODE_NORMAL) RENDER_NODE_COLOR(0.8f);
            RENDER_NODE_VERT(+, +, +);
            RENDER_NODE_VERT(+, +, -);
            RENDER_NODE_VERT(+, -, -);
            RENDER_NODE_VERT(+, -, +);
            /* Left face */
            if (mode == RENDER_MODE_NORMAL) RENDER_NODE_COLOR(0.7f);
            RENDER_NODE_VERT(-, +, +);
            RENDER_NODE_VERT(-, -, +);
            RENDER_NODE_VERT(-, -, -);
            RENDER_NODE_VERT(-, +, -);
            /* Top face */
            if (mode == RENDER_MODE_NORMAL) RENDER_NODE_COLOR(1.0f);
            RENDER_NODE_VERT(+, +, +);
            RENDER_NODE_VERT(-, +, +);
            RENDER_NODE_VERT(-, +, -);
            RENDER_NODE_VERT(+, +, -);
            /* Bottom face */
            if (mode == RENDER_MODE_NORMAL) RENDER_NODE_COLOR(0.5f);
            RENDER_NODE_VERT(+, -, +);
            RENDER_NODE_VERT(+, -, -);
            RENDER_NODE_VERT(-, -, -);
            RENDER_NODE_VERT(-, -, +);
            /* Front face */
            if (mode == RENDER_MODE_NORMAL) RENDER_NODE_COLOR(0.9f);
            RENDER_NODE_VERT(+, +, +);
            RENDER_NODE_VERT(+, -, +);
            RENDER_NODE_VERT(-, -, +);
            RENDER_NODE_VERT(-, +, +);
            /* Back face */
            if (mode == RENDER_MODE_NORMAL) RENDER_NODE_COLOR(0.6f);
            RENDER_NODE_VERT(+, +, -);
            RENDER_NODE_VERT(-, +, -);
            RENDER_NODE_VERT(-, -, -);
            RENDER_NODE_VERT(+, -, -);
        glEnd();
    } else {
        printf("[%d]\n", node->type);
        fputs("Expected node type of PARENT or GEOM\n", stderr);
        return 0;
    }
    return 1;
}

unsigned render(struct vec3* pos, struct vec3* rot) {
    unsigned child;
    if (!cur_vis_node.ptr || !point_is_inside_box(pos, &cur_vis_node.min, &cur_vis_node.max)) {
        float offset;
        cur_vis_node.ptr = find_vis_node(map, pos);
        if (!cur_vis_node.ptr) return 0;
        offset = cur_vis_node.ptr->size * 0.5f;
        cur_vis_node.min.x = cur_vis_node.ptr->pos.x - offset;
        cur_vis_node.min.y = cur_vis_node.ptr->pos.y - offset;
        cur_vis_node.min.z = cur_vis_node.ptr->pos.z - offset;
        cur_vis_node.max.x = cur_vis_node.ptr->pos.x + offset;
        cur_vis_node.max.y = cur_vis_node.ptr->pos.y + offset;
        cur_vis_node.max.z = cur_vis_node.ptr->pos.z + offset;
    }

    glEnable(GL_CULL_FACE);
    switch (mode) {
        case RENDER_MODE_NORMAL:
            glClearColor(0.0f, 0.0f, 0.1f, 1.0f);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            break;
        case RENDER_MODE_OVERDRAW:
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case RENDER_MODE_OVERDRAW_NO_DEPTH:
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
    }

    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    calc_view_mat(pos, rot, viewmat);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf((float*)projmat);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf((float*)viewmat);

    child = cur_vis_node.ptr->data.vis.child;
    if (child != -1U && !render_node(map, &map->nodes[child], pos)) return 0;
    {
        unsigned* siblings = map->vis_sibs + cur_vis_node.ptr->data.vis.first_sibling;
        unsigned i;
        for (i = 0; i < cur_vis_node.ptr->data.vis.sibling_count; ++i) {
            struct map_node* vis_node = &map->nodes[siblings[i]];
            child = vis_node->data.vis.child;
            if (child != -1U) {
                /* TODO: Frustum culling */
                if (!render_node(map, &map->nodes[child], pos)) return 0;
            }
        }
    }

    glFlush();

    return 1;
}

void set_map(const struct map* in) {
    map = in;
    cur_vis_node.ptr = NULL;
}

static void calc_proj_mat(float aspect, float fov, float nearplane, float farplane, float mat[4][4]) {
    float tmp1 = 1.0f / (float)tan(DEGTORAD_FLT(fov) * 0.5f);
    float tmp2 = 1.0f / (nearplane - farplane);
    mat[0][0] = -(tmp1 / aspect);
    mat[1][1] = tmp1;
    mat[2][2] = (nearplane + farplane) * tmp2;
    mat[3][2] = 2.0f * nearplane * farplane * tmp2;
}

static void calc_view_mat(struct vec3* pos, struct vec3* rot, float mat[4][4]) {
    float up[3];
    float front[3];
    {
        float radx = DEGTORAD_FLT(-rot->x);
        float rady = DEGTORAD_FLT(-rot->y);
        float radz = DEGTORAD_FLT(-rot->z);
        float sinx = sin(radx);
        float siny = sin(rady);
        float sinz = sin(radz);
        float cosx = cos(radx);
        float cosy = cos(rady);
        float cosz = cos(radz);
        up[0] = -sinx * siny * cosz - cosy * sinz;
        up[1] = cosx * cosz;
        up[2] = sinx * cosy * cosz - siny * sinz;
        front[0] = cosx * -siny;
        front[1] = -sinx;
        front[2] = cosx * cosy;
    }
    mat[0][0] = front[1] * up[2] - front[2] * up[1];
    mat[1][0] = front[2] * up[0] - front[0] * up[2];
    mat[2][0] = front[0] * up[1] - front[1] * up[0];
    mat[3][0] = -(mat[0][0] * pos->x + mat[1][0] * pos->y + mat[2][0] * pos->z);
    mat[0][1] = up[0];
    mat[1][1] = up[1];
    mat[2][1] = up[2];
    mat[3][1] = -(up[0] * pos->x + up[1] * pos->y + up[2] * pos->z);
    mat[0][2] = -front[0];
    mat[1][2] = -front[1];
    mat[2][2] = -front[2];
    mat[3][2] = front[0] * pos->x + front[1] * pos->y + front[2] * pos->z;
}

void recalc_proj(const struct uvec2* size, float fov, float nearplane, float farplane) {
    glViewport(0, 0, size->x, size->y);
    calc_proj_mat((float)size->x / size->y, fov, nearplane, farplane, projmat);
}
