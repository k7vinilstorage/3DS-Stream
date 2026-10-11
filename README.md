# 3DS-Stream

[![build](https://github.com/k7vinilstorage/3ds-stream/actions/workflows/build.yml/badge.svg)](https://github.com/k7vinilstorage/3ds-stream/actions/workflows/build.yml)

**English** · [Português](README.pt-BR.md)

The Nintendo 3DS client for [PSPStream](https://github.com/k7vinilstorage/PSP-Stream):
the PC screen and audio on the 3DS over Wi-Fi, and the 3DS buttons back to
the PC, with the same server the PSP uses.

**Status: phase 0, hardware probes.** There is no client yet. The plan is
the [3DS Port Plan](https://github.com/k7vinilstorage/PSP-Stream/wiki/3DS-Port-Plan)
in the PSP-Stream wiki: H.264 through the New 3DS hardware decoder (MVD),
JPEG on the Old 3DS, 400x240 on the top screen, protocol v5 first.

## The probe

`probe/` measures on the console what the plan depends on: whether MVD
decodes the server's H.264 packets and how many frames it holds back, how
fast software JPEG and the Y2R converter are, the Wi-Fi throughput and round
trip with the real server, and NDSP audio. How to run it and read the
results: [docs/Probe.md](docs/Probe.md).

## Layout

| path | what |
|---|---|
| `probe/` | the probe homebrew (devkitARM + libctru + libjpeg-turbo) |
| `probe/romfs/` | test data: the server's own H.264 and JPEG at 400x240, IMA ADPCM audio |
| `probe/source/protocol.h`, `ima.c` | copies from PSP-Stream (`psp/src/`), kept identical |
| `tools/probe_clips.py` | regenerates the test data with the PSP-Stream server encoders |
| `tools/dump2png.py` | MVD output dumps to PNG |
| `tests/host/` | the probe code built on the PC, run by CI |

## License

MIT, like PSP-Stream.
