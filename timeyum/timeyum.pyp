"""Timeyum Timeshift: ARRI Timing Shift Box / out-of-sync camera look for Cinema 4D.

Registers a render effect (VideoPost). Add it from Render Settings > Effect...
The image processing lives in timeyum_core.py and needs NumPy.
"""

import math
import os
import sys
import time
import traceback

import c4d

# Test ID from Maxon's reserved range (1000001-1000010). Get a free permanent ID
# at https://developers.maxon.net/forum/pid before sharing the plugin.
PLUGIN_ID = 1000007
PLUGIN_NAME = "Timeyum Timeshift"

_HERE = os.path.dirname(os.path.abspath(__file__))
for _path in (_HERE, os.path.join(_HERE, "lib")):
    if os.path.isdir(_path) and _path not in sys.path:
        sys.path.insert(0, _path)

import timeyum_params as tp  # noqa: E402

try:
    import numpy as np  # noqa: E402
    import timeyum_core as core  # noqa: E402
    _IMPORT_ERROR = None
except Exception as exc:  # NumPy missing or incompatible
    np = None
    core = None
    _IMPORT_ERROR = exc

_CAMERA_IDS = (tp.TY_TIMING_SHIFT, tp.TY_SHUTTER_ANGLE, tp.TY_PULLDOWN_ANGLE, tp.TY_CLAW_EASE)
_GHOST_IDS = (tp.TY_GHOST_COUNT, tp.TY_GHOST_OFFSET, tp.TY_GHOST_STRENGTH, tp.TY_GHOST_DECAY,
              tp.TY_GHOST_LENGTH)
_SHAKE_IDS = (tp.TY_SHAKE_AMOUNT, tp.TY_SHAKE_FREQ, tp.TY_SHAKE_SMOOTH, tp.TY_SHAKE_SEED,
              tp.TY_SHAKE_LENGTH, tp.TY_SHAKE_ANGLE, tp.TY_SHAKE_SMEAR, tp.TY_SHAKE_TIMING,
              tp.TY_SHAKE_ROLL, tp.TY_SHAKE_WEAVE_X, tp.TY_SHAKE_WEAVE_Y, tp.TY_SHAKE_GHOST)
_ARTISTIC_IDS = (tp.TY_SYMMETRIC, tp.TY_START_OFFSET, tp.TY_SMEAR)


def _log(msg):
    print("[Timeyum] " + msg)


def _vps_get(vps, key, default=None):
    """VideoPostStruct access that works whether Cinema 4D passes a dict or an object."""
    try:
        return vps[key]
    except Exception:
        return getattr(vps, key, default)


def _make_curve():
    sd = c4d.SplineData()
    try:
        sd.DeleteAllPoints()
        for x, y in tp.DEFAULT_CURVE_KNOTS:
            sd.InsertKnot(x, y)
    except Exception:
        sd.MakeLinearSplineLinear(2)
    return sd


def _sample_curve(sd, count=256):
    if sd is None:
        return None
    try:
        return [max(0.0, sd.GetPoint(i / (count - 1.0)).y) for i in range(count)]
    except Exception:
        return None


def read_params(bc):
    """BaseContainer -> core parameter dict (degrees, fractions, plain Python values)."""
    p = {}
    for pid, key, kind in tp.PARAMS:
        if kind == "float":
            p[key] = bc.GetFloat(pid)
        elif kind == "deg":
            p[key] = math.degrees(bc.GetFloat(pid))
        elif kind == "int":
            p[key] = bc.GetInt32(pid)
        elif kind == "bool":
            p[key] = bc.GetBool(pid)
        elif kind == "color":
            v = bc.GetVector(pid)
            p[key] = (v.x, v.y, v.z)
        elif kind == "spline":
            p[key] = _sample_curve(bc.GetData(pid))
    return p


def write_defaults(bc):
    for pid, key, kind in tp.PARAMS:
        value = tp.DEFAULTS[key]
        if kind == "float":
            bc.SetFloat(pid, float(value))
        elif kind == "deg":
            bc.SetFloat(pid, math.radians(value))
        elif kind == "int":
            bc.SetInt32(pid, int(value))
        elif kind == "bool":
            bc.SetBool(pid, bool(value))
        elif kind == "color":
            bc.SetVector(pid, c4d.Vector(*value))
        elif kind == "spline":
            bc.SetData(pid, _make_curve())


class _LineIO(object):
    """Moves float32 scanlines between a VPBuffer and NumPy.

    Recent Cinema 4D versions accept any writable Python buffer for
    GetLine/SetLine; older ones want a c4d.storage.ByteSeq. The working
    variant is detected on the first line.
    """

    def __init__(self, buf, width, cpp):
        self.buf = buf
        self.width = width
        self.cpp = cpp
        self.nbytes = width * cpp * 4
        self.line = bytearray(self.nbytes)
        self.view = np.frombuffer(self.line, dtype=np.float32)
        self.use_seq = False
        self.seq = None

    def _seq_bytes(self):
        for conv in (lambda s: memoryview(s).tobytes(), bytes, lambda s: s[0:self.nbytes]):
            try:
                data = conv(self.seq)
                if len(data) >= self.nbytes:
                    return data[:self.nbytes]
            except Exception:
                pass
        raise RuntimeError("cannot read c4d.storage.ByteSeq")

    def read(self, y):
        if not self.use_seq:
            try:
                if self.buf.GetLine(0, y, self.width, self.line, 32, False) is not False:
                    return self.view.reshape(self.width, self.cpp)
            except Exception:
                pass
            self.use_seq = True
            self.seq = c4d.storage.ByteSeq(None, self.nbytes)
        if self.buf.GetLine(0, y, self.width, self.seq, 32, False) is False:
            raise RuntimeError("VPBuffer.GetLine failed on line %d" % y)
        return np.frombuffer(self._seq_bytes(), dtype=np.float32).reshape(self.width, self.cpp)

    def write(self, y, row):
        data = np.ascontiguousarray(row, dtype=np.float32).reshape(-1)
        if not self.use_seq:
            self.view[:] = data
            if self.buf.SetLine(0, y, self.width, self.line, 32, False) is False:
                raise RuntimeError("VPBuffer.SetLine failed on line %d" % y)
            return
        raw = data.tobytes()
        try:
            seq = c4d.storage.ByteSeq(raw, self.nbytes)
        except Exception:
            seq = self.seq
            memoryview(seq)[:self.nbytes] = raw
        if self.buf.SetLine(0, y, self.width, seq, 32, False) is False:
            raise RuntimeError("VPBuffer.SetLine failed on line %d" % y)


def _buffer_info(buf):
    def info(const_name, method):
        const = getattr(c4d, const_name, None)
        if const is not None:
            try:
                return int(buf.GetInfo(const))
            except Exception:
                pass
        return int(getattr(buf, method)())
    return info("VPGETINFO_XRES", "GetBw"), info("VPGETINFO_YRES", "GetBh"), info("VPGETINFO_CPP", "GetCpp")


class TimeyumVideoPost(c4d.plugins.VideoPostData):

    _announced = False

    def Init(self, node, isCloneInit=False):
        bc = node.GetDataInstance() if node else None
        if bc is None:
            return False
        if isCloneInit:
            return True
        write_defaults(bc)
        return True

    def GetRenderInfo(self, node):
        return 0

    def RenderEngineCheck(self, node, id):
        return True

    def GetDEnabling(self, node, id, t_data, flags, itemdesc):
        pid = id[0].id
        bc = node.GetDataInstance()
        if bc is None:
            return True
        profile = bc.GetInt32(tp.TY_PROFILE)
        camera = profile == tp.PROFILE_CAMERA
        if pid in _ARTISTIC_IDS:
            return not camera
        if pid == tp.TY_BACK_LENGTH:
            return not camera and not bc.GetBool(tp.TY_SYMMETRIC)
        if pid in (tp.TY_FALLOFF, tp.TY_FALLOFF_CURVE):
            return profile == tp.PROFILE_FADE
        if pid == tp.TY_DECAY:
            return profile == tp.PROFILE_EXPONENTIAL
        if pid == tp.TY_CURVE:
            return profile == tp.PROFILE_SPLINE
        if pid in _CAMERA_IDS:
            return camera
        if pid == tp.TY_FRAME_GAP:
            return camera or bc.GetInt32(tp.TY_EDGE) == tp.EDGE_WRAP
        if pid == tp.TY_KNEE:
            return bc.GetFloat(tp.TY_THRESHOLD) > 0.0
        if pid in _GHOST_IDS:
            return bc.GetBool(tp.TY_GHOST)
        if pid in _SHAKE_IDS:
            return bc.GetBool(tp.TY_SHAKE)
        return True

    def Execute(self, node, vps):
        try:
            return self._execute(node, vps)
        except Exception:
            _log("frame left unprocessed:\n" + traceback.format_exc())
            return c4d.RENDERRESULT_OK

    def _execute(self, node, vps):
        call = _vps_get(vps, "vp")
        is_open = _vps_get(vps, "open")
        bc = node.GetDataInstance()

        if call == c4d.VIDEOPOSTCALL_FRAMESEQUENCE and is_open:
            self._announced = False
            return c4d.RENDERRESULT_OK

        stage = c4d.VIDEOPOSTCALL_FRAME if bc.GetInt32(tp.TY_STAGE) == tp.STAGE_FRAME else c4d.VIDEOPOSTCALL_RENDER
        if call != stage or is_open:
            return c4d.RENDERRESULT_OK

        if core is None:
            if not self._announced:
                _log("NumPy not found, effect skipped (%s). See README: installing NumPy." % _IMPORT_ERROR)
                self._announced = True
            return c4d.RENDERRESULT_OK

        render = _vps_get(vps, "render")
        if render is None:
            if not self._announced:
                _log("no render buffer at this stage; set Output > Apply At to Render End.")
                self._announced = True
            return c4d.RENDERRESULT_OK
        thread = _vps_get(vps, "thread")
        if thread is not None and thread.TestBreak():
            return c4d.RENDERRESULT_OK
        buf = render.GetBuffer(c4d.VPBUFFER_RGBA, c4d.NOTOK)
        if buf is None:
            return c4d.RENDERRESULT_OK

        doc = _vps_get(vps, "doc") or node.GetDocument()
        fps = doc.GetFps() if doc else 25
        t = _vps_get(vps, "time")
        if not isinstance(t, c4d.BaseTime):
            t = doc.GetTime() if doc else c4d.BaseTime(0)
        frame = t.Get() * fps

        started = time.time()
        width, height, cpp = _buffer_info(buf)
        if width <= 0 or height <= 0 or cpp < 3:
            return c4d.RENDERRESULT_OK
        io = _LineIO(buf, width, cpp)
        img = np.empty((height, width, cpp), dtype=np.float32)
        for y in range(height):
            img[y] = io.read(y)

        out = core.process(img, read_params(bc), frame=frame, fps=fps)

        if thread is not None and thread.TestBreak():
            return c4d.RENDERRESULT_OK
        for y in range(height):
            io.write(y, out[y])

        if not self._announced:
            _log("%dx%d, %d channels, %.2fs per frame" % (width, height, cpp, time.time() - started))
            self._announced = True
        return c4d.RENDERRESULT_OK


if __name__ == "__main__":
    name = PLUGIN_NAME if core is not None else PLUGIN_NAME + " (NumPy missing)"
    if _IMPORT_ERROR is not None:
        _log("NumPy could not be imported: %s" % _IMPORT_ERROR)
    info = getattr(c4d, "PLUGINFLAG_VIDEOPOST_MULTIPLE", 0)
    if not c4d.plugins.RegisterVideoPostPlugin(PLUGIN_ID, name, info, TimeyumVideoPost, "VPtimeyum", 0, 0):
        _log("registration failed")
