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
- Probe 0.2 adds the fast IDCT on the raw path (`rawf`), which should take
  a few more ms off.

### Audio

| measure | value |
|---|---|
| IMA ADPCM decode, 44.1 kHz stereo | 1.6% of the 268 MHz core |
| NDSP start latency | 7.2 ms |
| 20 ms chunks, 4 queued, 2 s | 0 underruns |

The PSP's audio path works as is: `ima.c` with NDSP, 20 ms packets, no
change on the server.
