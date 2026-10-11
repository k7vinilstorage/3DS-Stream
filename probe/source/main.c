/*
 * 3DS-Stream probe (fase 0 do port): mede no 3DS o que o cliente vai usar e
 * grava tudo em sdmc:/3ds/3ds-stream/probe_result.txt. Ver docs/Probe.md.
 *
 * Ordem: sistema, JPEG, som, rede, MVD. O MVD por último: é o passo que mais
 * pode travar o console, e os anteriores já ficaram gravados.
 */
#include "probe.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

bool g_new3ds;

void test_sysinfo(void)
{
    u32 fw = osGetFirmVersion(), kv = osGetKernelVersion();
    u8 model = 0;
    CFGU_GetSystemModel(&model);
    static const char *models[] = {"3DS", "3DS XL", "New 3DS", "2DS", "New 3DS XL", "New 2DS XL"};
    time_t now = time(NULL);
    char when[32];
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M", gmtime(&now));
    say("3DS-Stream probe %s, %s", PROBE_VERSION, when);
    say("model: %s (%s), firmware %lu.%lu.%lu, kernel %lu.%lu.%lu", model < 6 ? models[model] : "?",
        g_new3ds ? "New 3DS hardware" : "Old 3DS hardware", GET_VERSION_MAJOR(fw), GET_VERSION_MINOR(fw),
        GET_VERSION_REVISION(fw), GET_VERSION_MAJOR(kv), GET_VERSION_MINOR(kv), GET_VERSION_REVISION(kv));
    say("linear heap free: %lu KB, heap free: %lu KB", linearSpaceFree() / 1024,
        (u32)(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024));
}

static void wait_key(u32 keys)
{
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & keys)
            return;
        gspWaitForVBlank();
    }
}

int main(void)
{
    gfxInit(GSP_RGB565_OES, GSP_BGR8_OES, false);
    consoleInit(GFX_BOTTOM, NULL);
    cfguInit();
    APT_CheckNew3DS(&g_new3ds);

    printf("3DS-Stream probe %s\n\n", PROBE_VERSION);
    Result rr = romfsInit();
    report_init();
    steps_init();
    if (R_FAILED(rr) || data_load()) {
        printf("test data missing from romfs\nSTART: exit\n");
        wait_key(KEY_START);
        goto out;
    }
    printf("Measures JPEG, audio, the network and\n"
           "(New 3DS) the H.264 decoder.\n\n"
           "For the network part, on the PC:\n"
           " pspstream --source test --size 400x240\n\n"
           "A: start    START: exit\n");
    for (;;) {
        if (!aptMainLoop())
            goto out;
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_START)
            goto out;
        if (k & KEY_A)
            break;
        gspWaitForVBlank();
    }
    consoleClear();

    test_sysinfo();
    report_save();
    test_jpeg();
    test_audio();
    test_net();
    test_mvd();
    if (g_new3ds)
        osSetSpeedupEnable(false);

    say("");
    say("done");
    report_save();
    printf("\nSaved to %s/probe_result.txt\nSTART: exit\n", PROBE_DIR);
    wait_key(KEY_START);
out:
    romfsExit();
    cfguExit();
    gfxExit();
    return 0;
}
