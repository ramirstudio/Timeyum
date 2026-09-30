"""Apply the Timeyum timeshift look to images or rendered sequences outside Cinema 4D.

Examples
    python tools/timeyum_cli.py "render/beauty_*.png" -o out
    python tools/timeyum_cli.py shot.exr -o out --colorspace linear --set profile=camera --set length=1
    python tools/timeyum_cli.py "seq/*.png" -o out --preset look.json --set shake=1 --fps 24

Values use the core units: percentages as fractions (length=0.6 is 60% of the
frame height), angles in degrees. Needs numpy and Pillow; EXR and 16-bit files
are read through imageio when it is installed.
"""

import argparse
import glob
import json
import os
import re
import sys
import time

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir, "timeyum"))
import timeyum_core as core  # noqa: E402

ENUMS = {
    "profile": {"fade": 0, "exponential": 1, "camera": 2, "curve": 3},
    "edge": {"wrap": 0, "extend": 1, "mirror": 2, "black": 3},
    "blend": {"exposure": 0, "add": 1, "screen": 2, "lighten": 3},
    "colorspace": {"linear": 0, "srgb": 1, "gamma24": 2, "logc3": 3, "slog3": 4},
}


def parse_value(key, text):
    if key not in core.DEFAULTS:
        raise SystemExit("unknown parameter: %s" % key)
    default = core.DEFAULTS[key]
    low = text.strip().lower()
    if key in ENUMS and low in ENUMS[key]:
        return ENUMS[key][low]
    if isinstance(default, bool):
        return low in ("1", "true", "yes", "on")
    if isinstance(default, int):
        return int(float(text))
    if key in ("tint", "curve"):
        return [float(v) for v in text.split(",")]
    return float(text)


def read_image(path):
    ext = os.path.splitext(path)[1].lower()
    data = None
    try:
        import imageio.v3 as iio
        data = iio.imread(path)
    except Exception:
        if ext == ".exr":
            raise SystemExit("reading EXR needs imageio with an EXR backend (pip install imageio[freeimage] or imageio[opencv])")
    if data is None:
        from PIL import Image
        with Image.open(path) as im:
            data = np.asarray(im.convert("RGBA" if "A" in im.getbands() else "RGB"))
    if data.ndim == 2:
        data = np.repeat(data[..., None], 3, axis=2)
    if data.dtype == np.uint8:
        return data.astype(np.float32) / 255.0, np.uint8
    if data.dtype == np.uint16:
        return data.astype(np.float32) / 65535.0, np.uint16
    return data.astype(np.float32), np.float32


def write_image(path, img, dtype):
    if dtype == np.uint8:
        data = (np.clip(img, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    elif dtype == np.uint16:
        data = (np.clip(img, 0.0, 1.0) * 65535.0 + 0.5).astype(np.uint16)
    else:
        data = img.astype(np.float32)
    try:
        import imageio.v3 as iio
        iio.imwrite(path, data)
        return
    except Exception:
        if dtype != np.uint8:
            raise SystemExit("writing %s needs imageio with a suitable backend" % path)
    from PIL import Image
    Image.fromarray(data).save(path)


def frame_number(path, fallback):
    digits = re.findall(r"(\d+)", os.path.splitext(os.path.basename(path))[0])
    return int(digits[-1]) if digits else fallback


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("inputs", nargs="+", help="image files or glob patterns")
    ap.add_argument("-o", "--output", required=True, help="output folder")
    ap.add_argument("--preset", help="JSON file with parameters")
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE", help="override one parameter")
    ap.add_argument("--colorspace", help="input transfer: linear, srgb, gamma24, logc3, slog3 (default: srgb for 8/16-bit files, linear for float)")
    ap.add_argument("--fps", type=float, default=24.0)
    ap.add_argument("--start-frame", type=int, default=0, help="frame number when file names have no digits")
    ap.add_argument("--list", action="store_true", help="print the parameters with their defaults and exit")
    args = ap.parse_args(argv)

    if args.list:
        for key, value in core.DEFAULTS.items():
            if key not in ("weave_x", "weave_y", "stage"):
                print("%-16s %s" % (key, value))
        return 0

    params = {}
    if args.preset:
        with open(args.preset) as fh:
            params.update(json.load(fh))
    for item in args.set:
        key, _, value = item.partition("=")
        params[key.strip()] = parse_value(key.strip(), value)

    files = []
    for pattern in args.inputs:
        matches = sorted(glob.glob(pattern))
        files.extend(matches if matches else [pattern])
    os.makedirs(args.output, exist_ok=True)

    for index, path in enumerate(files):
        img, dtype = read_image(path)
        p = dict(params)
        if args.colorspace:
            p["colorspace"] = ENUMS["colorspace"][args.colorspace.lower()]
        elif "colorspace" not in p:
            p["colorspace"] = core.CS_LINEAR if dtype == np.float32 else core.CS_SRGB
        frame = frame_number(path, args.start_frame + index)
        started = time.time()
        out = core.process(img, p, frame=frame, fps=args.fps)
        target = os.path.join(args.output, os.path.basename(path))
        write_image(target, out, dtype)
        print("%s -> %s  (frame %d, %.2fs)" % (path, target, frame, time.time() - started))
    return 0


if __name__ == "__main__":
    sys.exit(main())
