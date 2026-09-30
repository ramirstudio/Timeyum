"""Compares the C++ core (timeyum_cli) with the NumPy reference engine.

    python tests/compare_reference.py build/timeyum_cli
"""
import os
import subprocess
import sys
import tempfile
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "reference"))
import timeyum_core as ref  # noqa: E402

NAMES = {"profile": ["fade", "exponential", "camera", "curve"], "edge": ["wrap", "extend", "mirror", "black"],
         "blend": ["exposure", "add", "screen", "lighten"], "colorspace": ["linear", "srgb", "gamma24", "logc3", "slog3"]}


def scene(W, H, seed=1):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    img = np.full((H, W, 3), 0.01, np.float32)
    for _ in range(40):
        cx, cy = rng.uniform(0.3, 0.7) * W, rng.uniform(0.3, 0.7) * H
        r = rng.uniform(0.02, 0.09) * H
        m = np.exp(-(((x - cx) ** 2 + (y - cy) ** 2) / r ** 2))
        img += (m * rng.uniform(0.3, 1.0))[..., None] * rng.uniform(0.4, 1.0, 3).astype(np.float32)
    img[int(H * 0.4), int(W * 0.52)] = 8.0
    img[int(H * 0.7), int(W * 0.2)] = [5.0, 1.0, 0.2]
    return np.concatenate([img, rng.random((H, W, 1), dtype=np.float32) * 0.5 + 0.5], axis=2)


def write_tyf(path, a):
    with open(path, "wb") as f:
        f.write(b"TYF1 %d %d %d\n" % (a.shape[1], a.shape[0], a.shape[2]))
        f.write(np.ascontiguousarray(a, np.float32).tobytes())


def read_tyf(path):
    with open(path, "rb") as f:
        _, w, h, c = f.readline().split()
        return np.frombuffer(f.read(), np.float32).reshape(int(h), int(w), int(c))


def cli_args(params):
    out = []
    for k, v in params.items():
        if k in NAMES:
            v = NAMES[k][v]
        elif isinstance(v, bool):
            v = int(v)
        elif isinstance(v, (list, tuple)):
            v = ",".join(str(x) for x in v)
        out.append("%s=%s" % (k, v))
    return out


CASES = {
    "default": {},
    "camera": dict(profile=2, length=1.0, timing_shift=100.0),
    "camera gap": dict(profile=2, length=1.0, timing_shift=40.0, frame_gap=0.02, shutter_angle=300.0, claw_ease=0.4),
    "exponential symmetric": dict(profile=1, decay=5.0, symmetric=True, length=0.3),
    "back+start": dict(back_length=0.5, start_offset=0.05, falloff=0.9, falloff_curve=2.0),
    "curve": dict(profile=3, curve=[1.0, 0.2, 0.8, 0.0]),
    "diagonal cleanup": dict(angle=37.0, cleanup=2.0, length=0.35),
    "horizontal mirror": dict(angle=0.0, edge=2, length=0.5),
    "extend": dict(edge=1, length=0.7),
    "black chroma": dict(edge=3, chroma=0.12, length=0.4),
    "ghost": dict(ghost=True, ghost_count=3, ghost_offset=0.07, ghost_strength=0.2, ghost_length=0.02),
    "threshold add": dict(threshold=0.5, knee=0.4, blend=1, gain=1.4, smear=0.6),
    "screen tint sat": dict(blend=2, tint=[1.0, 0.8, 0.5], saturation=0.4),
    "lighten": dict(blend=3),
    "breakup": dict(breakup=0.6, breakup_scale=5.0, angle=90.0),
    "breakup diagonal": dict(breakup=0.6, angle=50.0),
    "roll bar": dict(roll=0.13, roll_bar=0.04, roll_bar_soft=0.5),
    "shake": dict(shake=True, shake_roll=0.1, shake_weave_x=2.0, shake_weave_y=3.0, shake_ghost=0.5, ghost=True, shake_angle=10.0),
    "opacity": dict(opacity=0.4),
    "srgb": dict(colorspace=1, length=0.5),
    "logc3": dict(colorspace=3, length=0.5),
    "slog3": dict(colorspace=4, length=0.5),
    "gamma24": dict(colorspace=2),
}


def main():
    cli = sys.argv[1]
    sizes = [(160, 90), (251, 143)]  # the second has prime-ish sizes that exercise Bluestein
    worst = 0.0
    with tempfile.TemporaryDirectory() as tmp:
        for W, H in sizes:
            img = scene(W, H)
            if True:
                write_tyf(os.path.join(tmp, "in.tyf"), img)
            for name, params in CASES.items():
                inp = img.copy()
                if params.get("colorspace", 0):
                    inp[..., :3] = np.clip(inp[..., :3], 0, 1)
                write_tyf(os.path.join(tmp, "in.tyf"), inp)
                frame, fps = 7.0, 24.0
                expected = ref.process(inp, dict(params), frame=frame, fps=fps)
                t = time.time()
                subprocess.run([cli, os.path.join(tmp, "in.tyf"), os.path.join(tmp, "out.tyf"), "frame=%s" % frame, "fps=%s" % fps,
                                "space=%s" % NAMES["colorspace"][params.get("colorspace", 0)]] + cli_args({k: v for k, v in params.items() if k != "colorspace"}), check=True)
                got = read_tyf(os.path.join(tmp, "out.tyf"))
                err = float(np.abs(got - expected).max())
                scale = max(float(np.abs(expected).max()), 1.0)
                rel = err / scale
                worst = max(worst, rel)
                print("%-22s %dx%d  max abs err %.2e  (rel %.1e)  %.2fs %s" % (name, W, H, err, rel, time.time() - t, "" if rel < 2e-3 else "<-- CHECK"))
    print("worst relative error: %.2e" % worst)
    sys.exit(0 if worst < 2e-3 else 1)


if __name__ == "__main__":
    main()
