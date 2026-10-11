/*
 * Decoder H.264 de hardware do New 3DS (serviço MVD), com os pacotes exatos
 * que o servidor PSPStream manda (tools/probe_clips.py usa o encoder dele).
 *
 * Responde, para cada clipe:
 *  1. o MVD abre e decodifica, chamado de um homebrew?
 *  2. quanto leva por frame (processar as NALs + render para RGB565)?
 *  3. ele segura frames? Cada frame leva o próprio número em 8 blocos; depois
 *     do render lemos qual saiu. No PSP, o decoder segurava 2 frames, e cada
 *     pacote passou a levar 2 cópias do frame para empurrá-lo (Design
 *     Decisions do PSP-Stream). Se aqui o atraso for 0, o 3DS não precisa das
 *     cópias (capacidade "sem cópias" no protocolo v6).
 *  4. um IDR no meio de uma sequência de P, depois de uma perda (no PSP sem
 *     Stop isso desligava o console), funciona sem reiniciar o decoder?
 *
 * As NALs vão uma a uma, cada uma com o prefixo 00 00 01, como no exemplo
 * mvd do devkitPro. O render vai depois de cada NAL de slice.
 */
#include "probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IN_SIZE 0x100000
#define OUT_SIZE (VID_W * VID_H * 2)
#define DUMPS 3

static uint8_t *inbuf, *outbuf;

typedef struct {
    const uint8_t *p;
    int len;
    int type;
} nal_t;

/* Divide um trecho Annex B em NALs (sem o start code). */
static int split_nals(const uint8_t *d, int n, nal_t *out, int max)
{
    int count = 0, i = 0, start = -1;
    while (i + 3 <= n) {
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
            if (start >= 0 && count < max) {
                int end = i;
                while (end > start && d[end - 1] == 0) /* o 00 a mais de um start code de 4 bytes */
                    end--;
                out[count].p = d + start;
                out[count].len = end - start;
                out[count].type = d[start] & 0x1F;
                count++;
            }
            i += 3;
            start = i;
        } else {
            i++;
        }
    }
    if (start >= 0 && start < n && count < max) {
        out[count].p = d + start;
        out[count].len = n - start;
        out[count].type = d[start] & 0x1F;
        count++;
    }
    return count;
}

typedef struct {
    const char *step;   /* nome do passo (crashed.txt) */
    const char *clip;   /* clipe do clips.bin */
    int with_aud;       /* manda também as NALs AUD (tipo 9) */
    int skip_from, skip_to; /* pacotes pulados (perda simulada); -1 = nenhum */
} mvd_case_t;

static const mvd_case_t cases[] = {
    {"mvd_intra", "oh_intra", 0, -1, -1},
    {"mvd_p1", "oh_p1", 0, -1, -1},
    {"mvd_p3", "oh_p3", 0, -1, -1},
    {"mvd_p3_aud", "oh_p3", 1, -1, -1},
    {"mvd_idr10", "oh_idr10", 0, -1, -1},
    /* perde 15-19 e volta no IDR do 20, como o stream depois de pedir um IDR */
    {"mvd_idr10_loss", "oh_idr10", 0, 15, 19},
};

/* Contagem de códigos devolvidos pelo ProcessVideoFrame. */
typedef struct {
    Result code;
    int count;
} code_count_t;

static void count_code(code_count_t *c, int max, Result r)
{
    for (int i = 0; i < max; i++) {
        if (c[i].count && c[i].code == r) {
            c[i].count++;
            return;
        }
        if (!c[i].count) {
            c[i].code = r;
            c[i].count = 1;
            return;
        }
    }
}

static const char *code_name(Result r)
{
    switch ((u32)r) {
    case MVD_STATUS_OK: return "OK";
    case MVD_STATUS_PARAMSET: return "PARAMSET";
    case MVD_STATUS_BUSY: return "BUSY";
    case MVD_STATUS_FRAMEREADY: return "FRAMEREADY";
    case MVD_STATUS_INCOMPLETEPROCESSING: return "INCOMPLETE";
    case MVD_STATUS_NALUPROCFLAG: return "NALUPROCFLAG";
    default: return NULL;
    }
}

static void dump(int n)
{
    char path[64];
    snprintf(path, sizeof(path), PROBE_DIR "/mvd_dump_%d.bin", n);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(outbuf, 1, OUT_SIZE, f);
        fclose(f);
    }
}

/* Qual número saiu no último render: tenta o layout normal e o transposto. */
static int read_output(int *layout)
{
    int v = marker_rgb565((uint16_t *)outbuf, VID_W, 0);
    if (v >= 0) {
        *layout = 1;
        return v;
    }
    v = marker_rgb565((uint16_t *)outbuf, VID_H, 1);
    if (v >= 0)
        *layout = 2;
    return v;
}

static void run_case(const mvd_case_t *c, const item_set_t *clip, int *dumps_left)
{
    if (!step_begin(c->step))
        return;
    status("  %s...", c->step);

    Result r = mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264, MVD_OUTPUT_RGB565, MVD_DEFAULT_WORKBUF_SIZE, NULL);
    if (R_FAILED(r)) {
        say("  %-15s mvdstdInit failed: 0x%08lX", c->step, r);
        step_end();
        return;
    }
    MVDSTD_Config cfg;
    mvdstdGenerateDefaultConfig(&cfg, VID_W, VID_H, VID_W, VID_H, NULL, (u32 *)outbuf, (u32 *)outbuf);

    code_count_t codes[8];
    memset(codes, 0, sizeof(codes));
    /* atraso = pacote - número que saiu. own: logo depois do slice do próprio
     * frame; last: depois do pacote inteiro (com as cópias, o que seria exibido). */
    int lag_own[5] = {0}, lag_last[5] = {0}, unreadable = 0, render_fail = 0, no_render = 0, layout = 0;
    int after_loss_ok = -1; /* idr10_loss: o 1º frame lido certo depois da perda */
    uint64_t t_proc = 0, t_render = 0, max_frame = 0;
    int frames = 0, err = 0;
    Result first_err = 0;
    const char *err_where = "";

    for (int k = 0; k < clip->count && !err; k++) {
        if (k >= c->skip_from && k <= c->skip_to)
            continue;
        nal_t nals[16];
        int nn = split_nals(clip->data[k], clip->sizes[k], nals, 16);
        int slices = 0, shown = -2, rendered = 0;
        uint64_t f0 = now_us();
        for (int i = 0; i < nn && !err; i++) {
            if (nals[i].type == 9 && !c->with_aud)
                continue;
            if (nals[i].len + 3 > IN_SIZE) {
                err = 1;
                err_where = "NAL too large";
                break;
            }
            inbuf[0] = 0;
            inbuf[1] = 0;
            inbuf[2] = 1;
            memcpy(inbuf + 3, nals[i].p, nals[i].len);
            GSPGPU_FlushDataCache(inbuf, nals[i].len + 3);
            MVDSTD_ProcessNALUnitOut out;
            uint64_t t0 = now_us();
            r = mvdstdProcessVideoFrame(inbuf, nals[i].len + 3, 0, &out);
            t_proc += now_us() - t0;
            count_code(codes, 8, r);
            if (!MVD_CHECKNALUPROC_SUCCESS(r)) {
                err = 1;
                first_err = r;
                err_where = "ProcessVideoFrame";
                break;
            }
            if (nals[i].type != 1 && nals[i].type != 5)
                continue;
            /* depois de cada slice: render e qual frame saiu. Como no exemplo
             * mvd do devkitPro, não depois de PARAMSET/INCOMPLETE: o render
             * com espera poderia não voltar. */
            if (r == MVD_STATUS_PARAMSET || r == MVD_STATUS_INCOMPLETEPROCESSING) {
                no_render++;
                continue;
            }
            t0 = now_us();
            r = mvdstdRenderVideoFrame(&cfg, true);
            t_render += now_us() - t0;
            if (r != MVD_STATUS_OK) {
                render_fail++;
                if (!first_err)
                    first_err = r;
                continue;
            }
            GSPGPU_InvalidateDataCache(outbuf, OUT_SIZE);
            rendered = 1;
            shown = read_output(&layout);
            if (*dumps_left > 0 && k < 3) {
                dump(DUMPS - *dumps_left);
                (*dumps_left)--;
            }
            if (slices++ == 0) {
                if (shown < 0)
                    unreadable++;
                else
                    lag_own[k - shown < 0 ? 4 : k - shown > 3 ? 3 : k - shown]++;
            }
        }
        uint64_t ft = now_us() - f0;
        if (ft > max_frame)
            max_frame = ft;
        frames++;
        if (shown >= 0)
            lag_last[k - shown < 0 ? 4 : k - shown > 3 ? 3 : k - shown]++;
        if (c->skip_from >= 0 && k > c->skip_to && after_loss_ok < 0 && shown == k)
            after_loss_ok = k;
        if (rendered)
            screen_show565((uint16_t *)outbuf, VID_W);
    }
    mvdstdExit();

    say("  %-15s %s: %d frames, %.2f ms/frame (process %.2f + render %.2f), max %.2f", c->step,
        err ? "ERROR" : "ok", frames, frames ? (t_proc + t_render) / 1000.0 / frames : 0,
        frames ? t_proc / 1000.0 / frames : 0, frames ? t_render / 1000.0 / frames : 0, max_frame / 1000.0);
    if (err)
        say("    stopped at %s: 0x%08lX", err_where, first_err);
    say("    lag after own slice: 0:%d 1:%d 2:%d 3+:%d ahead:%d unreadable:%d", lag_own[0], lag_own[1], lag_own[2],
        lag_own[3], lag_own[4], unreadable);
    if (clip->flags & CLIP_COPIES)
        say("    lag after packet:    0:%d 1:%d 2:%d 3+:%d ahead:%d", lag_last[0], lag_last[1], lag_last[2],
            lag_last[3], lag_last[4]);
    if (no_render)
        say("    %d slices with nothing to render (PARAMSET/INCOMPLETE)", no_render);
    if (render_fail)
        say("    render failed %d times (first 0x%08lX)", render_fail, first_err);
    if (layout == 2)
        say("    output is transposed (240x400)");
    if (c->skip_from >= 0)
        say("    after the loss: %s", after_loss_ok >= 0 ? "recovered on the IDR" : "NOT recovered");
    char line[160];
    int len = snprintf(line, sizeof(line), "    codes:");
    for (int i = 0; i < 8 && codes[i].count; i++) {
        const char *name = code_name(codes[i].code);
        if (name)
            len += snprintf(line + len, sizeof(line) - len, " %s=%d", name, codes[i].count);
        else
            len += snprintf(line + len, sizeof(line) - len, " 0x%08lX=%d", codes[i].code, codes[i].count);
        if (len >= (int)sizeof(line))
            break;
    }
    say("%s", line);
    step_end();
}

void test_mvd(void)
{
    say("");
    say("== H.264 hardware decoder (MVD, %dx%d) ==", VID_W, VID_H);
    if (!g_new3ds) {
        say("  Old 3DS / 2DS: no MVD, skipped");
        return;
    }
    osSetSpeedupEnable(true);
    inbuf = linearMemAlign(IN_SIZE, 0x40);
    outbuf = linearMemAlign(0x100000, 0x40);
    if (!inbuf || !outbuf) {
        say("  out of linear memory");
        goto out;
    }
    const item_set_t *clips;
    int n = data_clips(&clips);
    int dumps_left = DUMPS;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const item_set_t *clip = NULL;
        for (int k = 0; k < n; k++)
            if (!strcmp(clips[k].name, cases[i].clip))
                clip = &clips[k];
        if (!clip) {
            say("  %-15s clip %s missing from romfs", cases[i].step, cases[i].clip);
            continue;
        }
        run_case(&cases[i], clip, &dumps_left);
    }
out:
    if (inbuf)
        linearFree(inbuf);
    if (outbuf)
        linearFree(outbuf);
}
