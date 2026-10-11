/* Relatório na tela de baixo e em PROBE_DIR/probe_result.txt, e os passos travados. */
#include "probe.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define REPORT_MAX (64 * 1024)
static char report[REPORT_MAX];
static int report_len;

void report_init(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(PROBE_DIR, 0777);
    report_len = 0;
}

void say(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    printf("%s\n", line);
    int n = strlen(line);
    if (report_len + n + 2 < REPORT_MAX) {
        memcpy(report + report_len, line, n);
        report_len += n;
        report[report_len++] = '\n';
    }
}

void status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    gfxFlushBuffers();
    gfxSwapBuffers();
}

void report_save(void)
{
    FILE *f = fopen(PROBE_DIR "/probe_result.txt", "w");
    if (!f)
        return;
    fwrite(report, 1, report_len, f);
    fclose(f);
}

/* ---- passos que podem travar o 3DS ---- */
static char crashed[2048];

void steps_init(void)
{
    /* um passo que não terminou da última vez: travou ou desligou */
    FILE *f = fopen(PROBE_DIR "/current_step.txt", "r");
    if (f) {
        char name[64] = "";
        if (fgets(name, sizeof(name), f))
            name[strcspn(name, "\r\n")] = 0;
        fclose(f);
        remove(PROBE_DIR "/current_step.txt");
        if (name[0]) {
            FILE *c = fopen(PROBE_DIR "/crashed.txt", "a");
            if (c) {
                fprintf(c, "%s\n", name);
                fclose(c);
            }
        }
    }
    f = fopen(PROBE_DIR "/crashed.txt", "r");
    if (f) {
        size_t n = fread(crashed, 1, sizeof(crashed) - 1, f);
        crashed[n] = 0;
        fclose(f);
    }
}

static int was_crashed(const char *name)
{
    size_t len = strlen(name);
    for (const char *p = crashed; *p;) {
        const char *end = p + strcspn(p, "\r\n");
        if ((size_t)(end - p) == len && !memcmp(p, name, len))
            return 1;
        p = *end ? end + 1 : end;
    }
    return 0;
}

int step_begin(const char *name)
{
    if (was_crashed(name)) {
        say("[%s] SKIPPED: it froze the 3DS last time (delete crashed.txt to retry)", name);
        return 0;
    }
    report_save();
    FILE *f = fopen(PROBE_DIR "/current_step.txt", "w");
    if (f) {
        fprintf(f, "%s\n", name);
        fclose(f);
    }
    return 1;
}

void step_end(void)
{
    remove(PROBE_DIR "/current_step.txt");
    report_save();
}
