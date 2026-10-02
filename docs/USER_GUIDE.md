# Timeyum user guide

Timeyum is an effect for After Effects that reproduces the look of a film camera whose shutter is out of phase with the pull-down claw, the fault that an ARRI Timing Shift Box creates on purpose. Bright areas leave streaks along the direction the film travels, a sharp image remains on top, and whatever leaves the frame comes back from the opposite edge because it lands on the neighbouring frame of the film.

Version 1.0.0. Windows 10 or 11, 64 bit. After Effects 2026. CPU only.

## Getting started

Install the plug-in as described in INSTALL.txt, restart After Effects and find it under Effects & Presets, Timeyum, Timeyum. Drag it onto a layer, or onto an adjustment layer to treat everything below it.

With the defaults you get a vertical streak that rises from every bright area of the image and re-enters from the bottom. Two controls change the character most: Length, how far the film travels, and Streak Model, which decides how the light is spread along the streak. Set Streak Model to Camera (Timing Shift) and move Timing Shift from 0 to about 100 degrees: the sharp image fades while the streak grows, which is how the real fault behaves.

Streaks are made from light, so the effect is most visible on footage with strong highlights against darker areas, and it responds best to footage that still has highlight detail. If you work in 8 or 16 bits per channel with a non-linear project, set Input Space to sRGB. If the project works in linear light (linear blending, or 32 bits per channel with a linear working space), leave it on Linear.

Angles follow one convention everywhere: 90 degrees points up, 0 points right, 180 left and -90 down.

## Main controls

**Opacity** blends the whole effect with the original image.

**Streak Model** chooses how the exposure is distributed along the streak. Fade tapers the streak toward its tip, Exponential makes it decay exponentially, and Camera (Timing Shift) uses the physical model described under Camera.

## Streak

**Length** is the length of the streak as a percentage of the frame height. In the Camera model 100% means the film travels exactly one frame.

**Angle** is the direction of the streak.

**Symmetric** extends the streak in both directions. **Back Length** adds a second streak in the opposite direction, as a percentage of the main one; it is off in the Camera model and when Symmetric is on.

**Start Offset** moves the beginning of the streak away from the object that produced it.

**Edge Behavior** decides what happens at the border of the frame. Wrap-Around brings the streak back from the opposite edge, as film does. Extend repeats the edge pixels, Mirror reflects the image and Black treats everything outside the frame as empty.

**Frame Line Gap** adds empty space between two frames, as a percentage of the frame height, so that a streak crosses the black line between frames before it re-enters. It acts with Wrap-Around and in the Camera model. About 2% looks like the gap between 4-perf frames.

## Profile

These apply to the Fade and Exponential models.

**Falloff** is how much the streak fades toward its tip (Fade). **Falloff Curve** shapes that fade: above 1 the streak stays bright for longer and drops near its tip, below 1 it fades quickly at first and leaves a long dim tail. **Decay** is the exponential rate of the Exponential model; higher values give shorter, tighter streaks.

## Camera

These apply to the Camera (Timing Shift) model. The film cycle is 360 degrees: the film rests, then the claw pulls it one frame down, then it rests again. The shutter is open for part of the cycle.

**Timing Shift** is where in the cycle the shutter opens. At 0 degrees the shutter closes before the film moves and nothing happens. Between roughly 60 and 120 degrees you get the classic look: a sharp image plus a streak. Around 180 degrees with a 180 degree shutter all the exposure takes place while the film moves and the sharp image disappears.

**Shutter Angle** is how long the shutter stays open. **Pull-down Angle** is how much of the cycle the claw needs to move the film. **Claw Ease** is how smoothly the claw accelerates and brakes: 100% gives a soft movement, 0% a movement at constant speed.

## Exposure

**Smear Amount** is the share of the exposure that leaves the sharp image and goes into the streak (not used in the Camera model, where the cycle decides it). **Smear Gain** multiplies the streak.

**Blend** chooses how the streak is combined with the image. Exposure moves the light: what goes into the streak is taken from the sharp image, so the average brightness of the frame stays the same. Add, Screen and Lighten add the streak on top without taking anything away.

**Threshold** limits the streak to the highlights; values are in linear light, where 1.0 is white. **Knee** softens the threshold. **Cleanup (px)** softens the streak sideways, across its direction, to calm hard stripes.

## Ghost

**Enable** adds echoes of the image along the streak direction. **Count** is the number of echoes, **Offset** the distance between them as a percentage of the frame height, **Strength** the brightness of the first echo, **Decay** how much weaker each following echo is, and **Length** how much each echo is smeared.

## Frame Roll

**Roll** moves the whole frame vertically with wrap-around, like a projector out of frame. **Frame Line** shows the black bar between frames at the seam, and **Frame Line Softness** feathers its edges.

## Color

**Smear Tint** colours only the streak. **Smear Saturation** changes its saturation. **Chromatic Spread** makes the red part of the streak longer and the blue part shorter (positive values) or the other way round (negative). **Breakup** varies the intensity between neighbouring streaks and **Breakup Scale (px)** sets how thin those stripes are.

## Shake

Shake adds an irregular, repeatable jitter, like a camera that does not run steadily. It is deterministic: the same frame always renders identically, in any order, with any number of render threads.

**Enable** turns it on. **Amount** scales all jitter. **Frequency (Hz)** is how many new random values appear per second; set it equal to the frame rate to get a new value on every frame. **Smoothness** goes from sudden jumps (0%) to smooth drifting (100%). **Seed** gives a different sequence.

The Jitter controls set how much each property varies: **Length Jitter**, **Smear Jitter**, **Angle Jitter** (degrees), **Timing Jitter** (degrees), **Ghost Jitter** and **Roll Jitter**. **Weave X** and **Weave Y** shake the whole frame by a few pixels, like the gate of a camera.

## Output

**Input Space** tells Timeyum how the pixel values are encoded: Linear, sRGB, Gamma 2.4, ARRI LogC3 or Sony S-Log3. The effect converts to linear light, works there and converts back. **Smear Alpha** extends the streak to the alpha channel, so it stays visible when the layer is composited over another background.

## Warp

Warp stops the streaks from being uniform. Their length and position change from place to place, smoothly and continuously in time, driven by an animated flow, by the video itself and by points you control. Turn it on with Enable.

**Amount** scales everything in Warp. **View** replaces the result with a picture of the field so you can tune it: Length Map, Reaction (what the video is asking for) and Displacement (the direction and size of the movement).

### Flow

A field that evolves on its own. **Flow Length** varies the length of the streaks, **Flow Wave** makes them undulate like a fluid, as a percentage of the frame height. **Flow Scale** is the size of the features of the flow, **Flow Speed** how fast it evolves, **Flow Detail** how many layers of finer detail it has, and **Flow Seed** picks a different pattern. **Drift Angle** and **Drift Speed** make the whole pattern travel across the frame.

If Flow Wave is large compared to Flow Scale, Timeyum flattens the wave just enough to avoid folding the image over itself. For more undulation, increase Flow Scale as well.

### Reaction to the video

**Brightness Response** makes the field depend on how bright the footage is. **Motion Response** makes it depend on how much changes between frames, and **Motion Sensitivity** sets how little change is needed. **Reaction Softness** blurs the reaction, as a percentage of the frame height. **Inertia** gives the reaction a memory, so it rises and falls slowly instead of flickering, and **History Frames** is how many past frames it may look at. With History Frames at 0 no past frame is read.

**Length Reaction** lengthens the streaks where the video reacts. **Wave Reaction** restricts the flow wave to the areas that react.

The first frame of a clip has no past, so motion appears from the second frame onward.

### Pull

**Pull Point** is a point you can drag in the Composition panel, or link to a null with an expression. Around it the image bulges (positive **Pull Strength**) or pinches (negative) and the streaks grow longer (**Pull Length**) within **Pull Radius**, a percentage of the frame height. **Auto Target Strength** does the same toward the brightest area of the video, which Timeyum follows by itself; negative values push away from it.

**Base Follow** sets how much of the displacement also moves the sharp image. At 0 only the streaks move.

**Length Steps** sets how many streak lengths are blended per pixel. Fewer steps render faster, more steps give smoother transitions.

## Tips

Put Timeyum on an adjustment layer above a shot to treat the whole composition. For a subtle fault, use the Camera model with a low Timing Shift and a moderate Length. For a heavier look, add Ghost and a little Shake. To make the effect travel with the music or the action, animate Timing Shift or Length with keyframes or expressions.

Use half or third resolution while you work: the effect scales its pixel values to the preview resolution, so the preview looks like the final render.

## Performance

Timeyum runs on the CPU and uses every core. A frame takes a fraction of a second to a few seconds depending on resolution, on the edge mode (Wrap-Around is the fastest) and on whether Warp is on. Warp costs roughly one and a half times the plain effect, and Length Steps is its main lever. Long streaks with Extend, Mirror or Black need a larger working area and take longer than with Wrap-Around.

## Troubleshooting

The effect is not in the menu. If you downloaded the zip, right-click it, choose Properties and tick Unblock before extracting. Check that Timeyum.aex is in the MediaCore folder described in INSTALL.txt, that After Effects was closed during the copy, and that you are using a 64 bit Windows version of After Effects 2026.

The picture at the top of the panel is missing. The plug-in writes the reason to timeyum_log.txt in your temporary folder (type %TEMP% in the Explorer address bar). The effect works without it.

Nothing happens at Timing Shift 0 degrees. That is correct: the shutter closes before the film moves.

The streak goes down instead of up. Set Angle to -90 degrees.

The result looks too bright or too dark compared with a plain render. Check Input Space. Footage in a non-linear project should use sRGB.

## Support

Include the version number (shown in the About box of the effect), your After Effects version and a screenshot of the Timeyum panel when you write to support.
