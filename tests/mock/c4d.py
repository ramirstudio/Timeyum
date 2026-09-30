"""Minimal fake of the c4d API surface used by timeyum.pyp (for smoke tests only)."""
import types, numpy as np
VIDEOPOSTCALL_FRAMESEQUENCE, VIDEOPOSTCALL_FRAME, VIDEOPOSTCALL_SUBFRAME, VIDEOPOSTCALL_RENDER, VIDEOPOSTCALL_INNER = 0, 1, 2, 3, 4
RENDERRESULT_OK = 0; VPBUFFER_RGBA = 100; NOTOK = -1
VPGETINFO_XRES, VPGETINFO_YRES, VPGETINFO_CPP = 0, 1, 3
PLUGINFLAG_VIDEOPOST_MULTIPLE = 1 << 5
class Vector:
    def __init__(self, x=0., y=0., z=0.): self.x, self.y, self.z = float(x), float(y), float(z)
class BaseTime:
    def __init__(self, s=0.): self.s = s
    def Get(self): return self.s
class SplineData:
    def __init__(self): self.k = []
    def DeleteAllPoints(self): self.k = []
    def InsertKnot(self, x, y, flags=0): self.k.append((x, y)); self.k.sort(); return len(self.k)-1
    def MakeLinearSplineLinear(self, n): self.k = [(0, 0), (1, 1)]
    def GetPoint(self, r):
        xs, ys = zip(*self.k); return Vector(r, float(np.interp(r, xs, ys)), 0)
class BaseContainer(dict):
    def SetFloat(self, i, v): assert isinstance(v, float); self[i] = v
    def SetInt32(self, i, v): assert isinstance(v, int); self[i] = v
    def SetBool(self, i, v): self[i] = bool(v)
    def SetVector(self, i, v): assert isinstance(v, Vector); self[i] = v
    def SetData(self, i, v): self[i] = v
    def GetFloat(self, i): return float(self.get(i, 0.0))
    def GetInt32(self, i): return int(self.get(i, 0))
    def GetBool(self, i): return bool(self.get(i, False))
    def GetVector(self, i): return self.get(i, Vector())
    def GetData(self, i): return self.get(i)
class _Node:
    def __init__(self): self.bc = BaseContainer()
    def GetDataInstance(self): return self.bc
    def GetDocument(self): return None
class ByteSeq:
    def __init__(self, mem, size): self.b = bytearray(mem if mem is not None else size)
    def __bytes__(self): return bytes(self.b)
class VPBuffer:
    """Stores RGBA float32; accept_bytearray toggles which buffer types GetLine takes."""
    def __init__(self, img, accept_bytearray=True): self.img = img.copy(); self.accept = accept_bytearray
    def GetInfo(self, t): return {0: self.img.shape[1], 1: self.img.shape[0], 3: self.img.shape[2]}[t]
    def _check(self, buf):
        if isinstance(buf, bytearray) and not self.accept: raise TypeError("expected ByteSeq")
    def GetLine(self, x, y, cnt, buf, bitdepth, dither):
        self._check(buf); data = self.img[y, x:x+cnt].astype(np.float32).tobytes()
        (buf if isinstance(buf, bytearray) else buf.b)[:len(data)] = data; return True
    def SetLine(self, x, y, cnt, buf, bitdepth, dither):
        self._check(buf); raw = bytes(buf if isinstance(buf, bytearray) else buf.b)
        self.img[y, x:x+cnt] = np.frombuffer(raw, np.float32).reshape(cnt, -1); return True
class _Render:
    def __init__(self, vpb): self.vpb = vpb
    def GetBuffer(self, t, i): assert t == VPBUFFER_RGBA; return self.vpb
class _Doc:
    def GetFps(self): return 24
    def GetTime(self): return BaseTime(0.5)
class _Thread:
    def TestBreak(self): return False
registered = {}
class _VPD: pass
def _reg(pid, name, info, g, desc, disk, prio): registered.update(pid=pid, name=name, g=g, desc=desc); return True
plugins = types.SimpleNamespace(VideoPostData=_VPD, RegisterVideoPostPlugin=_reg)
storage = types.SimpleNamespace(ByteSeq=ByteSeq)
class _DescLevel:
    def __init__(self, i): self.id = i
class DescID(list):
    def __init__(self, i): super().__init__([_DescLevel(i)])
