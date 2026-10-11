# The hardware probe (phase 0)

The [3DS port plan](https://github.com/k7vinilstorage/PSP-Stream/wiki/3DS-Port-Plan)
depends on a few things only the console can answer. `probe/` is a small
homebrew that measures them and writes everything to
`sdmc:/3ds/3ds-stream/probe_result.txt`. Nothing in it is the client yet.

## Running it

1. Get `3ds-stream-probe.3dsx` from the latest run of the **build** workflow
   (artifact `3ds-stream-probe`), or build it (below). Copy it to
   `sdmc:/3ds/` and open it from the Homebrew Launcher.
2. For the network part, on the PC, a PSPStream server at the 3DS size:
   ```sh
   pspstream --source test --size 400x240
   ```
   (`--source kms` or `portal` works too; the test pattern is enough.) The
   probe finds the PC with the same broadcast as "Find the PC" on the PSP;
   if the router blocks it, put `server=192.168.x.y` in
   `sdmc:/3ds/3ds-stream/server.txt`.
3. For audio, the DSP firmware dump must be at `sdmc:/3ds/dspfirm.cdc`
   (the usual DSP1 homebrew makes it). Without it, the audio step says so
   and the rest runs.
4. Press **A**. It takes about a minute. Send back `probe_result.txt` (and,
   on a New 3DS, the `mvd_dump_*.bin` next to it).

If the console freezes in a step, turn it off and run the probe again: the
step that was running goes to `crashed.txt` and is skipped, so the others
still run. Delete `crashed.txt` to try it again.

## What it measures

| step | question | what to look at |
|---|---|---|
| system | which model, firmware, free memory | `model:` line |
| JPEG | software JPEG decode at 400x240, qualities 50-95, on a busy test pattern (`src2`) and a desktop image (`card`): libjpeg straight to RGB565, the same with the fast IDCT, and YCbCr planes + the **Y2R** hardware converter | ms/frame per column; on a New 3DS, at 268 MHz (the Old 3DS clock) and 804 MHz. 16.7 ms = 60 fps |
| audio | IMA ADPCM decode cost (the PSP's `ima.c`), NDSP start latency, 20 ms chunks with 4 queued (as they come from the network) | `% of a core`, `start latency`, `underruns` |
| network | the PSPStream server over UDP, protocol v5 as is: broadcast discovery, 20 pings, then 6 s of JPEG one frame at a time (network only), and 6 s decoding and showing each frame | `ping`, `fps`, `lost`, `burst KB/s` (the Wi-Fi throughput) |
| MVD | New 3DS only. The server's own H.264 packets (`oh_*` clips, made by its encoder): all-IDR, P frames with the 2 copies the PSP needs, P frames without copies, with and without the AUD NALs, IDR every 10 frames, and a 5-frame loss followed by an IDR | `ok`/`ERROR`, ms/frame, and the **lag** lines |

### The MVD lag lines

Each test frame carries its own number in 8 white/black blocks in the
top-left corner. After every slice the probe renders and reads which frame
came out:

- `lag after own slice: 0:N` — the frame came out as soon as its own data
  went in. All in `0:` means MVD holds nothing back, and the 3DS does **not**
  need the copies the PSP needs (protocol v6 can drop them for the 3DS).
- `1:` / `2:` — MVD held 1 or 2 frames. Then `lag after packet` on the
  `p3` clips says whether the server's 2 copies already push the frame out
  (`0:` there).
- `unreadable` — the output could not be read: wrong layout, colours or a
  corrupt frame. The `mvd_dump_*.bin` files show what MVD wrote:
  `python3 tools/dump2png.py mvd_dump_0.bin`.
- `after the loss: recovered on the IDR` — after skipping 5 packets, the IDR
  brings the picture back without resetting the decoder (on the PSP, this
  case powered the console off until the stream called Stop first).

## Building

With [devkitPro](https://devkitpro.org/wiki/Getting_Started) (devkitARM,
libctru, `3ds-libjpeg-turbo`):

```sh
make -C probe
```

or with Docker:

```sh
docker run --rm -v "$PWD":/src -w /src/probe devkitpro/devkitarm make
```

The test data in `probe/romfs/` is committed. To regenerate it (needs
ffmpeg, Pillow, libopenh264 and a PSP-Stream checkout next to this one):

```sh
python3 tools/probe_clips.py --psp-stream ../PSP-Stream
```

## Tests on the PC

`tests/host/` builds the same probe code with the PC's gcc, with a small
`3ds.h` shim instead of libctru: the MVD is fake (it checks every NAL the
probe hands over) and there is no Y2R. CI runs it on every push.

```sh
make -C tests/host check                       # data, NALs, frame markers, JPEG
tests/host/run_net.sh ../PSP-Stream            # network against the real server
```
