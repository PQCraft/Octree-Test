#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <SDL2/SDL.h>

#include "util.h"
#include "renderer.h"
#include "compiler.h"

#include <math.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

static SDL_Window* window;
static struct uvec2 window_size = {800, 600};
static SDL_GLContext gl_ctx;

static float fov = 90.0f;
static float nearplane = 0.1f;
static float farplane = 100.0f;

static struct map map;

static void put_controls_text(void);

int main(int argc, char** argv) {
    int retval = 0;
    struct {
        unsigned move_forwards  : 1;
        unsigned move_backwards : 1;
        unsigned move_left      : 1;
        unsigned move_right     : 1;
        unsigned move_up        : 1;
        unsigned move_down      : 1;
        unsigned run            : 1;
        unsigned release_mouse  : 1;
    } actions = {0};
    static const float mouse_sensitivity = 15.0f / 100.0f;
    struct vec3 camera_pos = {0};
    struct vec3 camera_rot = {0};
    long unsigned last_frame_timestamp = gettime_us();
    const char* map_filename = (argc <= 1) ? "map.txt" : argv[1];

    /* Compile map */
    {
        FILE* f = fopen(map_filename, "r");
        int ok;
        if (!f) {
            fprintf(stderr, "Failed to open '%s': %s\n", map_filename, strerror(errno));
            return 1;
        }
        ok = compile_map(f, &map);
        fclose(f);
        if (!ok) {
            fputs("Failed to compile map\n", stderr);
            return 1;
        }
    }

    /* Init SDL2 */
    if (SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "Failed to init SDL: %s\n", SDL_GetError());
        return 1;
    }

    /* Stop SDL2 from disabling the compositor */
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");

    /* Request GL 1.1 */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    /* Create window */
    window = SDL_CreateWindow(
        "Octree Test",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        window_size.x, window_size.y,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL
    );
    if (!window) {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        retval = 1;
        goto longbreak_only_quit;
    }

    /* Create GL context */
    gl_ctx = SDL_GL_CreateContext(window);
    if (!gl_ctx) {
        fprintf(stderr, "Failed to create GL context: %s\n", SDL_GetError());
        retval = 1;
        goto longbreak_only_destroywindow;
    }
    SDL_GL_MakeCurrent(window, gl_ctx);
    /* and print info */
    printf(
        "OPENGL INFO:\n"
        "    OpenGL version: %s\n"
        "    Vendor string: %s\n"
        "    Renderer string: %s\n",
        (char*)glGetString(GL_VERSION),
        (char*)glGetString(GL_VENDOR),
        (char*)glGetString(GL_RENDERER)
    );
    put_controls_text();

    set_map(&map);

    /* Set up some SDL attribs */
    SDL_SetRelativeMouseMode(1);
    if (SDL_GL_SetSwapInterval(-1) == -1) SDL_GL_SetSwapInterval(1);
    /* and the renderer */
    recalc_proj(&window_size, fov, nearplane, farplane);

    /* Main loop */
    while (1) {
        long unsigned cur_frame_timestamp = gettime_us();
        long unsigned delta_us = cur_frame_timestamp - last_frame_timestamp;
        float delta = delta_us / 1000000.0;
        struct vec3 camera_movement = {0};
        float move_speed;
        static const float walk_speed = 2.0;
        static const float run_speed = 8.0;
        SDL_Event event;

        /* Handle events */
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT: goto longbreak;

                case SDL_KEYDOWN: {
                    switch (event.key.keysym.scancode) {
                        case SDL_SCANCODE_ESCAPE: goto longbreak;
                        case SDL_SCANCODE_M: {
                            if (event.key.repeat) break;
                            if (!actions.release_mouse) {
                                SDL_SetRelativeMouseMode(0);
                                actions.release_mouse = 1;
                            } else {
                                SDL_SetRelativeMouseMode(1);
                                actions.release_mouse = 0;
                            }
                        } break;
                        case SDL_SCANCODE_W: actions.move_forwards = 1;  break;
                        case SDL_SCANCODE_A: actions.move_left = 1;      break;
                        case SDL_SCANCODE_S: actions.move_backwards = 1; break;
                        case SDL_SCANCODE_D: actions.move_right = 1;     break;
                        case SDL_SCANCODE_SPACE: actions.move_up = 1;    break;
                        case SDL_SCANCODE_LSHIFT: actions.move_down = 1; break;
                        case SDL_SCANCODE_LCTRL: actions.run = 1;        break;
                        case SDL_SCANCODE_R: {
                            struct map new_map;
                            FILE* f;
                            int ok;
                            if (event.key.repeat) break;
                            f = fopen(map_filename, "r");
                            if (!f) {
                                fprintf(stderr, "Failed to open '%s': %s\n", map_filename, strerror(errno));
                                break;
                            }
                            ok = compile_map(f, &new_map);
                            fclose(f);
                            if (!ok) {
                                fputs("Failed to compile map\n", stderr);
                                break;
                            }
                            free_map(&map);
                            map = new_map;
                            set_map(&map);
                        } break;
                        case SDL_SCANCODE_1: set_render_mode(RENDER_MODE_NORMAL);            break;
                        case SDL_SCANCODE_2: set_render_mode(RENDER_MODE_OVERDRAW);          break;
                        case SDL_SCANCODE_3: set_render_mode(RENDER_MODE_OVERDRAW_NO_DEPTH); break;
                        default: break;
                    }
                } break;
                case SDL_KEYUP: {
                    switch (event.key.keysym.scancode) {
                        case SDL_SCANCODE_W: actions.move_forwards = 0;  break;
                        case SDL_SCANCODE_A: actions.move_left = 0;      break;
                        case SDL_SCANCODE_S: actions.move_backwards = 0; break;
                        case SDL_SCANCODE_D: actions.move_right = 0;     break;
                        case SDL_SCANCODE_SPACE: actions.move_up = 0;    break;
                        case SDL_SCANCODE_LSHIFT: actions.move_down = 0; break;
                        case SDL_SCANCODE_LCTRL: actions.run = 0;        break;
                        default: break;
                    }
                } break;
                case SDL_MOUSEMOTION: {
                    if (actions.release_mouse || event.motion.which == SDL_TOUCH_MOUSEID) break;
                    camera_rot.y += event.motion.xrel * mouse_sensitivity;
                    camera_rot.y = fwrap(camera_rot.y, 360.0f);
                    camera_rot.x -= event.motion.yrel * mouse_sensitivity;
                    if (camera_rot.x > 90.0f) camera_rot.x = 90.0f;
                    else if (camera_rot.x < -90.0f) camera_rot.x = -90.0f;
                } break;

                case SDL_WINDOWEVENT: {
                    switch (event.window.event) {
                        case SDL_WINDOWEVENT_RESIZED: {
                            window_size.x = event.window.data1;
                            window_size.y = event.window.data2;
                            recalc_proj(&window_size, fov, nearplane, farplane);
                        } break;
                        default: break;
                    }
                } break;

                default: break;
            }
        }

        /* Handle movement */
        if (actions.move_forwards) camera_movement.z += 1.0f;
        if (actions.move_backwards) camera_movement.z -= 1.0f;
        if (actions.move_left) camera_movement.x -= 1.0f;
        if (actions.move_right) camera_movement.x += 1.0f;
        if (actions.move_up) camera_movement.y += 1.0f;
        if (actions.move_down) camera_movement.y -= 1.0f;
        vec3_normalize(&camera_movement, &camera_movement);
        {
            float rot_y_rad = DEGTORAD_FLT(camera_rot.y), tmp;
            tmp = camera_movement.x * (float)cos(rot_y_rad) + camera_movement.z * (float)sin(rot_y_rad);
            camera_movement.z = camera_movement.z * (float)cos(rot_y_rad) - camera_movement.x * (float)sin(rot_y_rad);
            camera_movement.x = tmp;
        }
        move_speed = ((!actions.run) ? walk_speed : run_speed) * delta;
        camera_pos.x += camera_movement.x * move_speed;
        camera_pos.y += camera_movement.y * move_speed;
        camera_pos.z += camera_movement.z * move_speed;

        #if 0
        printf(
            "[%f %f %f][%f %f %f][%f -> %f] [%f %f %f] [%lu, %f]\n",
            camera_pos.x, camera_pos.y, camera_pos.z,
            camera_movement.x, camera_movement.y, camera_movement.z,
            ((!actions.run) ? walk_speed : run_speed), move_speed,
            camera_rot.x, camera_rot.y, camera_rot.z,
            delta_us, delta
        );
        #endif

        /* Render */
        if (!render(&camera_pos, &camera_rot)) {
            fputs("Rendering error\n", stderr);
            retval = 1;
            goto longbreak;
        }
        SDL_GL_SwapWindow(window);

        last_frame_timestamp = cur_frame_timestamp;
    }
    longbreak:

    SDL_SetRelativeMouseMode(0);

    SDL_GL_DeleteContext(gl_ctx);
    longbreak_only_destroywindow:
    SDL_DestroyWindow(window);
    longbreak_only_quit:
    SDL_Quit();

    return retval;
}

static void put_controls_text(void) {
    puts("CONTROLS:");
    puts("    Esc    - Exit");
    puts("    W      - Move forwards");
    puts("    A      - Strafe left");
    puts("    S      - Move backwards");
    puts("    D      - Strafe right");
    puts("    Space  - Move upwards");
    puts("    LShift - Move downwards");
    puts("    LCtrl  - Move faster");
    puts("    M      - Toggle mouse grab");
    puts("    R      - Reload map");
    puts("    1      - Render normal");
    puts("    2      - Render overdraw heatmap");
    puts("    3      - Render overdraw heatmap with depth test disabled");
}
