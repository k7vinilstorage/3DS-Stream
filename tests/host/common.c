/* Testes no PC: relatório na saída padrão, sem passos travados. */
#include "probe.h"

#include <stdarg.h>
#include <stdio.h>

bool g_new3ds = true;
uint16_t fake_fb[400 * 240];

void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

void status(const char *fmt, ...) {}
int step_begin(const char *name) { return 1; }
void step_end(void) {}
