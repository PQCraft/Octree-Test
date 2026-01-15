#include "util.h"

#include <time.h>
#include <math.h>

unsigned long gettime_us(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec * 1000000 + time.tv_nsec / 1000;
}
void wait_us(unsigned long d) {
    struct timespec dts;
    dts.tv_sec = d / 1000000;
    dts.tv_nsec = (d % 1000000) * 1000;
    nanosleep(&dts, NULL);
}

float fwrap(float n, float d) {
    float tmp = n - (int)(n / d) * d;
    if (tmp < 0.0f) tmp += d;
    return tmp;
}

float vec3_dist(const struct vec3* v1, const struct vec3* v2) {
    struct vec3 v;
    v.x = v2->x - v1->x;
    v.y = v2->y - v1->y;
    v.z = v2->z - v1->z;
    return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
float vec3_dist_from_zero(const struct vec3* v) {
    return sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
}
void vec3_normalize(const struct vec3* vin, struct vec3* vout) {
    float dist = vec3_dist_from_zero(vin);
    if (dist >= 1e-6f) {
        vout->x = vin->x / dist;
        vout->y = vin->y / dist;
        vout->z = vin->z / dist;
    } else {
        *vout = *vin;
    }
}

unsigned point_is_inside_box(const struct vec3* p, const struct vec3* box_min, const struct vec3* box_max) {
    return (
        p->x >= box_min->x && p->x < box_max->x &&
        p->y >= box_min->y && p->y < box_max->y &&
        p->z >= box_min->z && p->z < box_max->z
    );
}
