# Changelog

## 1.0.0

First release.

- Timing shift streaks with wrap-around, three streak models (Fade, Exponential, Camera) and a physical camera model with timing shift, shutter angle, pull-down angle and claw ease.
- Edge behaviour (wrap-around, extend, mirror, black), frame line gap, symmetric and back streaks, start offset.
- Exposure controls: smear amount and gain, four blend modes, highlight threshold with soft knee, cross-streak cleanup.
- Ghost echoes, frame roll with a frame line, smear tint, saturation, chromatic spread and breakup.
- Deterministic shake with jitter on length, angle, smear, timing, ghosts and roll, plus gate weave.
- Warp: flow field, reaction to brightness and motion with memory, interactive pull point, automatic bright-area target, control views.
- Control input: a matte, mask, alpha channel, depth map or Z-depth pass, from another layer or from this layer, decides where the effect shows, where the streaks come from, how long they are and what drives the Warp. Levels, inversion, softness and a view of the control.
- A static panel: no control is rewritten or greyed out when another one changes.
- 8, 16 and 32 bits per channel, multi-frame rendering, CPU only.
- Command line tool for image sequences.
