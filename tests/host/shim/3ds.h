/*
 * Testes no PC: o mínimo da libctru para compilar o probe com o gcc do PC.
 * Relógio e sockets são os do Linux; MVD é falso (tests/host/fake_mvd.c,
 * confere as NALs); Y2R não existe (o teste JPEG mede sem ele).
 */
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int64_t s64;
typedef int32_t Result;
typedef int Handle;
#define R_FAILED(r) ((r) < 0)

/* relógio: o "tick" é o microssegundo */
#define CPU_TICKS_PER_USEC 1
static inline uint64_t svcGetSystemTick(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000ull + t.tv_nsec / 1000;
}
static inline void svcSleepThread(int64_t ns)
{
    struct timespec t = {ns / 1000000000, ns % 1000000000};
    nanosleep(&t, 0);
}

/* rede: os sockets do Linux; o "3DS" está em 127.0.0.1 */
static inline Result socInit(u32 *buf, u32 size) { return 0; }
static inline Result socExit(void) { return 0; }
static inline int SOCU_GetIPInfo(struct in_addr *ip, struct in_addr *mask, struct in_addr *bc)
{
    ip->s_addr = inet_addr("127.0.0.1");
    mask->s_addr = inet_addr("255.0.0.0");
    bc->s_addr = inet_addr("127.255.255.255");
    return 0;
}

/* Wi-Fi: sempre conectado no PC */
static inline Result acInit(void) { return 0; }
static inline void acExit(void) {}
static inline Result ACU_GetWifiStatus(u32 *out) { *out = 1; return 0; }
#define R_SUCCEEDED(r) ((r) >= 0)

/* memória e cache */
static inline void *linearAlloc(size_t n) { return aligned_alloc(64, (n + 63) / 64 * 64); }
static inline void *linearMemAlign(size_t n, size_t a) { return aligned_alloc(a, (n + a - 1) / a * a); }
static inline void linearFree(void *p) { free(p); }
static inline void GSPGPU_FlushDataCache(const void *p, u32 n) {}
static inline void GSPGPU_InvalidateDataCache(const void *p, u32 n) {}
static inline void osSetSpeedupEnable(bool on) {}

/* tela: um framebuffer de mentira (tests/host/common.c) */
#define GFX_TOP 0
#define GFX_LEFT 0
extern uint16_t fake_fb[400 * 240];
static inline uint8_t *gfxGetFramebuffer(int screen, int side, u16 *w, u16 *h) { return (uint8_t *)fake_fb; }
static inline void gfxFlushBuffers(void) {}
static inline void gfxSwapBuffers(void) {}

/* MVD (tests/host/fake_mvd.c) */
typedef struct { int unused; } MVDSTD_Config;
typedef struct { u32 end_vaddr, end_physaddr, remaining_size; } MVDSTD_ProcessNALUnitOut;
typedef enum { MVDMODE_COLORFORMATCONV, MVDMODE_VIDEOPROCESSING } MVDSTD_Mode;
#define MVD_INPUT_H264 0x00020001
#define MVD_OUTPUT_RGB565 0x00040004
#define MVD_DEFAULT_WORKBUF_SIZE 0x9006C8
#define MVD_STATUS_OK 0x17000
#define MVD_STATUS_PARAMSET 0x17001
#define MVD_STATUS_BUSY 0x17002
#define MVD_STATUS_FRAMEREADY 0x17003
#define MVD_STATUS_INCOMPLETEPROCESSING 0x17004
#define MVD_STATUS_NALUPROCFLAG 0x17007
#define MVD_CHECKNALUPROC_SUCCESS(x) \
    (x == MVD_STATUS_OK || x == MVD_STATUS_PARAMSET || x == MVD_STATUS_FRAMEREADY || \
     x == MVD_STATUS_INCOMPLETEPROCESSING || x == MVD_STATUS_NALUPROCFLAG)
Result mvdstdInit(MVDSTD_Mode mode, int in, int out, u32 size, void *init);
void mvdstdExit(void);
void mvdstdGenerateDefaultConfig(MVDSTD_Config *c, u32 iw, u32 ih, u32 ow, u32 oh, u32 *cc, u32 *o0, u32 *o1);
Result mvdstdProcessVideoFrame(void *buf, size_t size, u32 flag, MVDSTD_ProcessNALUnitOut *out);
Result mvdstdRenderVideoFrame(MVDSTD_Config *c, bool wait);

/* Y2R: sem ele no PC */
typedef struct {
    int input_format, output_format, rotation, block_alignment;
    short input_line_width, input_lines;
    int standard_coefficient;
    u8 unused;
    u16 alpha;
} Y2RU_ConversionParams;
enum { INPUT_YUV420_INDIV_8 = 1, OUTPUT_RGB_16_565 = 3, ROTATION_NONE = 0, BLOCK_LINE = 0, COEFFICIENT_ITU_R_BT_601 = 0 };
static inline Result y2rInit(void) { return -1; }
static inline void y2rExit(void) {}
static inline Result Y2RU_SetConversionParams(const Y2RU_ConversionParams *p) { return -1; }
static inline Result Y2RU_SetTransferEndInterrupt(bool on) { return -1; }
static inline Result Y2RU_GetTransferEndEvent(Handle *h) { return -1; }
static inline Result Y2RU_SetSendingY(const void *b, u32 s, short u, short g) { return -1; }
static inline Result Y2RU_SetSendingU(const void *b, u32 s, short u, short g) { return -1; }
static inline Result Y2RU_SetSendingV(const void *b, u32 s, short u, short g) { return -1; }
static inline Result Y2RU_SetReceiving(void *b, u32 s, short u, short g) { return -1; }
static inline Result Y2RU_StartConversion(void) { return -1; }
static inline Result Y2RU_StopConversion(void) { return -1; }
static inline Result svcWaitSynchronization(Handle h, s64 ns) { return -1; }
