/* Marcador de número do frame e cópia para a tela de cima. */
#include "probe.h"

/* Centro de cada bloco: (k * 32 + 16, 16). Branco = 1, preto = 0; no meio
 * (cinza) é ambíguo: imagem errada, outro layout ou frame corrompido. */
static int read_marker(int (*luma)(const void *, int, int, int, int), const void *img, int stride, int transposed)
{
    int v = 0;
    for (int k = 0; k < 8; k++) {
        int l = luma(img, stride, k * 32 + 16, 16, transposed);
        if (l > 160)
            v |= 1 << k;
        else if (l > 90)
            return -1;
    }
    return v;
}

static int luma565(const void *img, int stride, int x, int y, int transposed)
{
    const uint16_t *p = img;
    uint16_t px = transposed ? p[x * stride + y] : p[y * stride + x];
    /* o verde tem 6 bits e pesa mais na luminância: basta para branco x preto */
    return ((px >> 5) & 0x3F) * 255 / 63;
}

static int luma8(const void *img, int stride, int x, int y, int transposed)
{
    return ((const uint8_t *)img)[y * stride + x];
}

int marker_rgb565(const uint16_t *img, int stride, int transposed)
{
    return read_marker(luma565, img, stride, transposed);
}

int marker_y(const uint8_t *y, int stride)
{
    return read_marker(luma8, y, stride, 0);
}

void screen_show565(const uint16_t *img, int stride)
{
    /* O framebuffer de cima é 240x400 "em pé": cada coluna da tela é uma linha
     * de 240 pixels, de baixo para cima. */
    u16 w, h;
    uint16_t *fb = (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &w, &h);
    for (int x = 0; x < VID_W; x++) {
        uint16_t *col = fb + x * VID_H + (VID_H - 1);
        const uint16_t *src = img + x;
        for (int y = 0; y < VID_H; y++, src += stride)
            *col-- = *src;
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
}
