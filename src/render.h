#ifndef OCTEST_RENDER_H
#define OCTEST_RENDER_H

#include "util.h"
#include "map.h"

enum render_mode {
    RENDER_MODE_NORMAL,
    RENDER_MODE_OVERDRAW,
    RENDER_MODE_OVERDRAW_NO_DEPTH
};

void recalc_proj(const struct uvec2* size, float fov, float nearplane, float farplane);
void set_map(const struct map* map);
void set_render_mode(enum render_mode mode);
unsigned render(struct vec3* pos, struct vec3* rot);

#endif
