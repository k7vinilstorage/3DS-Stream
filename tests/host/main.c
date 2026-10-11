/* Testes no PC: ./probe_host jpeg | mvd | net (net precisa do servidor PSPStream rodando). */
#include "probe.h"

#include <stdio.h>
#include <string.h>

int fake_mvd_check(void);

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "";
    if (!strcmp(what, "net")) {
        test_net();
        return 0;
    }
    if (data_load()) {
        printf("romfs:/ data missing (run from tests/host, after tools/probe_clips.py)\n");
        return 1;
    }
    if (!strcmp(what, "jpeg")) {
        test_jpeg();
        return 0;
    }
    if (!strcmp(what, "mvd")) {
        test_mvd();
        return fake_mvd_check();
    }
    printf("usage: %s jpeg|mvd|net\n", argv[0]);
    return 2;
}
