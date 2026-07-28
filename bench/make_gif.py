"""Assemble the README cube GIF from a --capture raw frame dump.

Usage: python bench/make_gif.py CAPTURE_RAW [--out docs/cube.gif]
       [--width 512] [--colors 128] [--stride 1]

CAPTURE_RAW is the path given to cube's --capture flag; the sidecar meta at
CAPTURE_RAW + ".json" must sit next to it. Frames in the raw dump are
bottom-up (glReadPixels row order) and are flipped here. Pillow is a
dev-only dependency, same status as plot_frames.py's matplotlib: never
needed to build, never installed in CI.
"""
import argparse
import json
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("FATAL: Pillow is required for GIF assembly (pip install Pillow) -- "
             "a dev-only dependency like matplotlib; never needed to build, never in CI")

HARD_CEILING_BYTES = 3_000_000  # a committed GIF larger than this is a bug


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("raw", help="raw dump written by cube --capture")
    ap.add_argument("--out", default="docs/cube.gif")
    ap.add_argument("--width", type=int, default=512, help="output width, px")
    ap.add_argument("--colors", type=int, default=128, help="palette size")
    ap.add_argument("--stride", type=int, default=1,
                    help="keep every Nth frame; must divide the frame count "
                         "or the rotation loop would seam")
    args = ap.parse_args()

    meta_path = args.raw + ".json"
    if not os.path.exists(meta_path):
        sys.exit(f"FATAL: missing sidecar meta {meta_path}")
    with open(meta_path) as f:
        meta = json.load(f)
    if meta.get("resized"):
        sys.exit("FATAL: capture was cut short by a mid-run window resize; re-capture")
    w, h, ch = meta["width"], meta["height"], meta["channels"]
    frames, fps = meta["frames"], meta["fps"]
    if frames != meta["requested"]:
        sys.exit(f"FATAL: capture incomplete: frames={frames} "
                 f"requested={meta['requested']}")
    if frames == 0 or frames % args.stride != 0:
        sys.exit(f"FATAL: --stride {args.stride} does not divide {frames} frames "
                 "-- the rotation loop would seam")
    expected = frames * w * h * ch
    actual = os.path.getsize(args.raw)
    if actual != expected:
        sys.exit(f"FATAL: raw size {actual} != frames*w*h*channels = {expected}")

    out_w = args.width
    out_h = round(h * out_w / w)
    frame_bytes = w * h * ch
    out_frames = []
    with open(args.raw, "rb") as f:
        for i in range(0, frames, args.stride):
            f.seek(i * frame_bytes)
            img = Image.frombytes("RGB", (w, h), f.read(frame_bytes))
            # glReadPixels rows run bottom-up.
            img = img.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            out_frames.append(img.resize((out_w, out_h), Image.Resampling.LANCZOS))

    # One global palette from the first frame, no dither: the cube is
    # flat-shaded, so a per-frame palette flickers and dithering only
    # bloats the LZW stream and fuzzes the face edges.
    palette = out_frames[0].quantize(colors=args.colors)
    out_frames = [fr.quantize(colors=args.colors, palette=palette,
                              dither=Image.Dither.NONE)
                  for fr in out_frames]

    duration_ms = round(1000 * args.stride / fps)
    if duration_ms < 20 or duration_ms % 10:
        sys.exit(f"FATAL: duration_ms={duration_ms} is not a whole >=2 cs GIF frame delay "
                 "(browsers clamp anything below 2 cs to 10 cs); capture at an fps where "
                 "1000*stride/fps is a multiple of 10 and >= 20 (e.g. --fps=50 or 25)")
    out_frames[0].save(args.out, save_all=True, append_images=out_frames[1:],
                       duration=duration_ms, loop=0, optimize=True, disposal=1)
    size = os.path.getsize(args.out)
    print(f"gif: frames={len(out_frames)} size={out_w}x{out_h} "
          f"colors={args.colors} duration_ms={duration_ms} bytes={size}")
    if size > HARD_CEILING_BYTES:
        sys.exit(f"FATAL: {args.out} is {size} bytes (> {HARD_CEILING_BYTES}); "
                 "shrink with --colors 64, --width 384, or --stride 2 and re-run")


if __name__ == "__main__":
    main()
