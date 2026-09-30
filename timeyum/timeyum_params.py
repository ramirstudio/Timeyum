"""Parameter IDs, enums and defaults shared by the VideoPost, the core and the CLI.

No Cinema 4D or NumPy imports here: the plugin must be able to build its UI
even when NumPy is missing. IDs mirror res/description/vptimeyum.h.
"""

PROFILE_FADE = 0
PROFILE_EXPONENTIAL = 1
PROFILE_CAMERA = 2
PROFILE_SPLINE = 3

EDGE_WRAP = 0
EDGE_EXTEND = 1
EDGE_MIRROR = 2
EDGE_BLACK = 3

BLEND_EXPOSURE = 0
BLEND_ADD = 1
BLEND_SCREEN = 2
BLEND_LIGHTEN = 3

CS_LINEAR = 0
CS_SRGB = 1
CS_GAMMA24 = 2
CS_LOGC3 = 3
CS_SLOG3 = 4

STAGE_RENDER = 0
STAGE_FRAME = 1

TY_OPACITY = 20100
TY_BLEND = 20101
TY_COLORSPACE = 20102
TY_AFFECT_ALPHA = 20103
TY_STAGE = 20104
TY_LENGTH = 20200
TY_ANGLE = 20201
TY_SYMMETRIC = 20202
TY_BACK_LENGTH = 20203
TY_START_OFFSET = 20204
TY_EDGE = 20205
TY_FRAME_GAP = 20206
TY_PROFILE = 20300
TY_FALLOFF = 20301
TY_FALLOFF_CURVE = 20302
TY_DECAY = 20303
TY_CURVE = 20304
TY_TIMING_SHIFT = 20400
TY_SHUTTER_ANGLE = 20401
TY_PULLDOWN_ANGLE = 20402
TY_CLAW_EASE = 20403
TY_SMEAR = 20500
TY_GAIN = 20501
TY_THRESHOLD = 20502
TY_KNEE = 20503
TY_CLEANUP = 20504
TY_GHOST = 20600
TY_GHOST_COUNT = 20601
TY_GHOST_OFFSET = 20602
TY_GHOST_STRENGTH = 20603
TY_GHOST_DECAY = 20604
TY_GHOST_LENGTH = 20605
TY_ROLL = 20700
TY_ROLL_BAR = 20701
TY_ROLL_BAR_SOFT = 20702
TY_TINT = 20800
TY_SATURATION = 20801
TY_CHROMA = 20802
TY_BREAKUP = 20803
TY_BREAKUP_SCALE = 20804
TY_SHAKE = 20900
TY_SHAKE_AMOUNT = 20901
TY_SHAKE_FREQ = 20902
TY_SHAKE_SMOOTH = 20903
TY_SHAKE_SEED = 20904
TY_SHAKE_LENGTH = 20905
TY_SHAKE_ANGLE = 20906
TY_SHAKE_SMEAR = 20907
TY_SHAKE_TIMING = 20908
TY_SHAKE_ROLL = 20909
TY_SHAKE_WEAVE_X = 20910
TY_SHAKE_WEAVE_Y = 20911
TY_SHAKE_GHOST = 20912

# Percent parameters are fractions, angles are degrees (the VideoPost stores
# DEGREE parameters in radians and converts on read/write).
DEFAULTS = {
    "opacity": 1.0,
    "blend": BLEND_EXPOSURE,
    "colorspace": CS_LINEAR,
    "affect_alpha": True,
    "stage": STAGE_RENDER,
    "profile": PROFILE_FADE,
    "length": 0.6,
    "angle": 90.0,
    "symmetric": False,
    "back_length": 0.0,
    "start_offset": 0.0,
    "edge": EDGE_WRAP,
    "frame_gap": 0.0,
    "falloff": 0.55,
    "falloff_curve": 1.0,
    "decay": 4.0,
    "curve": None,
    "timing_shift": 90.0,
    "shutter_angle": 180.0,
    "pulldown_angle": 180.0,
    "claw_ease": 1.0,
    "smear": 0.35,
    "gain": 1.0,
    "threshold": 0.0,
    "knee": 0.5,
    "cleanup": 0.0,
    "ghost": False,
    "ghost_count": 1,
    "ghost_offset": 0.12,
    "ghost_strength": 0.25,
    "ghost_decay": 0.5,
    "ghost_length": 0.01,
    "roll": 0.0,
    "roll_bar": 0.0,
    "roll_bar_soft": 0.3,
    "tint": (1.0, 1.0, 1.0),
    "saturation": 1.0,
    "chroma": 0.0,
    "breakup": 0.0,
    "breakup_scale": 6.0,
    "shake": False,
    "shake_amount": 1.0,
    "shake_freq": 12.0,
    "shake_smooth": 0.3,
    "shake_seed": 1234,
    "shake_length": 0.35,
    "shake_angle": 0.0,
    "shake_smear": 0.25,
    "shake_timing": 20.0,
    "shake_roll": 0.0,
    "shake_weave_x": 0.0,
    "shake_weave_y": 1.5,
    "shake_ghost": 0.0,
    "weave_x": 0.0,
    "weave_y": 0.0,
}

# (description id, core key, storage kind)
PARAMS = (
    (TY_OPACITY, "opacity", "float"),
    (TY_BLEND, "blend", "int"),
    (TY_COLORSPACE, "colorspace", "int"),
    (TY_AFFECT_ALPHA, "affect_alpha", "bool"),
    (TY_STAGE, "stage", "int"),
    (TY_PROFILE, "profile", "int"),
    (TY_LENGTH, "length", "float"),
    (TY_ANGLE, "angle", "deg"),
    (TY_SYMMETRIC, "symmetric", "bool"),
    (TY_BACK_LENGTH, "back_length", "float"),
    (TY_START_OFFSET, "start_offset", "float"),
    (TY_EDGE, "edge", "int"),
    (TY_FRAME_GAP, "frame_gap", "float"),
    (TY_FALLOFF, "falloff", "float"),
    (TY_FALLOFF_CURVE, "falloff_curve", "float"),
    (TY_DECAY, "decay", "float"),
    (TY_CURVE, "curve", "spline"),
    (TY_TIMING_SHIFT, "timing_shift", "deg"),
    (TY_SHUTTER_ANGLE, "shutter_angle", "deg"),
    (TY_PULLDOWN_ANGLE, "pulldown_angle", "deg"),
    (TY_CLAW_EASE, "claw_ease", "float"),
    (TY_SMEAR, "smear", "float"),
    (TY_GAIN, "gain", "float"),
    (TY_THRESHOLD, "threshold", "float"),
    (TY_KNEE, "knee", "float"),
    (TY_CLEANUP, "cleanup", "float"),
    (TY_GHOST, "ghost", "bool"),
    (TY_GHOST_COUNT, "ghost_count", "int"),
    (TY_GHOST_OFFSET, "ghost_offset", "float"),
    (TY_GHOST_STRENGTH, "ghost_strength", "float"),
    (TY_GHOST_DECAY, "ghost_decay", "float"),
    (TY_GHOST_LENGTH, "ghost_length", "float"),
    (TY_ROLL, "roll", "float"),
    (TY_ROLL_BAR, "roll_bar", "float"),
    (TY_ROLL_BAR_SOFT, "roll_bar_soft", "float"),
    (TY_TINT, "tint", "color"),
    (TY_SATURATION, "saturation", "float"),
    (TY_CHROMA, "chroma", "float"),
    (TY_BREAKUP, "breakup", "float"),
    (TY_BREAKUP_SCALE, "breakup_scale", "float"),
    (TY_SHAKE, "shake", "bool"),
    (TY_SHAKE_AMOUNT, "shake_amount", "float"),
    (TY_SHAKE_FREQ, "shake_freq", "float"),
    (TY_SHAKE_SMOOTH, "shake_smooth", "float"),
    (TY_SHAKE_SEED, "shake_seed", "int"),
    (TY_SHAKE_LENGTH, "shake_length", "float"),
    (TY_SHAKE_ANGLE, "shake_angle", "deg"),
    (TY_SHAKE_SMEAR, "shake_smear", "float"),
    (TY_SHAKE_TIMING, "shake_timing", "deg"),
    (TY_SHAKE_ROLL, "shake_roll", "float"),
    (TY_SHAKE_WEAVE_X, "shake_weave_x", "float"),
    (TY_SHAKE_WEAVE_Y, "shake_weave_y", "float"),
    (TY_SHAKE_GHOST, "shake_ghost", "float"),
)

# Default points of the custom curve (position along the streak, density).
DEFAULT_CURVE_KNOTS = ((0.0, 1.0), (0.3, 0.75), (1.0, 0.0))
