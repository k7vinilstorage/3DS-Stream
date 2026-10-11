/*
 * JPEG por software (libjpeg-turbo) a 400x240: o caminho do Old 3DS, que não
 * tem H.264 em hardware. Três jeitos, cada um nas qualidades 50-95:
 *
 *   rgb565  libjpeg direto para RGB565 (IDCT exata, upsampling suave);
 *   fast    o mesmo com IDCT rápida e upsampling simples;
 *   raw+y2r libjpeg só até os planos YCbCr (sem conversão de cor nem
 *           upsampling) e o serviço Y2R converte para RGB565 em hardware.
 *
 * No New 3DS, roda a 268 MHz (o clock do Old 3DS) e a 804 MHz.
 */
#include "probe.h"

#include <stdio.h> /* antes do jpeglib.h, que usa FILE */

#include <jpeglib.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define MIN_DECODES 16 /* cada medida: pelo menos isso de decodes */

typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf jmp;
} err_t;

static void on_error(j_common_ptr cinfo)
{
    longjmp(((err_t *)cinfo->err)->jmp, 1);
}

static struct jpeg_decompress_struct cinfo;
static err_t jerr;

static uint16_t *rgb;           /* 400x240 RGB565 */
static uint8_t *py, *pu, *pv;   /* planos 4:2:0 (linear: o DMA do Y2R lê daqui) */
static uint16_t *y2r_out;
static Handle y2r_event;
static int y2r_ok;

enum { M_RGB565, M_FAST, M_RAW, M_RAW_FAST, METHODS };

/* Devolve 0 se ok. w, h: tamanho da imagem. */
static int decode_rgb565(const uint8_t *jpg, uint32_t size, int fast, int *w, int *h)
{
    if (setjmp(jerr.jmp)) {
        jpeg_abort_decompress(&cinfo);
        return -1;
    }
    jpeg_mem_src(&cinfo, jpg, size);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB565;
    cinfo.dither_mode = JDITHER_NONE;
    cinfo.dct_method = fast ? JDCT_IFAST : JDCT_ISLOW;
    cinfo.do_fancy_upsampling = fast ? FALSE : TRUE;
    jpeg_start_decompress(&cinfo);
    *w = cinfo.output_width;
    *h = cinfo.output_height;
    if (*w > VID_W || *h > VID_H) {
        jpeg_abort_decompress(&cinfo);
        return -2;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = (JSAMPROW)(rgb + cinfo.output_scanline * VID_W);
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    return 0;
}

/* Só até YCbCr 4:2:0. -3 se a imagem não for 4:2:0 (o servidor manda 4:2:0). */
static int decode_raw(const uint8_t *jpg, uint32_t size, int fast, int *w, int *h)
{
    if (setjmp(jerr.jmp)) {
        jpeg_abort_decompress(&cinfo);
        return -1;
    }
    jpeg_mem_src(&cinfo, jpg, size);
    jpeg_read_header(&cinfo, TRUE);
    if (cinfo.num_components != 3 || cinfo.comp_info[0].h_samp_factor != 2 || cinfo.comp_info[0].v_samp_factor != 2 ||
        cinfo.comp_info[1].h_samp_factor != 1 || cinfo.comp_info[1].v_samp_factor != 1) {
        jpeg_abort_decompress(&cinfo);
        return -3;
    }
    cinfo.raw_data_out = TRUE;
    cinfo.dct_method = fast ? JDCT_IFAST : JDCT_ISLOW;
    jpeg_start_decompress(&cinfo);
    *w = cinfo.output_width;
    *h = cinfo.output_height;
    if (*w > VID_W || *h > VID_H) {
        jpeg_abort_decompress(&cinfo);
        return -2;
    }
    JSAMPROW yrows[16], urows[8], vrows[8];
    JSAMPARRAY planes[3] = {yrows, urows, vrows};
    /* linhas além da imagem (altura não múltipla de 16) vão para uma linha de sobra */
    static uint8_t scratch[VID_W];
    while (cinfo.output_scanline < cinfo.output_height) {
        int y0 = cinfo.output_scanline;
        for (int i = 0; i < 16; i++)
            yrows[i] = y0 + i < VID_H ? py + (y0 + i) * VID_W : scratch;
        for (int i = 0; i < 8; i++) {
            int r = y0 / 2 + i;
            urows[i] = r < VID_H / 2 ? pu + r * (VID_W / 2) : scratch;
            vrows[i] = r < VID_H / 2 ? pv + r * (VID_W / 2) : scratch;
        }
        jpeg_read_raw_data(&cinfo, planes, 16);
    }
    jpeg_finish_decompress(&cinfo);
    return 0;
}

static int y2r_setup(void)
{
    if (R_FAILED(y2rInit()))
        return -1;
    Y2RU_ConversionParams p = {
        .input_format = INPUT_YUV420_INDIV_8,
        .output_format = OUTPUT_RGB_16_565,
        .rotation = ROTATION_NONE,
        .block_alignment = BLOCK_LINE,
        .input_line_width = VID_W,
        .input_lines = VID_H,
        .standard_coefficient = COEFFICIENT_ITU_R_BT_601, /* JFIF: faixa cheia */
        .alpha = 0xFF,
    };
    if (R_FAILED(Y2RU_SetConversionParams(&p)) || R_FAILED(Y2RU_SetTransferEndInterrupt(true)) ||
        R_FAILED(Y2RU_GetTransferEndEvent(&y2r_event))) {
        y2rExit();
        return -1;
    }
    return 0;
}

/* Planos -> y2r_out. Devolve 0 se ok. */
static int y2r_convert(void)
{
    GSPGPU_FlushDataCache(py, VID_W * VID_H);
    GSPGPU_FlushDataCache(pu, VID_W * VID_H / 4);
    GSPGPU_FlushDataCache(pv, VID_W * VID_H / 4);
    Y2RU_SetSendingY(py, VID_W * VID_H, VID_W, 0);
    Y2RU_SetSendingU(pu, VID_W * VID_H / 4, VID_W / 2, 0);
    Y2RU_SetSendingV(pv, VID_W * VID_H / 4, VID_W / 2, 0);
    /* 8 linhas por transferência: com 1 linha, a 400x240, o fim vem cedo demais (y2r.h) */
    Y2RU_SetReceiving(y2r_out, VID_W * VID_H * 2, VID_W * 2 * 8, 0);
    if (R_FAILED(Y2RU_StartConversion()))
        return -1;
    if (R_FAILED(svcWaitSynchronization(y2r_event, 100 * 1000 * 1000LL))) {
        Y2RU_StopConversion();
        return -2;
    }
    GSPGPU_InvalidateDataCache(y2r_out, VID_W * VID_H * 2);
    return 0;
}

/* libjpeg e o buffer RGB565 ficam abertos até o fim (o teste de rede usa também). */
static int lib_init(void)
{
    if (rgb)
        return 0;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = on_error;
    jpeg_create_decompress(&cinfo);
    rgb = linearAlloc(VID_W * VID_H * 2);
    return rgb ? 0 : -1;
}

int jpeg_show(const uint8_t *jpg, uint32_t size, int *w, int *h)
{
    if (lib_init())
        return -1;
    int rc = decode_rgb565(jpg, size, 1, w, h);
    if (rc == 0)
        screen_show565(rgb, VID_W);
    return rc;
}

int jpeg_size(const uint8_t *jpg, uint32_t size, int *w, int *h)
{
    if (lib_init())
        return -1;
    if (setjmp(jerr.jmp)) {
        jpeg_abort_decompress(&cinfo);
        return -1;
    }
    jpeg_mem_src(&cinfo, jpg, size);
    jpeg_read_header(&cinfo, TRUE);
    *w = cinfo.image_width;
    *h = cinfo.image_height;
    jpeg_abort_decompress(&cinfo);
    return 0;
}

typedef struct {
    uint64_t total, y2r_total;
    int n, errors, marker_bad, size_bad;
} meas_t;

static void run_set(const item_set_t *s, int method, meas_t *m)
{
    memset(m, 0, sizeof(*m));
    int rounds = (MIN_DECODES + s->count - 1) / s->count;
    for (int r = 0; r < rounds; r++)
        for (int i = 0; i < s->count; i++) {
            int w = 0, h = 0, rc;
            uint64_t t0 = now_us();
            int raw = method == M_RAW || method == M_RAW_FAST;
            if (raw)
                rc = decode_raw(s->data[i], s->sizes[i], method == M_RAW_FAST, &w, &h);
            else
                rc = decode_rgb565(s->data[i], s->sizes[i], method == M_FAST, &w, &h);
            uint64_t t1 = now_us();
            if (rc == 0 && raw && y2r_ok) {
                rc = y2r_convert();
                m->y2r_total += now_us() - t1;
            }
            if (rc) {
                m->errors++;
                continue;
            }
            m->total += t1 - t0;
            m->n++;
            if (w != VID_W || h != VID_H)
                m->size_bad++;
            /* os frames src2 levam o número; o card não */
            if (r == 0 && !strncmp(s->name, "src2", 4)) {
                int mk = raw ? marker_y(py, VID_W) : marker_rgb565(rgb, VID_W, 0);
                if (mk != i)
                    m->marker_bad++;
            }
            if (r == 0 && i == 0)
                screen_show565(raw && y2r_ok ? y2r_out : rgb, VID_W);
        }
}

static void fmt_ms(char *out, int len, uint64_t us_total, int n)
{
    if (n)
        snprintf(out, len, "%5.1f", us_total / 1000.0 / n);
    else
        snprintf(out, len, "  err");
}

static void run_all(const char *clock)
{
    const item_set_t *sets;
    int n = data_jpegs(&sets);
    /* raw e rawf: só o libjpeg até YCbCr (IDCT exata / rápida); +y2r: a conversão no hardware, à parte */
    say("  %-10s %5s %6s %6s %6s %6s %5s  (ms/frame, %s)", "set", "KB", "rgb565", "fast", "raw", "rawf", "+y2r",
        clock);
    for (int i = 0; i < n; i++) {
        const item_set_t *s = &sets[i];
        uint32_t bytes = 0;
        for (int k = 0; k < s->count; k++)
            bytes += s->sizes[k];
        meas_t m[METHODS];
        char col[METHODS][16], y2r[16];
        int bad = 0;
        for (int method = 0; method < METHODS; method++) {
            run_set(s, method, &m[method]);
            fmt_ms(col[method], sizeof(col[method]), m[method].total, m[method].n);
            bad |= m[method].errors || m[method].marker_bad || m[method].size_bad;
        }
        if (y2r_ok)
            fmt_ms(y2r, sizeof(y2r), m[M_RAW].y2r_total, m[M_RAW].n);
        else
            snprintf(y2r, sizeof(y2r), "  n/a");
        say("  %-10s %5.1f %6s %6s %6s %6s %5s", s->name, bytes / 1024.0 / s->count, col[M_RGB565], col[M_FAST],
            col[M_RAW], col[M_RAW_FAST], y2r);
        if (bad)
            for (int method = 0; method < METHODS; method++)
                if (m[method].errors || m[method].marker_bad || m[method].size_bad)
                    say("    problems (column %d): errors %d, wrong frame number %d, wrong size %d", method + 1,
                        m[method].errors, m[method].marker_bad, m[method].size_bad);
    }
}

void test_jpeg(void)
{
    say("");
    say("== JPEG (software, %dx%d) ==", VID_W, VID_H);
    if (!step_begin("jpeg"))
        return;
    lib_init();
    py = linearAlloc(VID_W * VID_H);
    pu = linearAlloc(VID_W * VID_H / 4);
    pv = linearAlloc(VID_W * VID_H / 4);
    y2r_out = linearAlloc(VID_W * VID_H * 2);
    if (!rgb || !py || !pu || !pv || !y2r_out) {
        say("  out of linear memory");
        goto out;
    }
    y2r_ok = y2r_setup() == 0;
    if (!y2r_ok)
        say("  Y2R: init failed, raw times without the colour conversion");

    if (g_new3ds)
        osSetSpeedupEnable(false);
    run_all("268 MHz");
    if (g_new3ds) {
        osSetSpeedupEnable(true);
        run_all("804 MHz");
    }

    /* Y2R sozinho: conversão de um frame já em planos */
    if (y2r_ok) {
        uint64_t t0 = now_us();
        int ok = 0;
        for (int i = 0; i < 32; i++)
            ok += y2r_convert() == 0;
        say("  Y2R alone: %.2f ms/frame (%d/32 ok)", (now_us() - t0) / 1000.0 / 32, ok);
        y2rExit();
    }
out:
    linearFree(py);
    linearFree(pu);
    linearFree(pv);
    linearFree(y2r_out);
    step_end();
}
