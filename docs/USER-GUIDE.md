# Repousse user guide

Repousse is **the picture hammered into a metal sheet and lit by a lamp on a swinging cord, for
[Resolume](https://resolume.com) Arena and Avenue**, as an FFGL effect. It does not paint an
emboss filter over a clip. It reads the clip's brightness as height, works that height with a
punch of a real radius, and lights the result with a point lamp at a real position: inverse-square
falloff, a microfacet highlight in the measured reflectance of copper, brass, silver, gold or
steel, self-shadowing, and a patina in the recesses. The lamp is a pendulum, and an onset in the
audio or the Kick button gives it a push. The raking shadows, the sliding highlights, the soft
chased work under a big punch and the crisp engraving under a fine one are all what the model
does, not what somebody drew.

![A synthetic relief card rendered as chased copper: a dome, a ring, a raised block with a shadow off its lower edge, a comb of fine lines the punch has flattened, and a rough patch, under a lamp hung up and to the left whose pool of light sits on the sheet](hero.png)

*The offline harness's relief card through the plugin at its defaults, rendered by the harness
rather than captured from Resolume: copper, Depth 16 px, a 4 px punch, the lamp at (0.3, 0.75)
half a frame up, a quarter of a second after a kick. The comb of fine lines at the top left is
narrower than the punch, so it has been lowered to exactly the height the punch's opening
predicts; the block at the bottom right throws its shadow away from the lamp.*

> **Before you rely on this:** released at **v0.1.0**, and honestly early. The physics is measured
> rather than asserted, by a harness that drives the real plugin class and reads each claim back
> out of the picture it made, at two rasters: five planes of known slope under five lamps read
> the inverse-square Lambert law at every pixel and channel to 1.8e-7 against a tolerance derived
> from one float ulp; the highlight on a hemisphere lands on the exact pixel the half-vector point
> predicts; each metal reflects its computed F0 at normal incidence to 5e-8; the shadow behind a
> step is h / tan θ within one march step; a ridge narrower than the punch comes out exactly
> min(H, d²/2R) tall and a wider one keeps its height; the lamp's period, read from its position
> in the picture over six seconds, is 2π√(L/g) with the large-angle correction to 4 parts in 10⁵,
> and its decay matches the stated Q; and seventeen deliberate faults are shown to make those
> checks fail. All 20 controls are shown to change the picture. It has **never been loaded into
> Resolume on macOS** — the one host it has run in is the fleet's own test host, `oxbow`, for 120
> frames — and **no audio has ever reached it** outside the harness's synthetic spectra.
> On Windows, a build of this source loads, registers and renders in Resolume Arena 7.27.1, with every control matching what the plugin declares — on software rendering (win-lab, Mesa llvmpipe, no GPU), so that says nothing about a GPU. Cord Length, Damping and Swing could not be shown acting there, because the gate's picture is a still and it never presses Kick, and a pendulum nobody pushes hangs still; the harness's `--pendulum` check measures all three from the picture.
> Try it on a spare layer before you put it in a show.
>
> This codebase was created with AI assistance, directed and reviewed by a human author.

---

## Installing

Every download carries one effect, **SW Repousse**. Drop it into Resolume's effects folder and
restart Resolume:

```
macOS    ~/Documents/Resolume Arena/Extra Effects/
Windows  %USERPROFILE%\Documents\Resolume Arena\Extra Effects\
```

Avenue uses the same layout under its own folder name. The effect then appears in the effects
browser as **SW Repousse**.

The macOS download is a universal build (Apple silicon and Intel), as a `.dmg` or a `.zip`.
It is **Developer ID-signed and notarised**, so the bundle simply loads. The Windows download is an x64 installer or a `.zip`. It is not code-signed, so
the installer trips SmartScreen once: **More info** → **Run anyway**.

---

## Two facts make the look

Repoussé is sheet metal worked from behind with punches and from the front with chasing tools.
Two physical facts make the look, and both are in the model rather than in a filter:

| the fact | what comes out |
| --- | --- |
| **the punch sets the finest detail**: a sheet can be pushed only as sharply as the tool that pushes it | the clip's brightness becomes height, and that height is opened and closed by the punch's profile; detail narrower than the punch is **never formed**, not blurred away — a ridge narrower than the punch is lowered to exactly d²/2R, and a ridge wider than it keeps its full height |
| **metal is lit, not shaded**: a point lamp at a real position, a real metal's reflectance, and a surface that shadows itself | inverse-square falloff, so the pool of light under the lamp has a real edge; a microfacet highlight (GGX, height-correlated Smith, Schlick Fresnel) in the normal-incidence reflectance of copper, brass, silver, gold or steel; a small diffuse term for the patina; and a shadow behind every step, marched over the same height field |

The lamp hangs on a cord. It is a damped large-angle pendulum with the period its length gives it,
integrated on the CPU in double, and a push — an onset in the audio, or the Kick button — adds
angular velocity. That is the only audio coupling there is. The highlights slide across the relief
on the mirror points the microfacet model puts them at, and the pool of light under the lamp swings
with it.

The units are real. Depth and Punch Radius are in **pixels**, so the relief is a thing done to the
picture's pixels: the same clip at 4K is hammered finer, not deeper. Lamp Height and Cord Length
are in **frame heights**, and one frame height is one metre — that single scale is what ties the
pendulum's period in seconds to the distance the lamp is seen to swing.

---

## Start here

Put SW Repousse on a layer or a clip with **texture across the whole frame** — the bundled Beat
loops (circuit boards), the skulls, the cloud of blocks. Out of the box you get copper, Depth 16
px, a 4 px punch, the lamp up and to the left at (0.3, 0.75) half a frame above the sheet, and a
cord of half a metre, which is a period of 1.42 s.

Then:

1. **Press Kick.** The lamp swings, the pool of light moves with it, the highlights slide across
   the relief and the shadows swing round. With nothing routed to Audio, the lamp hangs still
   until you do.
2. **Punch Radius → 16 px.** Soft chased work: every edge rounds off and anything finer than the
   punch is never formed. **→ 1 px** and it is crisp engraving.
3. **Lamp Height → 0.12.** Raking light: every ridge throws a long shadow and the relief reads far
   deeper than its sixteen pixels. Then **Depth → 32 px** and the shadows lengthen again.
4. **Metal: Brass, Silver, Gold, Steel.** Each is its own reflectance, computed from the metal's
   published refractive index. Gold is the warmest, silver the brightest, steel the dullest.
5. **Roughness → 0.15.** The sheen tightens into a mirror highlight. **Patina → 1.** The recesses
   darken, because that is where the horizon is high; the polished high points still gleam.
6. **Colour: Clip.** A painted sheet: the clip is the paint over a dielectric base. **Cord Length →
   2 m** and the swing is slow (2.8 s); lower **Swing** so it fits the frame.
7. **Show Height.** The worked relief as grey, for seeing exactly what the punch has done.

**Dark clips are a flat sheet with a lamp on it.** The relief is the picture's brightness, so a
clip of thin lines on black is a copper sheet with a few scratches and the lamp's pool. That is
correct, and it is what a dark clip should look like; if you want relief, feed the plugin a clip
with brightness across the frame, or lift it with a brightness effect ahead of this one.

Every slider is declared to the host as 0 to 1. The value each position stands for is given with
each control below.

---

## The Sheet group

**Depth** — the height of a white pixel, **0 to 64 px**; **16 px by default** (0.25). Height is
Depth × the pixel's Rec. 709 luma, clamped to 0..1. It is in pixels of height, so it sets how
steep the relief's slopes are against the raster: at 4K the same clip is hammered finer, not
deeper.

**Invert** — off by default. Works the sheet from the other side: height is Depth × (1 − luma), so
dark areas stand proud and bright areas are recessed.

**Punch Radius** — the tip radius of the punch, **0 to 16 px**; **4 px by default** (0.25). The
height field is opened (eroded then dilated: the punch from behind) and then closed (dilated then
eroded: the chasing tool from the front) by the punch's profile, so nothing narrower than the
punch survives and nothing wider is touched. A ridge of width w has d = ⌈w/2⌉ pixels from its
middle to the nearest outside pixel, and comes out min(H, d²/2R) tall if d ≤ R, else its full
height H: a 4 px ridge under a 4 px punch keeps half a pixel of a 16 px height, a 9 px ridge keeps
all of it. Below 1 px the punch does nothing. The profile is the paraboloid osculating the sphere
at its tip, truncated at R, applied separately along x and y; see Known limits.

**Smooth** — a Gaussian on the height before the punch, **σ 0 to 3 px**; **σ 1 px by default**
(0.333). It is not detail control — the punch is — it is the answer to 8-bit footage: a one-code
step in a gentle gradient is a 0.06 px riser, and a lamp at 45° lights every riser as a terrace,
so a smooth dome comes out as concentric rings. One pixel of σ takes the terraces out and leaves
the punch to decide the detail. Set it to 0 for float or 10-bit sources.

**Metal** — **Copper**, **Brass**, **Silver**, **Gold** or **Steel**; Copper by default. Each is a
normal-incidence reflectance (F0) in linear sRGB, computed by the repository's `tools/f0.py` from
published complex refractive indices — Johnson & Christy 1972 for copper, silver and gold, Johnson
& Christy 1974 for iron (the plugin's steel), Querry 1985 for 70/30 brass — integrated against the
CIE 1931 observer under D65:

| metal | F0 (linear R, G, B) |
| --- | --- |
| Copper | 0.932, 0.623, 0.522 |
| Brass | 0.910, 0.778, 0.423 |
| Silver | 0.989, 0.984, 0.977 |
| Gold | 1.000, 0.728, 0.365 (red clamped from 1.038) |
| Steel | 0.530, 0.513, 0.494 |

In Metal colour the metal's F0 is also the albedo of its patina, at a fifth of the strength.

**Roughness** — the GGX α, **1/64 to 1**; **0.5 by default**. Low is a mirror: the highlight is
a small, bright point at the half-vector point and the pool under the lamp is intense. High is a
sheen spread across the relief. At normal incidence the highlight's peak is Intensity × F0 / 4α²,
which is why the default is not lower: at α 0.35 copper under the lamp already clips.

**Patina** — **0 to 1**; **0.6 by default**. Horizon-based occlusion over the same height field —
eight directions, four radii out to 14 px — darkens the diffuse and ambient terms by
mix(1, AO, Patina), and never the highlight: tarnish sits in the recesses, and the polished high
points still gleam.

**Colour** — **Metal** or **Clip**; Metal by default. Metal loses the clip's colour entirely, as a
metal sheet does, and keeps its alpha: a clip with transparency is a sheet with holes. Clip is a
painted sheet — dielectric F0 0.04, the clip as the albedo with a full diffuse term — so a dark
clip is a dark painted sheet with only a faint highlight; a painted sheet needs a clip with colour
across it.

---

## The Lamp group

**Lamp X**, **Lamp Y** — the lamp's position over the sheet as fractions of the frame, **0 to
1**; **0.3 and 0.75 by default**, up and to the left. The pendulum's displacement is added to this
rest position.

**Lamp Height** — the lamp's height above the sheet, **0.02 to 2 frame heights**; **0.5 by
default** (0.25). Low is raking light: every ridge throws a long shadow and the relief reads far
deeper than it is. High is flat, even light with short shadows. The pendulum's rise as it swings
adds to it.

**Intensity** — **0 to 2**; **0.9 by default** (0.45). Normalised at the lamp's rest height, so
**1 is a white Lambertian sheet directly under the lamp reading 1**, at any raster. Real footage is
mostly dark, so on a dark clip the sheet is a flat metal sheet with a pool of light on it; lower
this, or raise Roughness, if the pool clips.

**Ambient** — **0 to 1**; **0.12 by default**. A flat term times the albedo, unshadowed, so the
parts of the sheet the lamp does not reach are not black. Patina darkens it.

**Shadows** — a strength, **0 to 1**; **1 by default**. From every pixel a ray is marched toward
the lamp in 32 steps over the same height field, reading the surface between texels, and where it
is blocked the direct light — diffuse and highlight both — is scaled down by this much. The ambient
is never shadowed. It is a strength rather than a switch so a shadow can be softened without
losing it.

---

## The Pendulum group

**Cord Length** — **0.05 to 2 m**; **0.5 m by default** (0.25). One frame height is one metre, so
the period is what a real cord of that length gives, 2π√(L/g): 0.45 s at the shortest, **1.42 s at
the default**, 2.8 s at 2 m — a little longer at large angles, as a real pendulum's is. The same
length also sets how far the lamp is seen to swing: a 2 m cord swings the lamp two frame heights
for the same angle, which is why Swing should come down with it.

**Damping** — the damping ratio ζ, **0.005 to 1**; **0.05 by default**, which is Q = 10: the
swing decays to a third in about four cycles. High damping stops the lamp within a swing; the
lowest lets it go on for a minute.

**Swing** — how much angular velocity a full-strength push adds, **0 to 3 rad/s**; **1.2 rad/s by
default** (0.4). A kick from the button is a full push; an audio onset's push is scaled by how far
above its baseline the bass rose. Each kick's direction steps round the golden angle from the last
— the first along +x — so a run of hits swings the lamp in a rosette and the same music swings it
the same way on every machine.

**Kick** — a button. One full-strength push, on the frame it is pressed. Two planar pendulums, one
along x and one along y, are integrated with classical Runge-Kutta at a fixed 1/480 s substep in
double precision; the elapsed time between frames is clamped to half a second, so a dropped frame
or a scrub does not hurl the lamp.

**Audio** — the composition's FFT, 64 bins. The onset detector listens to the **lowest eight**
bins: the mean of √bin, a 0.1 s baseline, and a kick when the rise above that baseline is still
climbing, above 0.15, and above what is left of the last kick's 0.3 s envelope. A held note kicks
once. It is **primed** on the first frame and after every clock jump, so a clip triggered into loud
music does not start with a kick. Nobody has measured what Resolume's bins carry, so the threshold
and the gain are stated, not tuned against a host; see Known limits.

---

## The Output group

**Mix** — the worked sheet against the untouched clip, **0 to 1**; **1 by default**. Zero is the
clip as it arrived. The pendulum keeps swinging underneath whatever Mix says.

**Show Height** — off by default. A diagnostic: the worked height field as grey, black at zero and
white at Depth, after Smooth and the punch. Use it to see exactly what the punch has formed and
what it has flattened.

---

## How it works

Twelve passes a frame at the defaults, nothing kept on the GPU between frames:

1. **Height.** Rec. 709 luma of the clip, inverted if asked, times Depth, into a float texture.
2. **Smooth.** A separable Gaussian, x then y, with weights normalised in double so a plane stays
   a plane.
3. **The punch.** Eight one-dimensional passes: erode x, erode y, dilate x, dilate y (the opening),
   then dilate x, dilate y, erode x, erode y (the closing), each with the paraboloid profile
   −k²/2R over |k| ≤ ⌊R⌋.
4. **Shade.** Normals from central differences of the worked height, read exactly with
   `texelFetch`; the lamp's position in pixels, the pendulum's displacement added; irradiance
   Intensity × (Lref / r)² with Lref the lamp's rest height; Lambert diffuse, GGX with
   height-correlated Smith and Schlick Fresnel for the highlight; visibility by a 32-step march
   toward the lamp, reading the sheet between texels; horizon occlusion for the patina; ambient;
   then Mix.

The pendulum and the onset detector live on the CPU and are the only state; a change of
resolution reallocates the buffers and leaves them alone, so the lamp keeps swinging through a
resize.

---

## Performance

Measured by the offline harness on an M4 Max at the defaults, best of three runs of 60 frames
after a warm-up, `glFinish` both sides, on a GPU shared with other work:

| | ms/frame | % of a 60 fps frame | with Punch Radius 16 px |
| --- | --- | --- | --- |
| 1280 × 720 | 0.31 | 1.8% | 0.55 |
| 1920 × 1080 | 0.51 | 3.1% | 1.21 |
| 3840 × 2160 | 2.61 | 15.7% | 5.38 |

The punch's cost is its radius: ⌊R⌋ taps an axis over eight passes. Nothing was timed inside
Resolume, and nothing was timed on Windows.

---

## If it looks wrong

**The whole frame is a flat sheet with a bright pool on it.** The clip is dark, so there is no
height to work. Feed it a clip with brightness across the frame, or lift it with a brightness
effect ahead of this one.

**The pool under the lamp is blown out.** Intensity is normalised at a white sheet, and the
highlight at normal incidence is Intensity × F0 / 4α². Lower Intensity, or raise Roughness.

**A smooth gradient has come out as rings or terraces.** 8-bit footage under a raking lamp. Raise
Smooth to σ 1 or 2 px.

**Fine detail has vanished.** That is the punch: nothing narrower than it is formed. Lower Punch
Radius, or look at Show Height to see what survived.

**The shadows are enormous.** Lamp Height is low, or Depth is high. Raise the lamp.

**The lamp does not swing.** Nothing is routed to Audio, or the music has no onsets above the
baseline in the bass. Press Kick to check the pendulum, then route audio. Damping at 1 stops a
swing within a cycle.

**The lamp swings off the frame.** A long cord swings far for the same angle. Lower Swing, or
shorten the cord.

**The picture is black and white.** Colour is Metal, and a metal sheet has no picture colour;
Steel and Silver are close to neutral. Use Colour: Clip for a painted sheet.

**Clip colour is just the clip, faintly lit.** A painted sheet has the dielectric's tiny
highlight (F0 0.04), and its brightness is the clip's. It shows its relief only where the clip has
brightness.

**A clip with transparency has holes.** The clip's alpha is kept: a sheet with holes in it. Put
something under it, or flatten the clip first.

**SW Repousse is not in the effects browser.** Check the folder under Installing, and that
Resolume was restarted.

**The effect does nothing at all.** A shader that will not compile looks exactly like that, and
the real message is in the log:

```
macOS    ~/Library/Logs/repousse/repousse.YYYY-MM-DD.log
Windows  %LOCALAPPDATA%\repousse\logs\repousse.YYYY-MM-DD.log
```

It records the GL vendor, renderer and version at load, which pass failed if one did, and at
frame 60 the host's clock and the unit the plugin decided it is in.

---

## Known limits

- **Never loaded into Resolume on macOS**, and **no audio has ever reached it**: the onset
  detector has only met the harness's synthetic spectra. Whether Resolume's bins carry the level
  the 0.15 threshold and the ×4 gain assume is the first thing to check in a host.
- **The relief is brightness.** A bright shirt is a bump whether or not it is in front of
  anything; a dark frame is a flat sheet with a lamp on it.
- **The punch is a rounded tip on a square shank**, not a ball on a round one. The profile is the
  paraboloid osculating the sphere at its tip, so on ridges, edges and scratches it is the sphere
  to within 0.9% of R at R/2 (the closed-form error is in `AGENTS.md`); on an isolated dot it is a
  little squarer than a ball. The trade buys eight cheap one-dimensional passes.
- **The metal table is only as good as its n,k data.** Each metal is one published measurement of
  one sample (a thin film for Johnson & Christy, a bulk alloy for Querry); other datasets give
  silver 0.95–0.98 and copper 0.93–0.96 in red. The numbers are those datasets', computed as
  `tools/f0.py` states, not a measurement of any sheet.
- **Two planar pendulums are not a spherical pendulum**: a kick off-axis while swinging precesses
  differently from the real thing. Nobody will see it, but it is not the real thing.
- **The shadow march is 32 steps whatever the span**, so a long raking shadow at 4K spans hundreds
  of pixels and its boundary is placed to a few.
- **Checked at 320 × 180 and 1280 × 720** in the harness, and only timed at 4K.
- **Only ever run on an Apple M4 Max**, although the macOS build contains an Intel slice. On
  Windows, see the note at the top of this guide.
- **No presets** and no OpenFX version.
- **There is a browser demo** at [repousse-demo.stoatworks-labs.com](https://repousse-demo.stoatworks-labs.com/). It is a port to a
  web page, not the plugin: the shaders run in WebGL2, the pendulum is rewritten in JavaScript,
  and there is no audio, so its kicks are the button's. The page lists what it does not
  reproduce.

---

## About

The last group, **About**, carries the plugin's name, version, licence and maker, and buttons
that open this user guide ([stoatworks-labs.com/software/repousse/guide/](https://stoatworks-labs.com/software/repousse/guide/)),
the project page, the source on GitHub and the support page in your browser.

## Reporting something

[github.com/stoatworks-labs/repousse/issues](https://github.com/stoatworks-labs/repousse/issues).
A screenshot, the Sheet and Lamp settings, whether audio was routed, and the composition's
resolution and frame rate are usually enough. If the effect did nothing, attach the log.
