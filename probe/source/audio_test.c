/*
 * Som: IMA ADPCM decodificado na CPU (o mesmo ima.c do PSP) e tocado pelo NDSP.
 *  - o NDSP abre? Ele precisa de sdmc:/3ds/dspfirm.cdc (dump do DSP1);
 *  - quanto da CPU o decode toma (blocos de 20 ms, 44,1 kHz estéreo);
 *  - quanto leva do primeiro buffer entregue até o som começar;
 *  - tocando em pedaços de 20 ms, como chegam da rede, com 4 na fila: faltas.
 */
#include "probe.h"
#include "ima.h"

#include <stdlib.h>
#include <string.h>

#define CH 0
#define QUEUE 4

void test_audio(void)
{
    say("");
    say("== Audio (IMA ADPCM -> NDSP) ==");
    const audio_clip_t *a = data_audio();
    if (!a) {
        say("  audio.bin missing from romfs");
        return;
    }
    if (!step_begin("audio"))
        return;
    int frames_per_block = a->samples;
    int total = frames_per_block * a->blocks;
    int16_t *pcm = linearAlloc(total * 2 * sizeof(int16_t));
    if (!pcm) {
        say("  out of linear memory");
        step_end();
        return;
    }

    /* decode de todos os blocos, medido */
    uint64_t t0 = now_us();
    int frames = 0;
    for (int b = 0; b < a->blocks; b++)
        frames += ima_decode_block(a->data + b * a->block_bytes, a->block_bytes, a->channels, a->samples,
                                   pcm + frames * 2);
    uint64_t dt = now_us() - t0;
    double audio_s = frames / (double)a->rate;
    say("  IMA decode: %.2f ms for %.2f s of audio (%.2f%% of a %s core)", dt / 1000.0, audio_s,
        dt / 1e4 / audio_s, g_new3ds ? "804 MHz" : "268 MHz");
    DSP_FlushDataCache(pcm, frames * 2 * sizeof(int16_t));

    Result r = ndspInit();
    if (R_FAILED(r)) {
        say("  ndspInit failed (0x%08lX): copy the DSP firmware dump to sdmc:/3ds/dspfirm.cdc", r);
        linearFree(pcm);
        step_end();
        return;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(CH);
    ndspChnSetInterp(CH, NDSP_INTERP_LINEAR);
    ndspChnSetRate(CH, a->rate);
    ndspChnSetFormat(CH, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {1.0f, 1.0f};
    ndspChnSetMix(CH, mix);

    /* 1. um buffer só: do entregar até a posição andar */
    ndspWaveBuf one;
    memset(&one, 0, sizeof(one));
    one.data_vaddr = pcm;
    one.nsamples = frames / 2; /* metade do clipe */
    t0 = now_us();
    ndspChnWaveBufAdd(CH, &one);
    uint64_t start = 0;
    while (now_us() - t0 < 1000 * 1000) {
        if (ndspChnGetSamplePos(CH) > 0) {
            start = now_us() - t0;
            break;
        }
        svcSleepThread(200 * 1000);
    }
    while (one.status != NDSP_WBUF_DONE && now_us() - t0 < 3 * 1000 * 1000)
        svcSleepThread(1000 * 1000);
    uint64_t played = now_us() - t0;
    if (start)
        say("  start latency: %.1f ms; %.0f ms of audio played in %.0f ms", start / 1000.0,
            one.nsamples * 1000.0 / a->rate, played / 1000.0);
    else
        say("  NDSP did not start playing in 1 s");

    /* 2. pedaços de 20 ms, QUEUE na fila, repostos assim que acabam */
    ndspChnWaveBufClear(CH);
    ndspWaveBuf q[QUEUE];
    memset(q, 0, sizeof(q));
    int next_block = 0, underruns = 0, queued = 0;
    for (int i = 0; i < QUEUE && next_block < a->blocks; i++, next_block++) {
        q[i].data_vaddr = pcm + next_block * frames_per_block * 2;
        q[i].nsamples = frames_per_block;
        ndspChnWaveBufAdd(CH, &q[i]);
        queued++;
    }
    t0 = now_us();
    while (queued && now_us() - t0 < 5 * 1000 * 1000) {
        int done = 0;
        for (int i = 0; i < QUEUE; i++)
            if (q[i].status == NDSP_WBUF_DONE)
                done++;
        if (done == QUEUE && next_block < a->blocks)
            underruns++; /* todos tocados antes de repor: o som parou */
        for (int i = 0; i < QUEUE; i++) {
            if (q[i].status != NDSP_WBUF_DONE)
                continue;
            if (next_block < a->blocks) {
                q[i].data_vaddr = pcm + next_block * frames_per_block * 2;
                q[i].nsamples = frames_per_block;
                q[i].status = NDSP_WBUF_FREE;
                ndspChnWaveBufAdd(CH, &q[i]);
                next_block++;
            } else {
                q[i].status = NDSP_WBUF_FREE;
                queued--;
            }
        }
        svcSleepThread(1000 * 1000);
    }
    say("  20 ms chunks, %d queued: %d blocks, %d underruns, %.0f ms", QUEUE, a->blocks, underruns,
        (now_us() - t0) / 1000.0);
    ndspChnWaveBufClear(CH);
    ndspExit();
    linearFree(pcm);
    step_end();
}
