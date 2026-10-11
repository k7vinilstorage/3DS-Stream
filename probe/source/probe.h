/*
 * 3DS-Stream probe: mede no hardware o que o plano do port depende
 * (decoder H.264 MVD, JPEG por software + Y2R, Wi-Fi, NDSP). Ver docs/Probe.md.
 */
#ifndef PROBE_H
#define PROBE_H

#include <3ds.h>
#include <stdint.h>

#define PROBE_VERSION "0.4"
#define PROBE_DIR "sdmc:/3ds/3ds-stream"

/* Tamanho do stream no 3DS: a tela de cima, 1:1. */
#define VID_W 400
#define VID_H 240

/* ---- relógio ---- */
static inline uint64_t now_us(void)
{
    return svcGetSystemTick() / (uint64_t)CPU_TICKS_PER_USEC;
}

/* ---- relatório (report.c): tela de baixo + PROBE_DIR/probe_result.txt ---- */
void report_init(void);
void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void report_save(void);
/* Outro arquivo (o padrão é PROBE_DIR/probe_result.txt). */
void report_set_file(const char *path);
/* Só na tela (progresso), não vai para o arquivo. */
void status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Passos arriscados: o nome vai para current_step.txt antes de começar. Se o
 * 3DS travar, na próxima vez o passo vai para crashed.txt e é pulado.
 * step_begin devolve 0 se o passo deve ser pulado. */
void steps_init(void);
int step_begin(const char *name);
void step_end(void);

/* ---- dados do romfs (data.c), gerados por tools/probe_clips.py ---- */
#define CLIP_COPIES 0x1 /* pacote = AUD + frame + 2 x (AUD + cópia) */
#define CLIP_INTRA 0x2  /* todo frame IDR */

typedef struct {
    char name[24];
    uint32_t flags;
    int count;              /* pacotes (H.264) ou frames (JPEG) */
    uint32_t quality;       /* só JPEG */
    const uint32_t *sizes;
    const uint8_t **data;   /* início de cada pacote/frame */
} item_set_t;

int data_load(void);
int data_clips(const item_set_t **out);
int data_jpegs(const item_set_t **out);
/* Áudio: blocos IMA ADPCM de 20 ms, como o servidor manda. */
typedef struct {
    uint32_t rate;
    int channels, samples, block_bytes, blocks;
    const uint8_t *data;
} audio_clip_t;
const audio_clip_t *data_audio(void);

/* ---- imagem ---- */
/* Lê o número do frame (8 blocos de 32x32 no canto, branco = 1). luma(x, y)
 * vem de um buffer RGB565 ou de um plano Y. -1 se algum bloco for ambíguo. */
int marker_rgb565(const uint16_t *img, int stride, int transposed);
int marker_y(const uint8_t *y, int stride);
/* Copia uma imagem RGB565 400x240 para a tela de cima (girada, como o framebuffer quer). */
void screen_show565(const uint16_t *img, int stride);

/* JPEG -> tela de cima (jpeg_test.c; IDCT rápida). 0 se ok. */
int jpeg_show(const uint8_t *jpg, uint32_t size, int *w, int *h);
int jpeg_size(const uint8_t *jpg, uint32_t size, int *w, int *h);

/* ---- os passos ---- */
void test_sysinfo(void);
void test_jpeg(void);
void test_audio(void);
void test_net(void);
void test_mvd(void);

extern bool g_new3ds;

#endif
