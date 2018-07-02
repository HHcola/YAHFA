//
// Created by liuruikai756 on 05/07/2017.
//
#include <sys/mman.h>
#include <string.h>

#include "common.h"
#include "env.h"
#include "trampoline.h"

static unsigned char *trampolineCode; // place where trampolines are saved
static unsigned int trampolineSize; // trampoline size required for each hook

unsigned int hookCap = 0;
unsigned int hookCount = 0;

// trampoline:
// 1. set eax/r0/x0 to the hook ArtMethod addr
// 2. jump into its entry point
#if defined(__i386__)
// b8 78 56 34 12 ; mov eax, 0x12345678 (addr of the hook method)
// ff 70 20 ; push dword [eax + 0x20]
// c3 ; ret
unsigned char trampoline[] = {
//        0xb8, 0x21, 0x43, 0x65, 0x87,
//        0x66, 0xc7, 0x40, 0x12, 0x00, 0x00,
        0xb8, 0x78, 0x56, 0x34, 0x12,
        0xff, 0x70, 0x20,
        0xc3
};

#elif defined(__arm__)
// 00 00 9F E5 ; ldr r0, [pc, #0]
// 20 F0 90 E5 ; ldr pc, [r0, 0x20]
// 78 56 34 12 ; 0x12345678 (addr of the hook method)
unsigned char trampoline[] = {
//        0x14, 0x00, 0x9f, 0xe5,
//        0x04, 0x40, 0x2d, 0xe5,
//        0x00, 0x40, 0xa0, 0xe3,
//        0xb2, 0x41, 0xc0, 0xe1,
//        0x04, 0x40, 0x9d, 0xe4,
        0x00, 0x00, 0x9f, 0xe5,
        0x20, 0xf0, 0x90, 0xe5,
        0x78, 0x56, 0x34, 0x12
};

#elif defined(__aarch64__)
// 60 00 00 58 ; ldr x0, 12
// 10 18 40 f9 ; ldr x16, [x0, #48]
// 00 02 1f d6 ; br x16
// 78 56 34 12
// 89 67 45 23 ; 0x2345678912345678 (addr of the hook method)
unsigned char trampoline[] = {
//        0xa0, 0x00, 0x00, 0x58,
//        0x1f, 0x24, 0x00, 0x79,
        0x60, 0x00, 0x00, 0x58,
        0x10, 0x18, 0x40, 0xf9,
        0x00, 0x02, 0x1f, 0xd6,
//        0x89, 0x67, 0x45, 0x23,
//        0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12,
        0x89, 0x67, 0x45, 0x23
};
#endif
static unsigned int trampolineSize = roundUpToPtrSize(sizeof(trampoline));

void *genTrampoline(void *hookMethod, void *backupMethod) {
    void *targetAddr;

    targetAddr = trampolineCode + trampolineSize*hookCount;
    memcpy(targetAddr, trampoline, sizeof(trampoline)); // do not use trampolineSize since it's a rounded size

    // replace with the actual ArtMethod addr
#if defined(__i386__)
    memcpy(targetAddr+1, &hookMethod, pointer_size);

#elif defined(__arm__)
    memcpy(targetAddr+8, &hookMethod, pointer_size);

#elif defined(__aarch64__)
    memcpy(targetAddr+12, &hookMethod, pointer_size);
#endif

    return targetAddr;
}

void setupTrampoline() {
#if defined(__i386__)
    //    trampoline1[8] = (unsigned char)OFFSET_hotness_count_in_ArtMethod;
    trampoline[7] = (unsigned char)OFFSET_entry_point_from_quick_compiled_code_in_ArtMethod;
#elif defined(__arm__)
    trampoline[4] = (unsigned char)OFFSET_entry_point_from_quick_compiled_code_in_ArtMethod;
#elif defined(__aarch64__)
    switch (SDKVersion) {
        case ANDROID_O2:
        case ANDROID_O:
            trampoline[5] = '\x14'; //10 14 40 f9 ; ldr x16, [x0, #40]
            break;
        case ANDROID_N2:
        case ANDROID_N:
            trampoline[5] = '\x18'; //10 18 40 f9 ; ldr x16, [x0, #48]
            break;
        case ANDROID_M:
            trampoline[5] = '\x18'; //10 18 40 f9 ; ldr x16, [x0, #48]
            break;
        case ANDROID_L2:
            trampoline[5] = '\x1c'; //10 1c 40 f9 ; ldr x16, [x0, #56]
            break;
        case ANDROID_L:
            trampoline[5] = '\x1c'; //10 14 40 f9 ; ldr x16, [x0, #40]
            break;
        default:
            break;
    }
#endif
}

int doInitHookCap(unsigned int cap) {
    if(cap == 0) {
        LOGE("invalid capacity: %d", cap);
        return 1;
    }
    if(hookCap) {
        LOGW("allocating new space for trampoline code");
    }
    unsigned int allSize = trampolineSize*cap;
    unsigned char *buf = mmap(NULL, allSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
    if(buf == MAP_FAILED) {
        LOGE("mmap failed");
        return 1;
    }
    hookCap = cap;
    hookCount = 0;
    trampolineCode = buf;
    return 0;
}
