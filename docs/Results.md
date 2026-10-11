# Probe results

What the [probe](Probe.md) measured on real consoles, and what it means for
the [3DS port plan](https://github.com/k7vinilstorage/PSP-Stream/wiki/3DS-Port-Plan).

## Old 3DS (probe 0.1)

`model: 3DS (Old 3DS hardware), firmware 2.58.0`. The network step did not
run: the console was not on Wi-Fi (`socket()` failed with `ENETDOWN`; probe
0.2 waits for the connection and says so). MVD does not exist on this model.

### JPEG at 400x240, 268 MHz (ms/frame)

| set | KB | rgb565 | fast | raw | +y2r |
|---|---|---|---|---|---|
| src2_q50 | 6.8 | 35.8 | 23.1 | 14.9 | 6.1 |
| card_q50 | 14.6 | 38.9 | 25.7 | 18.6 | 6.1 |
| src2_q70 | 8.6 | 36.3 | 23.1 | 15.5 | 6.1 |
| card_q70 | 18.0 | 40.0 | 26.8 | 19.8 | 6.1 |
| src2_q85 | 11.6 | 37.2 | 24.2 | 16.6 | 6.1 |
| card_q85 | 23.2 | 41.9 | 28.9 | 21.8 | 6.1 |
| src2_q95 | 17.2 | 38.8 | 25.5 | 18.2 | 6.1 |
| card_q95 | 34.4 | 45.8 | 32.5 | 25.5 | 6.1 |

- **Decoding only to YCbCr and letting Y2R do the colour is ~2x faster**
  than libjpeg's RGB565 output: on a desktop image (`card`) at q70, 19.8 ms
  of CPU against 40.0 ms. Most of libjpeg's time was the colour
  conversion and upsampling, not the Huffman/IDCT.
- **Y2R takes 6.1 ms per frame, the same at every quality**, and it is
  hardware with DMA: while it converts frame N, the CPU can decode N+1.
  Pipelined, the CPU limit is the `raw` column: about **50 fps on a desktop
  at q70** and 45 fps at q85, 60+ fps on simpler images at q50. In sequence
  (decode, then wait for Y2R), 38-48 fps.
- That is better than the plan's guess for the Old 3DS (20-30 fps). The
  next limits are the Wi-Fi (18 KB/frame x 50 fps = 900 KB/s, still to be
  measured) and the network receive running on the same core.
- Probe 0.2 (same console) added the fast IDCT on the raw path (`rawf`):
  **10-12% less CPU** than `raw`, e.g. 17.7 ms for the desktop image at q70
  (20.1 with the exact IDCT) and 19.5 ms at q85. With Y2R overlapped, about
  55 fps on a desktop at q70.

| set (0.2) | raw | rawf |
|---|---|---|
| src2_q50 | 15.0 | 13.2 |
| card_q50 | 18.9 | 16.5 |
| src2_q70 | 15.6 | 13.7 |
| card_q70 | 20.1 | 17.7 |
| src2_q85 | 16.7 | 14.8 |
| card_q85 | 22.0 | 19.5 |
| src2_q95 | 18.2 | 16.3 |
| card_q95 | 25.6 | 22.9 |

The 0.2 run got on Wi-Fi (`192.168.3.130/24`) but the broadcast "Find the
PC" had no answer and there was no `server.txt`, so the stream did not run.
Probe 0.3 asks for the PC's IP on the keyboard and then tells apart "the PC
does not answer" (server not running, firewall) from "it answers by IP but
not to the broadcast" (router isolation or the 3DS).

### Audio

| measure | value |
|---|---|
| IMA ADPCM decode, 44.1 kHz stereo | 1.6% of the 268 MHz core |
| NDSP start latency | 7.2 ms |
| 20 ms chunks, 4 queued, 2 s | 0 underruns |

The PSP's audio path works as is: `ima.c` with NDSP, 20 ms packets, no
change on the server.

### Network (probe 0.3)

| measure | value |
|---|---|
| "Find the PC" (broadcast) | answered in 185 ms |
| ping, 20 pings over Wi-Fi | 20/20; min 4.0 ms, median 5.7 ms, max 10.9 ms |
| burst (first -> last chunk of a frame) | ~3.3-3.5 MB/s, from small frames (5-6 chunks): indicative only |

- Broadcast discovery works on the 3DS, so the PSP's "Find the PC" carries
  over as is. The 0.2 run that found nothing had no server answering.
- The stream part of this run is **not valid**: the server was started
  without `--size 400x240`, so it picked H.264 with P frames (480x272), and
  it sends that even to a client that does not set `PS_CAP_H264` (it only
  logs a warning). The probe expected JPEG (frames read as `0x0`, decode
  failed, P-frame losses). Probe 0.4 recognises the payload, says how to
  start the server and skips the invalid part.
- The server behaviour is worth a change in PSP-Stream (protocol v6, or
  even v5): with a client that does not set `PS_CAP_H264`, fall back to
  JPEG instead of sending H.264 it cannot decode.
- If the burst figure holds with bigger frames, the 3DS Wi-Fi is several
  times the PSP's 802.11b (~400-600 KB/s): JPEG at 18 KB x 55 fps
  (~1 MB/s) would fit.
