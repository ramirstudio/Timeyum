"""Engine sweep plus a smoke test of timeyum.pyp against tests/mock/c4d.py.

    pip install numpy
    python tests/run_tests.py
"""
import itertools
import os
import runpy
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN = os.path.join(HERE, os.pardir, "timeyum")
sys.path.insert(0, PLUGIN)
sys.path.insert(0, os.path.join(HERE, "mock"))

import timeyum_core as core  # noqa: E402


def scene(W=200, H=120, alpha=True, seed=1):
    rng = np.random.default_rng(seed)
    img = np.full((H, W, 3), 0.01, np.float32)
    y, x = np.mgrid[0:H, 0:W]
    blob = np.exp(-(((x - W * 0.5) / (W * 0.1)) ** 2 + ((y - H * 0.55) / (H * 0.15)) ** 2))
    img += (blob * 0.8)[..., None] * np.array([1.0, 0.85, 0.6], np.float32)
    img[int(H * 0.4), int(W * 0.52)] = 8.0
    if alpha:
        img = np.concatenate([img, rng.random((H, W, 1), dtype=np.float32)], axis=2)
    return img


def test_identities():
    img = scene(alpha=False)
    assert np.allclose(core.process(img, dict(smear=0.0)), img, atol=1e-5)
    assert np.allclose(core.process(img, dict(opacity=0.0)), img, atol=1e-6)
    assert np.allclose(core.process(img, dict(profile=core.PROFILE_CAMERA, timing_shift=0.0)), img, atol=1e-5)
    for p in ({}, dict(profile=core.PROFILE_CAMERA, length=1.0, timing_shift=120.0), dict(angle=37.0, symmetric=True)):
        out = core.process(img, p)
        assert abs(out.mean() - img.mean()) < 1e-4 * img.mean(), p


def test_direction():
    img = np.zeros((100, 60, 3), np.float32)
    img[70, 30] = 1.0
    out = core.process(img, dict(length=0.3, falloff=0.0))
    assert out[50, 30, 0] > 1e-3 and out[90, 30, 0] < 1e-6  # angle 90: streak goes up only


def test_sweep():
    small = scene()
    count = 0
    for edge, prof, sym, L, ang, thr, blend, cs in itertools.product(
            range(4), range(4), (0, 1), (0.0, 0.3, 3.5), (90, -90, 0, 33, 180), (0, 0.4), range(4), range(5)):
        count += 1
        if count % 7:
            continue
        p = dict(edge=edge, profile=prof, symmetric=bool(sym), length=L, angle=ang, threshold=thr, blend=blend,
                 colorspace=cs, curve=[1, 0.2, 0.8, 0], frame_gap=0.02, ghost=bool(count % 2), shake=bool(count % 3),
                 roll=0.1, roll_bar=0.02, timing_shift=-40 + count % 400, shutter_angle=(count * 13) % 360,
                 pulldown_angle=(count * 7) % 360, chroma=0.1 * (count % 3 - 1), breakup=0.3, cleanup=count % 3)
        inp = small.copy()
        if cs:
            inp[..., :3] = np.clip(inp[..., :3], 0, 1)
        out = core.process(inp, p, frame=count, fps=24)
        assert out.shape == inp.shape and np.isfinite(out).all(), p


def test_plugin_glue():
    import c4d
    mod = runpy.run_path(os.path.join(PLUGIN, "timeyum.pyp"), run_name="__main__")
    tp = mod["tp"]
    assert c4d.registered["desc"] == "VPtimeyum"
    vp = c4d.registered["g"]()
    node = c4d._Node()
    assert vp.Init(node)
    params = mod["read_params"](node.bc)
    for key, value in tp.DEFAULTS.items():
        if key in ("curve", "weave_x", "weave_y"):
            continue
        assert np.allclose(params[key], value), key
    img = scene()
    expected = core.process(img, params, frame=12.0, fps=24)
    for accept in (True, False):
        buf = c4d.VPBuffer(img, accept_bytearray=accept)
        for call, opened in ((0, True), (1, True), (3, True), (4, False), (3, False), (1, False), (0, False)):
            vps = dict(vp=call, open=opened, render=c4d._Render(buf), doc=c4d._Doc(), thread=c4d._Thread(),
                       time=c4d.BaseTime(0.5))
            assert vp.Execute(node, vps) == c4d.RENDERRESULT_OK
        assert np.allclose(buf.img, expected, atol=1e-6)
    node.bc[tp.TY_PROFILE] = tp.PROFILE_CAMERA
    enabled = lambda i: vp.GetDEnabling(node, c4d.DescID(i), None, 0, None)  # noqa: E731
    assert enabled(tp.TY_TIMING_SHIFT) and not enabled(tp.TY_SMEAR) and not enabled(tp.TY_GHOST_COUNT)


def test_resource_ids():
    import re
    import timeyum_params as tp
    res_dir = os.path.join(PLUGIN, "res")
    h = open(os.path.join(res_dir, "description", "vptimeyum.h")).read()
    res = open(os.path.join(res_dir, "description", "vptimeyum.res")).read()
    ids = dict((k, int(v)) for k, v in re.findall(r"\b(TY_[A-Z0-9_]+)\s*=\s*(\d+)", h))
    for folder in ("strings_en-US", "strings_us"):
        s = open(os.path.join(res_dir, folder, "description", "vptimeyum.str")).read()
        assert set(re.findall(r"^\s*(TY_[A-Z0-9_]+)\s+\"", s, re.M)) == set(ids), folder
    assert set(re.findall(r"\b(TY_[A-Z0-9_]+)\b", res)) == set(ids)
    for name, value in vars(tp).items():
        if name.startswith("TY_"):
            assert ids[name] == value, name
    assert {pid for pid, _, _ in tp.PARAMS} == {v for k, v in ids.items() if v >= 20100}


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
