#ifndef OCTEST_VLB_H
#define OCTEST_VLB_H

#include <stdlib.h>

#define VLB(T) {\
    T* data;\
    unsigned long len;\
    unsigned long size;\
}

#define VLB_OOM_NOP ;

#define VLB__EXP(VLB__b, VLB__en, VLB__ed, VLB__do, VLB__oom) do {\
    if ((VLB__b).len != (VLB__b).size) {\
        VLB__do;\
    } else {\
        void* VLB__ptr;\
        register unsigned long VLB__tmp = (VLB__b).size;\
        VLB__tmp = VLB__tmp * (VLB__en) / (VLB__ed);\
        if (VLB__tmp <= (VLB__b).size) VLB__tmp = (VLB__b).size + 1;\
        VLB__ptr = realloc((VLB__b).data, VLB__tmp * sizeof(*(VLB__b).data));\
        if (VLB__ptr) {(VLB__b).data = VLB__ptr; VLB__do; (VLB__b).size = VLB__tmp;}\
        else {VLB__oom}\
    }\
} while (0)
#define VLB__EXP_MULTI(VLB__b, VLB__l, VLB__en, VLB__ed, VLB__oom) do {\
    if ((VLB__l) > (VLB__b).size) {\
        void* VLB__ptr;\
        register unsigned long VLB__tmp = (VLB__b).size;\
        do {\
            register unsigned long VLB__old = VLB__tmp;\
            VLB__tmp = VLB__tmp * (VLB__en) / (VLB__ed);\
            if (VLB__tmp <= VLB__old) VLB__tmp = VLB__old + 1;\
        } while (VLB__tmp < (VLB__l));\
        VLB__ptr = realloc((VLB__b).data, VLB__tmp * sizeof(*(VLB__b).data));\
        if (VLB__ptr) {(VLB__b).data = VLB__ptr; (VLB__b).len = (VLB__l); (VLB__b).size = VLB__tmp;}\
        else {VLB__oom}\
    } else if ((VLB__l) > (VLB__b).len) {\
        (VLB__b).len = (VLB__l);\
    }\
} while (0)

#define VLB_INIT(VLB__b, VLB__sz, VLB__oom) do {\
    (VLB__b).data = malloc((VLB__sz) * sizeof(*(VLB__b).data));\
    if ((VLB__b).data || !(VLB__sz)) {\
        (VLB__b).len = 0;\
        (VLB__b).size = (VLB__sz);\
    } else {\
        VLB__oom\
    }\
} while (0)
#define VLB_ZINIT(VLB__b) do {\
    (VLB__b).data = NULL;\
    (VLB__b).len = 0;\
    (VLB__b).size = 0;\
} while (0)
#define VLB_FREE(b) free((b).data)

#define VLB_ADD(VLB__b, VLB__d, VLB__en, VLB__ed, VLB__oom) do {\
    VLB__EXP((VLB__b), (VLB__en), (VLB__ed), (VLB__b).data[(VLB__b).len++] = (VLB__d), VLB__oom);\
} while (0)
#define VLB_NEXTPTR(VLB__b, VLB__o, VLB__en, VLB__ed, VLB__oom) do {\
    VLB__EXP((VLB__b), (VLB__en), (VLB__ed), (VLB__o) = &(VLB__b).data[(VLB__b).len++], VLB__oom);\
} while (0)

#define VLB_EXPANDBY(VLB__b, VLB__a, VLB__en, VLB__ed, VLB__oom) do {\
    register unsigned long VLB__l = (VLB__b).len + (VLB__a);\
    VLB__EXP_MULTI((VLB__b), (VLB__l), (VLB__en), (VLB__ed), VLB__oom);\
} while (0)
#define VLB_EXPANDTO(VLB__b, VLB__l, VLB__en, VLB__ed, VLB__oom) do {\
    VLB__EXP_MULTI((VLB__b), (VLB__l), (VLB__en), (VLB__ed), VLB__oom);\
} while (0)
#define VLB_SHRINK(VLB__b, VLB__oom) do {\
    if ((VLB__b).len != (VLB__b).size) {\
        void* VLB__ptr = realloc((VLB__b).data, (VLB__b).len * sizeof(*(VLB__b).data));\
        if (VLB__ptr || !(VLB__b).len) {(VLB__b).data = VLB__ptr; (VLB__b).size = (VLB__b).len;}\
        else {VLB__oom}\
    }\
} while (0)

#endif
