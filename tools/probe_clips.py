#!/usr/bin/env python3
"""Generates the probe's test data in probe/romfs/ with PSPStream's own encoders.

    python3 tools/probe_clips.py --psp-stream ../PSP-Stream

The H.264 and JPEG data comes from the server code (server/h264.py,
server/imaging.py), at 400x240: the same bytes the server would send to a
3DS. Each frame carries its own number (0-255) drawn in 8 blocks of 32x32
in the top-left corner (white = bit 1), like psp/probe in PSP-Stream: after
decoding, the 3DS reads the blocks and knows which frame came out. If after
packet N the decoder outputs frame N-2, it holds 2 frames back, and that is
latency added to the stream.

Needs ffmpeg (lavfi testsrc2), Pillow and libopenh264 (the server's direct
openh264 backend). The generated files are committed, so this only needs to
run to change the data.

Formats (little-endian; each record padded with zeros to a multiple of 4):
  clips.bin  "H3DSCLP1", u32 clips
             per clip: char name[24], u32 flags, u32 packets, u32 bytes,
                       u32 size[packets], data (Annex B, one server packet each)
             flags: CLIP_COPIES (packet = AUD + frame + 2 x (AUD + copy)),
                    CLIP_INTRA (every frame IDR, SPS + PPS in each)
  jpegs.bin  "J3DSJPG1", u32 sets
             per set: char name[24], u32 quality, u32 frames, u32 bytes,
                      u32 size[frames], data
  audio.bin  "A3DSIMA1", u32 rate, u16 channels, u16 samples per block,
             u32 bytes per block, u32 blocks, data (WAV IMA ADPCM blocks,
             the server's layout: server/audio.py)
"""
import argparse
import math
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "probe" / "romfs"

W, H = 400, 240
CLIP_COPIES = 0x1
CLIP_INTRA = 0x2

# Moving content (testsrc2) + the frame number in binary, as in PSP-Stream's tools/h264_probe_clips.py.
MARKER = ",".join(
    ["drawbox=x=0:y=0:w=256:h=32:color=black:t=fill"]
    + [f"drawbox=x={k * 32}:y=0:w=32:h=32:color=white:t=fill:enable='eq(mod(floor(n/{1 << k}),2),1)'"
       for k in range(8)]
)


def frames(count: int):
    """I420 400x240 frames of testsrc2 with the marker, numbered from 0."""
    cmd = ["ffmpeg", "-v", "error", "-nostdin", "-f", "lavfi", "-i", f"testsrc2=size={W}x{H}:rate=30",
           "-vf", MARKER, "-frames:v", str(count), "-pix_fmt", "yuv420p", "-f", "rawvideo", "-"]
    raw = subprocess.run(cmd, check=True, capture_output=True).stdout
    size = W * H * 3 // 2
    assert len(raw) == size * count, (len(raw), size * count)
    return [raw[i * size:(i + 1) * size] for i in range(count)]


def pad4(data: bytes) -> bytes:
    return data + b"\0" * (-len(data) % 4)


def name24(name: str) -> bytes:
    raw = name.encode()
    assert len(raw) < 24, name
    return raw.ljust(24, b"\0")


def clip_record(name: str, flags: int, packets) -> bytes:
    data = b"".join(packets)
    head = name24(name) + struct.pack("<III", flags, len(packets), len(data))
    return pad4(head + struct.pack(f"<{len(packets)}I", *map(len, packets)) + data)


def h264_clips(h264, quality: int):
    src = frames(120)
    clips = []

    # --codec h264: every frame IDR (SPS + PPS + IDR each)
    enc = h264.H264Encoder(W, H, quality, backend="openh264")
    clips.append(clip_record("oh_intra", CLIP_INTRA, [enc.encode(f) for f in src[:60]]))
    enc.close()

    # --codec h264p as the server sends it today: AUD + frame + 2 copies
    enc = h264.H264PEncoder(W, H, quality, backend="openh264")
    clips.append(clip_record("oh_p3", CLIP_COPIES, [enc.encode(f) for f in src[:60]]))
    enc.close()

    # the same without the copies: what a "no copies" capability would send
    enc = h264.H264PEncoder(W, H, quality, copies=0, backend="openh264")
    clips.append(clip_record("oh_p1", 0, [enc.encode(f) for f in src[:60]]))
    enc.close()

    # P frames with copies, IDR every 10 packets (what the stream does after a loss)
    enc = h264.H264PEncoder(W, H, quality, backend="openh264")
    packets = []
    for i, f in enumerate(src):
        if i and i % 10 == 0:
            enc.request_idr(force=True)
        packets.append(enc.encode(f))
    clips.append(clip_record("oh_idr10", CLIP_COPIES, packets))
    enc.close()
    return clips


def jpeg_sets(imaging, card: Path):
    src = frames(8)
    card_i420 = imaging.image_to_i420(str(card), W, H) if card.exists() else None
    sets = []
    for q in (50, 70, 85, 95):
        jpgs = [imaging.i420_to_jpeg(f, W, H, q) for f in src]
        sets.append(set_record(f"src2_q{q}", q, jpgs))
        if card_i420:
            sets.append(set_record(f"card_q{q}", q, [imaging.i420_to_jpeg(card_i420, W, H, q)]))
    return sets


def set_record(name: str, quality: int, jpgs) -> bytes:
    data = b"".join(jpgs)
    head = name24(name) + struct.pack("<III", quality, len(jpgs), len(data))
    return pad4(head + struct.pack(f"<{len(jpgs)}I", *map(len, jpgs)) + data)


def ima_encode_block(samples, channels: int, audio, carry) -> bytes:
    """One WAV IMA ADPCM block (the layout server/audio.py decodes). samples[c] = list of int16;
    the block holds 1 + 8k samples per channel. carry[c]: step index carried between blocks."""
    out = bytearray()
    state = []
    for c in range(channels):
        first = samples[c][0]
        out += struct.pack("<hBB", first, carry[c], 0)
        state.append([first, carry[c]])
    n = len(samples[0])
    for g in range((n - 1) // 8):
        for c in range(channels):
            nibs = []
            for k in range(8):
                pred, index = state[c]
                step = audio.STEP_TABLE[index]
                diff = samples[c][1 + g * 8 + k] - pred
                nib = 8 if diff < 0 else 0
                diff = abs(diff)
                # the encoder mirrors the decoder: shifts and additions
                delta = step >> 3
                if diff >= step:
                    nib |= 4
                    diff -= step
                    delta += step
                if diff >= step >> 1:
                    nib |= 2
                    diff -= step >> 1
                    delta += step >> 1
                if diff >= step >> 2:
                    nib |= 1
                    delta += step >> 2
                pred = pred - delta if nib & 8 else pred + delta
                state[c] = [max(-32768, min(32767, pred)), max(0, min(88, index + audio.INDEX_TABLE[nib]))]
                nibs.append(nib)
            out += bytes(nibs[i] | nibs[i + 1] << 4 for i in range(0, 8, 2))
    for c in range(channels):
        carry[c] = state[c][1]
    return bytes(out)


def audio_record(audio, rate: int = 44100, channels: int = 2, seconds: float = 2.0) -> bytes:
    """A chord (A4 left, E5 right) in 20 ms IMA blocks, as the server sends them."""
    spb = audio.block_samples(rate)
    blocks = []
    pos = 0
    index = [0] * channels
    total = int(rate * seconds)
    while pos + spb <= total:
        chans = []
        for c, freq in enumerate((440.0, 659.25)[:channels]):
            chans.append([int(9000 * math.sin(2 * math.pi * freq * (pos + i) / rate)) for i in range(spb)])
        block = ima_encode_block(chans, channels, audio, index)
        assert len(block) == audio.block_align(spb, channels)
        # sanity check against the server's reference decoder
        dec = audio.ima_decode_block(block, channels)
        if pos:  # the very first block starts from the smallest step
            assert max(abs(dec[i * channels] - chans[0][i]) for i in range(spb)) < 1500
        blocks.append(block)
        pos += spb
    head = b"A3DSIMA1" + struct.pack("<IHHII", rate, channels, spb, len(blocks[0]), len(blocks))
    return head + b"".join(blocks)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--psp-stream", type=Path, default=ROOT.parent / "PSP-Stream",
                    help="PSP-Stream checkout (its server/ has the encoders)")
    ap.add_argument("--quality", type=int, default=80, help="H.264 quality, the server's 1-100 scale")
    args = ap.parse_args()
    server = args.psp_stream / "server"
    if not (server / "h264.py").exists():
        sys.exit(f"{server}/h264.py not found: pass --psp-stream")
    sys.path.insert(0, str(server))
    import audio  # noqa: E402  (PSP-Stream server modules)
    import h264  # noqa: E402
    import imaging  # noqa: E402
    import openh264  # noqa: E402
    if not openh264.available():
        sys.exit("libopenh264 not found (Debian/Ubuntu: apt install libopenh264-7)")

    OUT.mkdir(parents=True, exist_ok=True)
    clips = h264_clips(h264, args.quality)
    (OUT / "clips.bin").write_bytes(b"H3DSCLP1" + struct.pack("<I", len(clips)) + b"".join(clips))
    sets = jpeg_sets(imaging, args.psp_stream / "assets" / "testcard.jpg")
    (OUT / "jpegs.bin").write_bytes(b"J3DSJPG1" + struct.pack("<I", len(sets)) + b"".join(sets))
    (OUT / "audio.bin").write_bytes(audio_record(audio))
    for name in ("clips.bin", "jpegs.bin", "audio.bin"):
        print(f"{name}: {(OUT / name).stat().st_size} bytes")


if __name__ == "__main__":
    main()
