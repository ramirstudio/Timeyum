"""Timeyum core: emulation of the ARRI Timing Shift Box / out-of-sync film camera look.

The film camera model
---------------------
In a film camera the claw pulls the film down while the rotating shutter is
closed. Shifting the shutter phase ("timing shift") makes part of the exposure
happen while the film is still moving: every bright point of the image paints
a streak along the film travel, then (depending on the phase) a displaced copy
once the film is at rest again. The streak that runs past the gate lands on the
neighbouring frame, which on the developed film reads as a streak wrapping
around from the opposite edge of the frame.

The module works on float32 arrays shaped (H, W, C) with C = 3 or 4 and has no
dependency on Cinema 4D, so the same code runs inside the VideoPost and in the
command line tool.

Conventions
-----------
Percent parameters are fractions (0.35 = 35%). Angles are in degrees.
``angle`` = 90 makes streaks extend upward from bright areas, 0 to the right.
Lengths and offsets expressed as a fraction of the frame height.
"""

import math

import numpy as np

from timeyum_params import (  # noqa: F401  (re-exported for the CLI)
    BLEND_ADD, BLEND_EXPOSURE, BLEND_LIGHTEN, BLEND_SCREEN,
    CS_GAMMA24, CS_LINEAR, CS_LOGC3, CS_SLOG3, CS_SRGB, DEFAULTS,
    EDGE_BLACK, EDGE_EXTEND, EDGE_MIRROR, EDGE_WRAP,
    PROFILE_CAMERA, PROFILE_EXPONENTIAL, PROFILE_FADE, PROFILE_SPLINE,
)

__all__ = ["DEFAULTS", "process", "resolve_shake", "streak_kernel"]

_LUMA = np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
_MASK32 = 0xFFFFFFFF


# ---------------------------------------------------------------------------
# Colour transfer functions
# ---------------------------------------------------------------------------

def _srgb_decode(x):
    return np.where(x <= 0.04045, x / 12.92, np.power((np.maximum(x, 0.0) + 0.055) / 1.055, 2.4))


def _srgb_encode(x):
    return np.where(x <= 0.0031308, x * 12.92, 1.055 * np.power(np.maximum(x, 0.0), 1.0 / 2.4) - 0.055)


# ARRI LogC3, EI 800
_LC_A, _LC_B, _LC_C, _LC_D = 5.555556, 0.052272, 0.247190, 0.385537
_LC_E, _LC_F, _LC_CUT = 5.367655, 0.092809, 0.010591


def _logc3_decode(t):
    lin = (np.power(10.0, (t - _LC_D) / _LC_C) - _LC_B) / _LC_A
    return np.where(t > _LC_E * _LC_CUT + _LC_F, lin, (t - _LC_F) / _LC_E)


def _logc3_encode(x):
    log = _LC_C * np.log10(np.maximum(_LC_A * x + _LC_B, 1e-10)) + _LC_D
    return np.where(x > _LC_CUT, log, _LC_E * x + _LC_F)


# Sony S-Log3
def _slog3_decode(t):
    cut = 171.2102946929 / 1023.0
    lin = np.power(10.0, (t * 1023.0 - 420.0) / 261.5) * (0.18 + 0.01) - 0.01
    return np.where(t >= cut, lin, (t * 1023.0 - 95.0) * 0.01125 / (171.2102946929 - 95.0))


def _slog3_encode(x):
    log = (420.0 + np.log10(np.maximum((x + 0.01) / (0.18 + 0.01), 1e-10)) * 261.5) / 1023.0
    return np.where(x >= 0.01125, log, (x * (171.2102946929 - 95.0) / 0.01125 + 95.0) / 1023.0)


def to_linear(rgb, space):
    if space == CS_SRGB:
        return _srgb_decode(rgb).astype(np.float32)
    if space == CS_GAMMA24:
        return np.power(np.maximum(rgb, 0.0), 2.4).astype(np.float32)
    if space == CS_LOGC3:
        return _logc3_decode(rgb).astype(np.float32)
    if space == CS_SLOG3:
        return _slog3_decode(rgb).astype(np.float32)
    return rgb


def from_linear(rgb, space):
    if space == CS_SRGB:
        return _srgb_encode(rgb).astype(np.float32)
    if space == CS_GAMMA24:
        return np.power(np.maximum(rgb, 0.0), 1.0 / 2.4).astype(np.float32)
    if space == CS_LOGC3:
        return _logc3_encode(rgb).astype(np.float32)
    if space == CS_SLOG3:
        return _slog3_encode(rgb).astype(np.float32)
    return rgb


# ---------------------------------------------------------------------------
# Deterministic noise
# ---------------------------------------------------------------------------

def _hash01(seed, stream, i):
    """Integer hash to [0, 1). Works on Python ints and numpy integer arrays."""
    i = np.asarray(i, dtype=np.int64).astype(np.uint64)
    h = (i * np.uint64(0x9E3779B1)) & np.uint64(_MASK32)
    h ^= np.uint64((int(seed) * 0x85EBCA77 + int(stream) * 0xC2B2AE3D) & _MASK32)
    h ^= h >> np.uint64(15)
    h = (h * np.uint64(0x2C1B3C6D)) & np.uint64(_MASK32)
    h ^= h >> np.uint64(12)
    h = (h * np.uint64(0x297A2D39)) & np.uint64(_MASK32)
    h ^= h >> np.uint64(15)
    return h.astype(np.float64) / 4294967296.0


def _value_noise(seed, stream, t, smooth=1.0):
    """1D value noise in [-1, 1].

    ``smooth`` = 0 holds each random value for a whole step, 1 interpolates
    smoothly between steps, values in between hold then glide.
    """
    t = np.asarray(t, dtype=np.float64)
    i0 = np.floor(t)
    f = t - i0
    smooth = min(max(float(smooth), 0.0), 1.0)
    if smooth <= 1e-6:
        f = np.zeros_like(f)
    else:
        f = np.clip((f - (1.0 - smooth)) / smooth, 0.0, 1.0)
    f = f * f * (3.0 - 2.0 * f)
    a = _hash01(seed, stream, i0)
    b = _hash01(seed, stream, i0 + 1)
    return (a + (b - a) * f) * 2.0 - 1.0


def resolve_shake(params, frame=0, fps=25.0):
    """Return a copy of params with the per-frame jitter applied."""
    p = dict(DEFAULTS)
    p.update(params)
    p["weave_x"] = 0.0
    p["weave_y"] = 0.0
    if not p["shake"] or p["shake_amount"] <= 0.0:
        return p

    amount = float(p["shake_amount"])
    t = float(frame) / max(float(fps), 1e-6) * max(float(p["shake_freq"]), 0.0)
    seed = int(p["shake_seed"])
    smooth = p["shake_smooth"]

    def n(stream):
        return float(_value_noise(seed, stream, t, smooth))

    p["length"] = max(0.0, p["length"] * (1.0 + amount * p["shake_length"] * n(1)))
    p["angle"] = p["angle"] + amount * p["shake_angle"] * n(2)
    p["smear"] = min(max(p["smear"] * (1.0 + amount * p["shake_smear"] * n(3)), 0.0), 1.0)
    p["timing_shift"] = p["timing_shift"] + amount * p["shake_timing"] * n(4)
    p["roll"] = p["roll"] + amount * p["shake_roll"] * n(5)
    p["weave_x"] = amount * p["shake_weave_x"] * n(6)
    p["weave_y"] = amount * p["shake_weave_y"] * n(7)
    p["ghost_offset"] = p["ghost_offset"] * (1.0 + amount * p["shake_ghost"] * n(8))
    p["_breakup_seed"] = seed + int(math.floor(t)) * 7919
    return p


# ---------------------------------------------------------------------------
# Streak kernel
# ---------------------------------------------------------------------------

def _profile_weights(p, t):
    """Relative exposure density along the streak, t in [0, 1] from start to tip."""
    prof = p["profile"]
    if prof == PROFILE_EXPONENTIAL:
        return np.exp(-max(p["decay"], 0.0) * t)
    if prof == PROFILE_SPLINE and p.get("curve") is not None:
        curve = np.asarray(p["curve"], dtype=np.float64)
        if curve.size >= 2:
            xs = np.linspace(0.0, 1.0, curve.size)
            return np.maximum(np.interp(t, xs, curve), 0.0)
    fall = min(max(p["falloff"], 0.0), 1.0)
    power = max(p["falloff_curve"], 0.01)
    return np.maximum(1.0 - fall * np.power(t, power), 0.0)


def _segment(start, length, p):
    """Sample one side of an artistic streak at quarter-pixel steps."""
    if length <= 0.0:
        return np.zeros(0), np.zeros(0)
    n = int(min(max(math.ceil(length * 4.0), 2), 200000))
    t = (np.arange(n) + 0.5) / n
    w = _profile_weights(p, t) * (length / n)
    return start + t * length, w


def _camera_offsets(p, travel):
    """Film displacement histogram for the open-shutter interval.

    Film rests for (360 - pulldown) degrees, then the claw moves it by one
    ``travel`` over ``pulldown`` degrees. The shutter is open from
    ``timing_shift`` to ``timing_shift + shutter_angle``. Returns
    (offsets, weights, base) where ``base`` is the share of the exposure taken
    with the film at rest in its own position.
    """
    sa = min(max(p["shutter_angle"], 1.0), 360.0)
    pd = min(max(p["pulldown_angle"], 1.0), 359.0)
    ease = min(max(p["claw_ease"], 0.0), 1.0)
    n = int(min(max(4096, 16 * travel), 262144))
    ph = p["timing_shift"] + (np.arange(n) + 0.5) / n * sa
    cycle = np.floor(ph / 360.0)
    q = ph - cycle * 360.0
    rest = 360.0 - pd
    u = np.clip((q - rest) / pd, 0.0, 1.0)
    e = (1.0 - ease) * u + ease * (u - np.sin(2.0 * math.pi * u) / (2.0 * math.pi))
    d = (cycle + e) * travel
    at_rest_home = (cycle == 0) & (q < rest)
    base = float(np.count_nonzero(at_rest_home)) / n
    moving = d[~at_rest_home]
    if moving.size == 0:
        return np.zeros(0), np.zeros(0), 1.0
    q4 = np.round(moving * 4.0).astype(np.int64)
    lo = q4.min()
    counts = np.bincount(q4 - lo)
    nz = np.nonzero(counts)[0]
    return (nz + lo) / 4.0, counts[nz].astype(np.float64) / n, base


def streak_kernel(p, height, chroma_scale=1.0):
    """Offsets (px, along the streak direction) and weights of the moving exposure.

    Returns (offsets, weights, moving_share). ``weights`` sum to
    ``moving_share``: the fraction of the exposure that leaves the base image.
    """
    H = float(height)
    offs, wts = [], []

    if p["profile"] == PROFILE_CAMERA:
        pitch = H * (1.0 + max(p["frame_gap"], 0.0))
        travel = max(p["length"], 0.0) * pitch * chroma_scale
        o, w, base = _camera_offsets(p, travel)
        smear_share = 1.0 - base
        if o.size:
            offs.append(o)
            wts.append(w)
    else:
        smear_share = min(max(p["smear"], 0.0), 1.0)
        length = max(p["length"], 0.0) * H * chroma_scale
        start = max(p["start_offset"], 0.0) * H
        back = length if p["symmetric"] else max(p["back_length"], 0.0) * length
        o1, w1 = _segment(start, length, p)
        o2, w2 = _segment(start, back, p)
        o = np.concatenate([o1, -o2])
        w = np.concatenate([w1, w2])
        total = w.sum()
        if total > 0.0 and smear_share > 0.0:
            offs.append(o)
            wts.append(w * (smear_share / total))
        else:
            smear_share = 0.0

    ghost_share = 0.0
    if p["ghost"] and p["ghost_strength"] > 0.0 and p["ghost_count"] > 0:
        glen = max(p["ghost_length"], 0.0) * H
        for i in range(int(p["ghost_count"])):
            gw = p["ghost_strength"] * (1.0 - min(max(p["ghost_decay"], 0.0), 1.0)) ** i
            if gw <= 0.0:
                continue
            centre = (i + 1) * p["ghost_offset"] * H
            if glen > 0.5:
                n = int(min(max(math.ceil(glen * 4.0), 2), 20000))
                o = centre + ((np.arange(n) + 0.5) / n - 0.5) * glen
                w = np.full(n, gw / n)
            else:
                o, w = np.array([centre]), np.array([gw])
            offs.append(o)
            wts.append(w)
            ghost_share += gw

    if not offs:
        return np.zeros(0), np.zeros(0), 0.0
    o = np.concatenate(offs)
    w = np.concatenate(wts)
    share = smear_share + ghost_share
    if share > 1.0:
        w = w / share
        share = 1.0
    return o, w, share


def _kernel_spectrum(offsets, weights, direction, shape, cleanup, perp):
    Hp, Wp = shape
    K = np.zeros(shape, dtype=np.float64)
    if offsets.size:
        x = offsets * direction[0]
        y = offsets * direction[1]
        x0 = np.floor(x)
        y0 = np.floor(y)
        fx = x - x0
        fy = y - y0
        x0 = x0.astype(np.int64)
        y0 = y0.astype(np.int64)
        for dy, wy in ((0, 1.0 - fy), (1, fy)):
            for dx, wx in ((0, 1.0 - fx), (1, fx)):
                np.add.at(K, ((y0 + dy) % Hp, (x0 + dx) % Wp), weights * wy * wx)
    Kh = np.fft.rfft2(K.astype(np.float32))
    if cleanup > 0.0:
        fy = np.fft.fftfreq(Hp)[:, None]
        fx = np.fft.rfftfreq(Wp)[None, :]
        proj = fx * perp[0] + fy * perp[1]
        Kh *= np.exp(-2.0 * math.pi ** 2 * cleanup ** 2 * proj ** 2).astype(np.float32)
    return Kh


def _smooth_len(n):
    """Smallest 2-3-5 smooth integer >= n (fast FFT size)."""
    n = max(int(n), 1)
    while True:
        m = n
        for f in (2, 3, 5):
            while m % f == 0:
                m //= f
        if m == 1:
            return n
        n += 1


# ---------------------------------------------------------------------------
# Image helpers
# ---------------------------------------------------------------------------

def _soft_threshold(rgb, threshold, knee):
    """Portion of the light that takes part in the smear (bloom style soft knee)."""
    rgb = np.maximum(rgb, 0.0)
    if threshold <= 0.0:
        return rgb, None
    lum = np.tensordot(rgb, _LUMA, axes=([2], [0]))
    k = max(threshold * knee, 1e-5)
    soft = np.clip(lum - threshold + k, 0.0, 2.0 * k)
    soft = soft * soft / (4.0 * k)
    contrib = np.maximum(soft, lum - threshold) / np.maximum(lum, 1e-6)
    ratio = np.clip(contrib, 0.0, 1.0).astype(np.float32)
    return rgb * ratio[..., None], ratio


def _shift_linear(img, dy, dx):
    """Translate (H, W, C) by fractional pixels with edge clamping."""
    H, W = img.shape[:2]
    out = img
    if abs(dy) > 1e-4:
        src = np.arange(H) - dy
        i0 = np.floor(src).astype(np.int64)
        f = (src - i0).astype(np.float32)[:, None, None]
        a = out[np.clip(i0, 0, H - 1)]
        b = out[np.clip(i0 + 1, 0, H - 1)]
        out = a + (b - a) * f
    if abs(dx) > 1e-4:
        src = np.arange(W) - dx
        i0 = np.floor(src).astype(np.int64)
        f = (src - i0).astype(np.float32)[None, :, None]
        a = out[:, np.clip(i0, 0, W - 1)]
        b = out[:, np.clip(i0 + 1, 0, W - 1)]
        out = a + (b - a) * f
    return out


def _roll_frame(img, roll_px, bar_px, soft, has_alpha):
    """Vertical frame displacement with an optional frame-line bar at the seam."""
    H = img.shape[0]
    bar = int(round(max(bar_px, 0.0)))
    P = H + bar
    padded = np.zeros((P,) + img.shape[1:], dtype=np.float32)
    padded[:H] = img
    if bar > 0:
        if has_alpha:
            padded[H:, :, 3] = 1.0
        feather = max(soft, 0.0) * bar
        if feather >= 0.5:
            y = np.arange(H, dtype=np.float32)
            dist = np.minimum(y + 0.5, H - y - 0.5)
            m = np.clip(dist / feather, 0.0, 1.0)
            m = (m * m * (3.0 - 2.0 * m)).astype(np.float32)
            padded[:H, :, :3] *= m[:, None, None]
            if has_alpha:
                padded[:H, :, 3] = 1.0 - (1.0 - padded[:H, :, 3]) * m[:, None]
    r = roll_px % P
    i = int(math.floor(r))
    f = np.float32(r - i)
    a = np.roll(padded, i, axis=0)
    if f > 1e-4:
        b = np.roll(padded, i + 1, axis=0)
        a = a + (b - a) * f
    return a[:H]


def _breakup_field(shape, perp, scale, seed):
    """Multiplicative streak-intensity variation across the streaks, in [-1, 1]."""
    H, W = shape
    scale = max(scale, 0.5)
    ys = np.arange(H, dtype=np.float64)[:, None]
    xs = np.arange(W, dtype=np.float64)[None, :]
    if abs(perp[1]) < 1e-6:
        c = xs * perp[0] + 0.0 * ys[:1]
    elif abs(perp[0]) < 1e-6:
        c = ys * perp[1] + 0.0 * xs[:, :1]
    else:
        c = xs * perp[0] + ys * perp[1]
    n1 = _value_noise(seed, 101, c / scale, 1.0)
    n2 = _value_noise(seed, 202, c / (scale * 0.37), 1.0)
    field = (0.7 * n1 + 0.3 * n2).astype(np.float32)
    return np.broadcast_to(field, (H, W))


# ---------------------------------------------------------------------------
# Main entry
# ---------------------------------------------------------------------------

def process(img, params=None, frame=0, fps=25.0):
    """Apply the timing shift look to an (H, W, 3|4) float image. Returns a new array."""
    p = resolve_shake(params or {}, frame, fps)
    src = np.nan_to_num(np.asarray(img, dtype=np.float32), nan=0.0, posinf=65504.0, neginf=0.0)
    if src.ndim != 3 or src.shape[2] < 3:
        raise ValueError("expected an (H, W, 3|4) image")
    H, W, C = src.shape
    has_alpha = C >= 4
    alpha_on = has_alpha and p["affect_alpha"]

    rgb = to_linear(src[..., :3], p["colorspace"])
    moving, ratio = _soft_threshold(rgb, p["threshold"], p["knee"])

    rad = math.radians(p["angle"])
    direction = (math.cos(rad), -math.sin(rad))
    perp = (-direction[1], direction[0])

    chroma = p["chroma"]
    scales = (1.0 + chroma, 1.0, 1.0 - chroma) if abs(chroma) > 1e-4 else (1.0, 1.0, 1.0)
    kernels = {}
    for s in set(scales):
        kernels[s] = streak_kernel(p, H, max(s, 0.0))
    share = kernels[1.0][2] if 1.0 in kernels else max(k[2] for k in kernels.values())

    out_rgb = rgb.copy()
    out_alpha = src[..., 3].copy() if has_alpha else None

    if share > 0.0:
        max_off = max((np.abs(k[0]).max() if k[0].size else 0.0) for k in kernels.values())
        cleanup = max(p["cleanup"], 0.0)
        edge = p["edge"]
        if edge == EDGE_WRAP:
            gap = int(round(max(p["frame_gap"], 0.0) * H))
            pad = ((0, gap), (0, 0))
            Hp, Wp = H + gap, W
            np_mode = "constant"
        else:
            margin = max_off + 3.0 * cleanup + 2.0
            py = int(math.ceil(margin * abs(direction[1]) + 3.0 * cleanup + 2))
            px = int(math.ceil(margin * abs(direction[0]) + 3.0 * cleanup + 2))
            Hp, Wp = _smooth_len(H + 2 * py), _smooth_len(W + 2 * px)
            pad = ((py, Hp - H - py), (px, Wp - W - px))
            np_mode = {EDGE_EXTEND: "edge", EDGE_MIRROR: "reflect"}.get(edge, "constant")

        spectra = {}
        for s, (o, w, _) in kernels.items():
            spectra[s] = _kernel_spectrum(o, w, direction, (Hp, Wp), cleanup, perp)

        def convolve(channel, spectrum):
            padded = np.pad(channel, pad, mode=np_mode)
            res = np.fft.irfft2(np.fft.rfft2(padded) * spectrum, s=(Hp, Wp))
            return res[pad[0][0]:pad[0][0] + H, pad[1][0]:pad[1][0] + W].astype(np.float32)

        smear = np.empty_like(rgb)
        for c in range(3):
            smear[..., c] = convolve(moving[..., c], spectra[scales[c]])
        np.maximum(smear, 0.0, out=smear)

        gain = max(p["gain"], 0.0)
        if gain != 1.0:
            smear *= np.float32(gain)
        if p["breakup"] > 0.0:
            field = _breakup_field((H, W), perp, p["breakup_scale"], p.get("_breakup_seed", p["shake_seed"]))
            smear *= np.maximum(1.0 + np.float32(p["breakup"]) * field, 0.0)[..., None]
        sat = p["saturation"]
        if abs(sat - 1.0) > 1e-4:
            lum = np.tensordot(smear, _LUMA, axes=([2], [0]))[..., None]
            smear = lum + (smear - lum) * np.float32(sat)
            np.maximum(smear, 0.0, out=smear)
        tint = np.asarray(p["tint"], dtype=np.float32).reshape(1, 1, 3)
        if not np.allclose(tint, 1.0):
            smear *= tint

        blend = p["blend"]
        if blend == BLEND_ADD:
            out_rgb = rgb + smear
        elif blend == BLEND_SCREEN:
            out_rgb = rgb + smear - rgb * np.clip(smear, 0.0, 1.0)
        elif blend == BLEND_LIGHTEN:
            out_rgb = np.maximum(rgb, smear)
        else:
            out_rgb = rgb - np.float32(share) * moving + smear

        if alpha_on:
            a = src[..., 3]
            a_mov = a * ratio if ratio is not None else a
            a_smear = np.clip(convolve(a_mov, spectra[scales[1]]), 0.0, None) * np.float32(gain)
            if blend == BLEND_EXPOSURE:
                out_alpha = a - np.float32(share) * a_mov + a_smear
            else:
                out_alpha = a + a_smear - a * np.clip(a_smear, 0.0, 1.0)
            out_alpha = np.clip(out_alpha, 0.0, 1.0)

    result = np.empty_like(src)
    result[..., :3] = from_linear(out_rgb, p["colorspace"])
    if has_alpha:
        result[..., 3] = out_alpha
    if C > 4:
        result[..., 4:] = src[..., 4:]

    roll_px = p["roll"] * H
    bar_px = max(p["roll_bar"], 0.0) * H
    if abs(roll_px) > 1e-3 or bar_px >= 0.5:
        result = _roll_frame(result, roll_px, bar_px, p["roll_bar_soft"], has_alpha)
    if abs(p["weave_x"]) > 1e-4 or abs(p["weave_y"]) > 1e-4:
        result = _shift_linear(result, p["weave_y"], p["weave_x"])

    opacity = min(max(p["opacity"], 0.0), 1.0)
    if opacity < 1.0:
        result = src + (result - src) * np.float32(opacity)
    return result.astype(np.float32)
