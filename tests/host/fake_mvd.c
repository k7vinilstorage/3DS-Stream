/* MVD falso: confere cada NAL que o mvd_test.c entrega (prefixo 00 00 01, sem
 * zeros sobrando no fim, AUD só quando pedido) e conta os renders. */
#include "probe.h"

#include <stdio.h>

static int by_type[32], malformed, renders, slices;

Result mvdstdInit(MVDSTD_Mode mode, int in, int out, u32 size, void *init) { return 0; }
void mvdstdExit(void) {}
void mvdstdGenerateDefaultConfig(MVDSTD_Config *c, u32 iw, u32 ih, u32 ow, u32 oh, u32 *cc, u32 *o0, u32 *o1) {}

Result mvdstdProcessVideoFrame(void *buf, size_t size, u32 flag, MVDSTD_ProcessNALUnitOut *out)
{
    const uint8_t *d = buf;
    if (size < 4 || d[0] || d[1] || d[2] != 1 || d[size - 1] == 0 || (d[3] & 0x80))
        malformed++;
    int type = d[3] & 0x1F;
    by_type[type]++;
    if (type == 1 || type == 5)
        slices++;
    return type == 7 || type == 8 ? MVD_STATUS_PARAMSET : MVD_STATUS_FRAMEREADY;
}

Result mvdstdRenderVideoFrame(MVDSTD_Config *c, bool wait)
{
    renders++;
    return MVD_STATUS_OK;
}

int fake_mvd_check(void)
{
    printf("NALs: SPS %d, PPS %d, IDR %d, P %d, AUD %d; malformed %d; renders %d of %d slices\n", by_type[7],
           by_type[8], by_type[5], by_type[1], by_type[9], malformed, renders, slices);
    return malformed == 0 && renders == slices && slices > 0 && by_type[7] > 0 ? 0 : 1;
}
