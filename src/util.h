#ifndef OCTEST_UTIL_H
#define OCTEST_UTIL_H

struct uvec2 {
    unsigned x;
    unsigned y;
};
struct vec3 {
    float x;
    float y;
    float z;
};

unsigned long gettime_us(void);
void wait_us(unsigned long us);

float fwrap(float n, float d);

float vec3_dist(const struct vec3* v1, const struct vec3* v2);
float vec3_dist_from_zero(const struct vec3* v);
void vec3_normalize(const struct vec3* vin, struct vec3* vout);

unsigned point_is_inside_box(const struct vec3* p, const struct vec3* box_min, const struct vec3* box_max);

#define PI_DBL (3.141592653589793238462643383279502884e0)
#define PI_FLT (3.141592653589793238462643383279502884e0f)

#define DEGTORAD_DBL(x) ((x) * PI_DBL / 180.0)
#define DEGTORAD_FLT(x) ((x) * PI_FLT / 180.0f)

#endif
