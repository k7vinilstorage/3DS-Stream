/*
 * Rede com o servidor PSPStream de verdade, protocolo v5 sem mudanças:
 *
 *  1. "Procurar o PC": PING em broadcast (255.255.255.255 e o da sub-rede),
 *     como o PSP faz (psp/src/net.c, net_discover). Sem resposta, usa
 *     server=IP de PROBE_DIR/server.txt.
 *  2. Ida e volta pura: 20 PINGs.
 *  3. Stream JPEG, um frame por vez (pedido -> frame inteiro -> próximo
 *     pedido): vazão real do Wi-Fi (primeiro -> último pedaço), perdas e FPS.
 *     Primeiro só a rede, depois decodificando e mostrando cada frame.
 *
 * O servidor precisa de --size 400x240 (com outro tamanho ele manda o que
 * pediram, e o relatório avisa). Sem os bits de H.264 em wflags, ele manda JPEG.
 */
#include "probe.h"
#include "protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define SOC_BUFFER_SIZE 0x100000
#define PHASE_S 6
#define FRAME_TIMEOUT_US (250 * 1000)

static u32 *soc_buf;
static int sock = -1;
static struct sockaddr_in server;
static uint8_t frame[PS_MAX_JPEG];
static uint8_t pkt[sizeof(ps_chunk_hdr_t) + PS_CHUNK_PAYLOAD + 64];

/* errno da libctru: às vezes vem negativo (-ENETDOWN) */
static const char *err_name(int e)
{
    return strerror(e < 0 ? -e : e);
}

static uint16_t u16clamp(uint64_t v)
{
    return v > 0xFFFF ? 0xFFFF : (uint16_t)v;
}

static void req_init(ps_req_t *r, uint16_t flags)
{
    memset(r, 0, sizeof(*r));
    r->magic = PS_MAGIC_REQ;
    r->lx = r->ly = 128;
    r->flags = flags;
    r->idle_t = PS_IDLE_NONE;
}

static int send_to(const struct sockaddr_in *to, const void *buf, int len)
{
    return sendto(sock, buf, len, 0, (const struct sockaddr *)to, sizeof(*to));
}

/* Espera um datagrama até timeout_us. Devolve bytes, 0 se nada, -1 erro. */
static int recv_wait(void *buf, int len, int timeout_us, struct sockaddr_in *from)
{
    struct pollfd pfd = {.fd = sock, .events = POLLIN};
    int r = poll(&pfd, 1, timeout_us / 1000);
    if (r <= 0)
        return r < 0 ? -1 : 0;
    socklen_t fl = sizeof(*from);
    struct sockaddr_in dummy;
    if (!from)
        from = &dummy;
    int n = recvfrom(sock, buf, len, 0, (struct sockaddr *)from, &fl);
    return n < 0 ? (errno == EAGAIN || errno == EWOULDBLOCK ? 0 : -1) : n;
}

static int discover(char *ip, int iplen)
{
    struct in_addr me, mask, bc;
    int have_bc = SOCU_GetIPInfo(&me, &mask, &bc) == 0;
    int on = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    uint32_t targets[2] = {0xFFFFFFFFu, have_bc ? bc.s_addr : 0};

    ps_req_t r;
    req_init(&r, PS_REQ_PING);
    r.echo_ts = (uint32_t)now_us() | 0x40000000u; /* o mesmo token do PSP: não é o ping do stream */
    uint64_t t0 = now_us(), next = t0;
    while (now_us() - t0 < 3 * 1000 * 1000) {
        if (now_us() >= next) {
            for (int k = 0; k < 2; k++) {
                if (!targets[k] || (k == 1 && targets[1] == targets[0]))
                    continue;
                struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(PS_DEFAULT_PORT)};
                to.sin_addr.s_addr = targets[k];
                send_to(&to, &r, sizeof(r));
            }
            next = now_us() + 200 * 1000;
        }
        struct sockaddr_in from;
        int n = recv_wait(pkt, sizeof(pkt), 50 * 1000, &from);
        ps_pong_t pong;
        if (n == sizeof(pong)) {
            memcpy(&pong, pkt, sizeof(pong));
            if (pong.magic == PS_MAGIC_PONG && pong.token == r.echo_ts) {
                snprintf(ip, iplen, "%s", inet_ntoa(from.sin_addr));
                say("  discovery: server at %s after %.0f ms", ip, (now_us() - t0) / 1000.0);
                return 0;
            }
        }
    }
    say("  discovery: no answer to the broadcast in 3 s");
    return -1;
}

static int server_from_file(char *ip, int iplen)
{
    FILE *f = fopen(PROBE_DIR "/server.txt", "r");
    if (!f)
        return -1;
    char line[128];
    int found = -1;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "server=", 7) && line[7]) {
            snprintf(ip, iplen, "%.63s", line + 7);
            found = 0;
        }
    }
    fclose(f);
    return found;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static int pings(void)
{
    uint32_t rtt[20];
    int got = 0;
    for (int i = 0; i < 20; i++) {
        ps_req_t r;
        req_init(&r, PS_REQ_PING);
        r.echo_ts = (uint32_t)now_us() ^ (uint32_t)i << 28;
        uint64_t t0 = now_us();
        send_to(&server, &r, sizeof(r));
        while (now_us() - t0 < 300 * 1000) {
            int n = recv_wait(pkt, sizeof(pkt), 300 * 1000 - (int)(now_us() - t0), NULL);
            if (n <= 0)
                break;
            ps_pong_t pong;
            memcpy(&pong, pkt, sizeof(pong));
            if (n == sizeof(pong) && pong.magic == PS_MAGIC_PONG && pong.token == r.echo_ts) {
                rtt[got++] = now_us() - t0;
                break;
            }
        }
        svcSleepThread(10 * 1000 * 1000LL);
    }
    if (!got) {
        say("  ping: no answer (old server, firewall, or wrong address)");
        return 0;
    }
    qsort(rtt, got, sizeof(rtt[0]), cmp_u32);
    say("  ping: %d/20, min %.1f ms, median %.1f ms, max %.1f ms", got, rtt[0] / 1000.0, rtt[got / 2] / 1000.0,
        rtt[got - 1] / 1000.0);
    return 1;
}

typedef struct {
    int frames, lost, stray, stripped, decode_fail;
    uint64_t bytes, burst_bytes, burst_us, first_us, decode_us;
    int burst_n, w, h;
} stream_stats_t;

/* Um frame: pedido enviado, espera os pedaços. 1 = completo, 0 = perdido, -1 = erro. */
static int get_frame(ps_req_t *req, stream_stats_t *st, uint32_t *frame_no, uint32_t *send_ts, int *size,
                     uint64_t *t_first, uint64_t *t_last)
{
    static uint32_t have[PS_MAX_CHUNKS / 32];
    uint64_t t_req = now_us();
    if (send_to(&server, req, sizeof(*req)) < 0)
        return -1;
    uint32_t cur = 0;
    int count = 0, got = 0;
    *t_first = 0;
    for (;;) {
        int left = FRAME_TIMEOUT_US - (int)(now_us() - (*t_first ? *t_last : t_req));
        if (left <= 0)
            return 0;
        int n = recv_wait(pkt, sizeof(pkt), left, NULL);
        if (n < 0)
            return -1;
        if (n < (int)sizeof(ps_chunk_hdr_t))
            continue; /* pong atrasado */
        ps_chunk_hdr_t h;
        memcpy(&h, pkt, sizeof(h));
        if (h.magic != PS_MAGIC_CHUNK || h.size > PS_MAX_JPEG || h.count > PS_MAX_CHUNKS || h.chunk >= h.count) {
            st->stray++;
            continue;
        }
        if (h.hdr & PS_HDR_STRIPPED) {
            st->stripped++; /* não guardamos cabeçalho (hdr_have = 0): não deveria vir */
            continue;
        }
        if (h.frame_no != cur) {
            if (cur && h.frame_no < cur) {
                st->stray++; /* pedaço de um frame velho */
                continue;
            }
            cur = h.frame_no;
            count = h.count;
            got = 0;
            memset(have, 0, sizeof(have));
            *t_first = now_us();
        }
        if (have[h.chunk / 32] & 1u << (h.chunk % 32))
            continue;
        have[h.chunk / 32] |= 1u << (h.chunk % 32);
        int off = h.chunk * PS_CHUNK_PAYLOAD, len = n - (int)sizeof(h);
        if (off + len > (int)h.size)
            len = h.size - off;
        if (len > 0)
            memcpy(frame + off, pkt + sizeof(h), len);
        *t_last = now_us();
        if (++got == count) {
            *frame_no = h.frame_no;
            *send_ts = h.send_ts;
            *size = h.size;
            st->first_us += *t_first - t_req;
            return 1;
        }
    }
}

static void stream_phase(int decode, stream_stats_t *st)
{
    memset(st, 0, sizeof(*st));
    ps_req_t req;
    req_init(&req, PS_REQ_HELLO | PS_REQ_FRAME);
    uint64_t t0 = now_us();
    while (now_us() - t0 < PHASE_S * 1000 * 1000ULL) {
        uint32_t frame_no, send_ts;
        int size;
        uint64_t t_first, t_last, t_req = now_us();
        int r = get_frame(&req, st, &frame_no, &send_ts, &size, &t_first, &t_last);
        if (r < 0) {
            say("  socket error: %s", err_name(errno));
            return;
        }
        uint16_t flags = PS_REQ_FRAME;
        if (r == 0) {
            st->lost++;
            req.flags = flags;
            req.lost = st->lost;
            continue;
        }
        st->frames++;
        st->bytes += size;
        if (size > PS_CHUNK_PAYLOAD && t_last > t_first) {
            /* o 1º pedaço marca o início: a rajada é o resto */
            st->burst_bytes += size - PS_CHUNK_PAYLOAD;
            st->burst_us += t_last - t_first;
            st->burst_n++;
        }
        if (st->frames == 1)
            jpeg_size(frame, size, &st->w, &st->h);
        uint64_t t_dec = now_us();
        if (decode) {
            int w, h;
            if (jpeg_show(frame, size, &w, &h))
                st->decode_fail++;
            st->decode_us += now_us() - t_dec;
        }
        uint64_t now = now_us();
        /* o próximo pedido leva o ack e os tempos, como o PSP manda */
        req_init(&req, flags);
        req.ack_frame = frame_no;
        req.echo_ts = send_ts;
        req.net_t = u16clamp((t_last - t_req) / 100);
        req.local_t = u16clamp((now - t_last) / 100);
        req.decode_t = u16clamp((now - t_dec) / 100);
        req.first_t = u16clamp((t_first - t_req) / 100);
        req.burst_t = u16clamp((t_last - t_first) / 100);
        req.since_t = 0;
        req.lost = st->lost;
    }
    ps_req_t bye;
    req_init(&bye, PS_REQ_BYE);
    send_to(&server, &bye, sizeof(bye));
    svcSleepThread(300 * 1000 * 1000LL); /* o servidor fecha a sessão antes da próxima fase */
    /* descarta o que ainda chegar */
    while (recv_wait(pkt, sizeof(pkt), 50 * 1000, NULL) > 0)
        ;
}

static void stream_report(const char *what, const stream_stats_t *st)
{
    say("  %s: %.1f fps, %d frames, %d lost, %.1f KB/frame", what, st->frames / (double)PHASE_S, st->frames,
        st->lost, st->frames ? st->bytes / 1024.0 / st->frames : 0);
    if (st->frames)
        say("    request->1st chunk %.1f ms (with the wait for a new frame), burst %.0f KB/s, goodput %.0f KB/s", st->first_us / 1000.0 / st->frames,
            st->burst_us ? st->burst_bytes * 1e6 / 1024.0 / st->burst_us : 0, st->bytes / 1024.0 / PHASE_S);
    if (st->decode_us && st->frames)
        say("    decode + show %.1f ms/frame%s", st->decode_us / 1000.0 / st->frames,
            st->decode_fail ? " (some failed)" : "");
    if (st->stray || st->stripped)
        say("    stray chunks %d, header-stripped %d", st->stray, st->stripped);
}

/* O Wi-Fi conecta sozinho com a conexão das Configurações do Sistema, mas
 * leva alguns segundos depois de abrir o app, e o Old 3DS tem a chave do
 * Wi-Fi na lateral. Sem conexão, socket() falha com ENETDOWN. */
static int wifi_wait(void)
{
    if (R_FAILED(acInit())) {
        say("  ac service unavailable");
        return -1;
    }
    u32 wifi = 0;
    uint64_t t0 = now_us();
    int shown = 0;
    while (R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && !wifi && now_us() - t0 < 20 * 1000 * 1000ULL) {
        if (!shown++)
            status("  waiting for Wi-Fi (up to 20 s)...");
        svcSleepThread(250 * 1000 * 1000LL);
    }
    acExit();
    if (!wifi) {
        say("  no Wi-Fi connection after 20 s: turn the wireless switch on (Old 3DS: on the side;");
        say("  the LED lights up) and check the connection in System Settings > Internet Settings");
        return -1;
    }
    say("  Wi-Fi connected after %.1f s", (now_us() - t0) / 1e6);
    return 0;
}

/* Sem resposta ao broadcast nem server.txt: pergunta o IP no teclado da
 * tela e guarda em server.txt para a próxima vez. 0 se ok. */
static int ask_server_ip(char *ip, int iplen, struct in_addr me)
{
#ifdef __3DS__
    char prefix[20];
    const uint8_t *b = (const uint8_t *)&me.s_addr;
    snprintf(prefix, sizeof(prefix), "%u.%u.%u.", b[0], b[1], b[2]); /* a mesma rede do 3DS */
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, 15);
    swkbdSetNumpadKeys(&kb, '.', 0);
    swkbdSetHintText(&kb, "IP of the PC running pspstream");
    swkbdSetInitialText(&kb, prefix);
    char buf[20];
    if (swkbdInputText(&kb, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM || !buf[0])
        return -1;
    struct in_addr a;
    if (!inet_aton(buf, &a))
        return -1;
    snprintf(ip, iplen, "%s", buf);
    FILE *f = fopen(PROBE_DIR "/server.txt", "w");
    if (f) {
        fprintf(f, "server=%s\n", buf);
        fclose(f);
    }
    return 0;
#else
    return -1; /* testes no PC: sem teclado */
#endif
}

void test_net(void)
{
    say("");
    say("== Network (PSPStream server, UDP %d) ==", PS_DEFAULT_PORT);
    if (!step_begin("net"))
        return;
    if (wifi_wait()) {
        step_end();
        return;
    }
    soc_buf = memalign(0x1000, SOC_BUFFER_SIZE);
    if (!soc_buf || R_FAILED(socInit(soc_buf, SOC_BUFFER_SIZE))) {
        say("  socInit failed");
        free(soc_buf);
        step_end();
        return;
    }
    struct in_addr me, mask, bc;
    if (SOCU_GetIPInfo(&me, &mask, &bc) == 0) {
        char a[16], b[16];
        snprintf(a, sizeof(a), "%s", inet_ntoa(me));
        snprintf(b, sizeof(b), "%s", inet_ntoa(mask));
        say("  3DS: %s, mask %s, broadcast %s", a, b, inet_ntoa(bc));
    } else {
        say("  no network: connect the 3DS to Wi-Fi in System Settings");
        goto out;
    }
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        say("  socket() failed: %s", err_name(errno));
        goto out;
    }
    int rcvbuf = 256 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = 0, .sin_addr.s_addr = INADDR_ANY};
    bind(sock, (struct sockaddr *)&local, sizeof(local));

    char ip[64];
    status("  looking for the PC (broadcast)...");
    int found_by_broadcast = discover(ip, sizeof(ip)) == 0;
    if (!found_by_broadcast && server_from_file(ip, sizeof(ip)) == 0) {
        say("  using server.txt");
    } else if (!found_by_broadcast) {
        if (ask_server_ip(ip, sizeof(ip), me)) {
            say("  no server: start pspstream on the PC, or put server=IP in " PROBE_DIR "/server.txt");
            goto out;
        }
        say("  typed on the keyboard (saved to server.txt)");
    }
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PS_DEFAULT_PORT);
    if (!inet_aton(ip, &server.sin_addr)) {
        say("  invalid server address: %s", ip);
        goto out;
    }
    say("  server: %s", ip);
    status("  ping...");
    int ping_ok = pings();
    if (!ping_ok) {
        say("  the PC does not answer: is pspstream running there, and is UDP 5123 open in its firewall");
        say("  (pspstream --setup opens it)? The PC must be on the same network as the 3DS.");
        goto out;
    }
    if (!found_by_broadcast)
        say("  the PC answers by IP but not to the broadcast: the router (AP/client isolation)"
            " or the 3DS blocks broadcasts; the client will need the IP in its settings");

    stream_stats_t st;
    status("  stream, network only (%d s)...", PHASE_S);
    stream_phase(0, &st);
    if (st.frames && (st.w != VID_W || st.h != VID_H))
        say("  NOTE: the server sends %dx%d; run it with --size 400x240", st.w, st.h);
    stream_report("JPEG, network only", &st);
    status("  stream, decoding and showing (%d s)...", PHASE_S);
    stream_phase(1, &st);
    stream_report("JPEG, decode + show", &st);
out:
    if (sock >= 0)
        close(sock);
    sock = -1;
    socExit();
    free(soc_buf);
    step_end();
}
