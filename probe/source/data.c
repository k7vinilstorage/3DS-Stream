/* Os dados de teste do romfs (tools/probe_clips.py descreve os formatos). */
#include "probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SETS 16

static uint8_t *clips_bin, *jpegs_bin, *audio_bin;
static item_set_t clips[MAX_SETS], jpegs[MAX_SETS];
static int nclips, njpegs;
static audio_clip_t audio;

static uint8_t *load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* 64 bytes: o mesmo alinhamento para todo mundo, e cabe o cache line */
    uint8_t *buf = n > 0 ? aligned_alloc(64, (n + 63) & ~63) : NULL;
    if (buf && fread(buf, 1, n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = n;
    return buf;
}

static uint32_t rd32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* "MAGIC123", u32 n, n x (name[24], u32 a, u32 count, u32 bytes, u32 size[count], dados, pad 4) */
static int parse_sets(uint8_t *bin, size_t size, const char *magic, item_set_t *sets, int jpeg)
{
    if (!bin || size < 12 || memcmp(bin, magic, 8))
        return -1;
    int n = rd32(bin + 8);
    if (n > MAX_SETS)
        n = MAX_SETS;
    size_t off = 12;
    for (int i = 0; i < n; i++) {
        if (off + 36 > size)
            return -1;
        item_set_t *s = &sets[i];
        memcpy(s->name, bin + off, 24);
        s->name[23] = 0;
        uint32_t a = rd32(bin + off + 24);
        s->count = rd32(bin + off + 28);
        uint32_t bytes = rd32(bin + off + 32);
        s->flags = jpeg ? 0 : a;
        s->quality = jpeg ? a : 0;
        if (off + 36 + 4 * (size_t)s->count + bytes > size)
            return -1;
        s->sizes = (const uint32_t *)(bin + off + 36);
        s->data = malloc(sizeof(*s->data) * s->count);
        const uint8_t *p = bin + off + 36 + 4 * s->count;
        for (int k = 0; k < s->count; k++) {
            s->data[k] = p;
            p += s->sizes[k];
        }
        off = (off + 36 + 4 * s->count + bytes + 3) & ~(size_t)3;
    }
    return n;
}

int data_load(void)
{
    size_t size;
    clips_bin = load("romfs:/clips.bin", &size);
    nclips = parse_sets(clips_bin, size, "H3DSCLP1", clips, 0);
    jpegs_bin = load("romfs:/jpegs.bin", &size);
    njpegs = parse_sets(jpegs_bin, size, "J3DSJPG1", jpegs, 1);
    audio_bin = load("romfs:/audio.bin", &size);
    if (audio_bin && size >= 24 && !memcmp(audio_bin, "A3DSIMA1", 8)) {
        audio.rate = rd32(audio_bin + 8);
        audio.channels = audio_bin[12] | audio_bin[13] << 8;
        audio.samples = audio_bin[14] | audio_bin[15] << 8;
        audio.block_bytes = rd32(audio_bin + 16);
        audio.blocks = rd32(audio_bin + 20);
        audio.data = audio_bin + 24;
        if (24 + (size_t)audio.block_bytes * audio.blocks > size)
            audio.blocks = 0;
    }
    return nclips > 0 && njpegs > 0 && audio.blocks > 0 ? 0 : -1;
}

int data_clips(const item_set_t **out)
{
    *out = clips;
    return nclips < 0 ? 0 : nclips;
}

int data_jpegs(const item_set_t **out)
{
    *out = jpegs;
    return njpegs < 0 ? 0 : njpegs;
}

const audio_clip_t *data_audio(void)
{
    return audio.blocks ? &audio : NULL;
}
