# repousse — orientation for another LLM (or a newcomer)

**What it is:** an FFGL 2.1 effect for Resolume Arena/Avenue that hammers the
picture into a metal sheet and lights it with a lamp on a swinging cord. C++17 +
GLSL 4.10, CMake, universal macOS `.bundle` and a Windows `.dll`. MIT, intended home
`github.com/stoatworks-labs/repousse`. Built 2026-09-24 as tranche four of the
Resolume plugin ideas; the idea was Allan's own pick.

`CLAUDE.md` is the command reference — build, install, verify. This file is the
*why*: read it before touching the punch, the lamp's law, the shadow march or the
pendulum.

---

## The one idea

Repoussé is a sheet of metal worked from behind with punches. Two physical facts make
the look, and both belong in the model rather than in a filter:

1. **The punch sets the finest detail.** A sheet can be pushed only as sharply as the
   tool that pushes it, so the relief's curvature is bounded by the punch's tip. The
   height field built from luma is opened and closed by the punch's profile, and
   detail narrower than the punch is never formed. It is not blurred away.
2. **Metal is lit, not shaded.** A point lamp at a real position with inverse-square
   falloff, a GGX microfacet highlight with the tabulated F0 of a real metal, a
   small diffuse term for the patina, and self-shadowing by a march over the same
   height field.

The lamp hangs on a cord: a damped large-angle pendulum with the period its length
gives it. An onset in the audio, or the Kick button, adds angular velocity. That is
the only audio coupling.

### The model, written down

**Height.** `h = Depth · luma`, Rec. 709 luma clamped to 0..1, `Invert` takes
`1 − luma`. Depth is in **pixels of height**, so the relief is a thing done to the
picture's pixels: the same clip at 4K is hammered finer, not deeper. Then, if
`Smooth` > 0, a separable Gaussian of σ = 3p px (radius ⌈3σ⌉ ≤ 9, weights normalised
in double so a plane stays a plane). That pass is not detail control — the punch is —
it is the answer to 8-bit footage (see the traps).

**The punch.** The spec says a spherical profile. The sphere is not separable, and a
2-D disc footprint costs O(πR²) per pass per pixel — with four passes at R = 10 that
is 1,268 taps a pixel, tens of milliseconds at 1080p. So the punch is **the
paraboloid osculating the sphere at its tip**, `b(k) = −k²/2R`, which IS exactly
separable (`b(u,v) = b(u) + b(v)`), has the sphere's tip curvature `1/R` — the
physical claim — and is truncated at `|k| ≤ ⌊R⌋` per axis: a rounded tip on a shank
of half-width R. Beyond R the shank meets the sheet, so a slot wider than the punch
lets the whole tool in and a ridge wider than the punch keeps its full height, which
is what the spec's ridge claim says.

Against the sphere `s(r) = √(R² − r²) − R`, the tip's error is, in closed form,

    p(r) − s(r) = r⁴ / ( 2R ( R + √(R² − r²) )² )        0 ≤ r ≤ R

which is `r⁴/8R³` near the tip, **0.9% of R at r = R/2**, and R/2 at the rim
(`rptest --profile` verifies the identity to 1.7e-16 R). Off the axes the separable
support is the square `|u|,|v| ≤ R` rather than the disc: on a ridge, an edge or a
scratch (anything constant along one axis) the punch is exactly the 1-D profile; on an
isolated dot it is a little squarer than a ball. That is the honest deviation, and it
buys eight cheap 1-D passes.

The opening (erode then dilate) is the punch from behind; the closing (dilate then
erode) is the chasing tool from the front. Erosion is `min_k h(p+k) + k²/2R`,
dilation `max_k h(p+k) − k²/2R`, each in x then y. The closed form the harness
predicts from: a ridge or square of width w has `d = ⌈w/2⌉` from its middle to the
nearest outside pixel, and comes out `min(H, d²/2R)` tall if `d ≤ ⌊R⌋`, else `H`.
The closing does not move a peak (an upturned paraboloid touching a downturned one
touches it at the apex only).

**The lamp.** Position in pixels: `(Lamp X · W, Lamp Y · H, Lamp Height · H)` plus
the pendulum's displacement. One frame height is one metre — that single scale ties
the pendulum's period (seconds, from metres and g) to the distance the lamp is seen
to swing. Irradiance `E = Intensity (Lref / r)²`, `Lref` the lamp's nominal height,
so **a white Lambertian sheet directly under the lamp reads Intensity** whatever the
raster; the pendulum's rise changes r, not the reference.

**The BRDF.** Viewer straight down, `v = (0,0,1)`. Diffuse `E · n·l · kd · albedo`
(the `1/π` folded into the lamp's normalisation, with the specular scaled by π to
match); GGX `D = α²/π(n·h²(α²−1)+1)²`, height-correlated Smith
`V = 1/2(n·l √(n·v²(1−α²)+α²) + n·v √(n·l²(1−α²)+α²))`, Schlick
`F = F0 + (1−F0)(1−v·h)⁵` written out as a product (no `pow`). Metal mode: F0 the
metal's, albedo the metal's F0 with `kd = 0.2` (the patina's share); Clip mode:
F0 = 0.04, albedo the clip, `kd = 1`. At normal incidence the whole specular is
`E F0 / 4α²`, which is what `--fresnel` reads. Ambient `Ambient · albedo`. Patina
multiplies the diffuse and the ambient by `mix(1, AO, Patina)` and never the
highlight: polished high points still gleam.

**Visibility.** From `P = (pixel centre, h)` march toward the lamp in 32 equal steps
over the span where the ray is still below Depth (`fD = (Depth − h)/(Lz − h)` of the
way), reading the sheet **between texels** (bilinear) with a bias of 0.01 px of
height. The normal reads texels exactly (`texelFetch`); the march reads the surface
between them. Both are the model.

**Occlusion.** Eight directions, radii 2, 5, 9, 14 px, the maximum tangent of the
horizon per direction, `AO = 1 − mean(sin horizon)`.

**The pendulum.** Two planar pendulums, x and y, each
`θ'' = −(g/L) sin θ − 2ζω₀θ'`, classical Runge-Kutta at a fixed 1/480 s substep in
double, the elapsed time clamped to 0.5 s. A kick adds `Swing · strength` rad/s along
a direction that steps round the golden angle per kick, so a run of hits swings a
rosette and the same music swings the same way on every machine. The picture sees
`L sin θ` per axis and a rise `L(2 − cos θx − cos θy)`.

**Onsets.** The mean of √bin over the lowest 8 of the 64 bins, a 0.1 s baseline,
flux = the rise above it, strength = min(1, 4·flux). A kick is the *arrival* of a
hit: the frame on which the flux is still rising, above 0.15, and above what is left
of the last kick's 0.3 s envelope; its size is how far above. A held note kicks
once. **Primed** on the first frame and after every clock jump.

---

## Shape of the code

    source/Controls.*       0..1 host parameters to units; the metal table; the
                            perturb bits
    source/Audio.*          OnsetDetector, CPU only
    source/Pendulum.*       the lamp on its cord, CPU only
    source/Clock.*          the host's clock, unit voted, kept in double (contour's)
    source/Shaders.cpp      the five stages: height, blur, morph, shade (+ vertex)
    source/PassBuffer.*     FFGLFBO with the leak fixed (tinsel's)
    source/Repousse.*       the plugin: parameters, buffers, the passes
    source/Diag.*           a log file, for the shader that will not compile
    tools/rptest/           the offline harness: eight rendered checks, six offline,
                            seventeen negative controls, --pipe, --bench
    tools/sweep.py          no control is silently dead
    tools/check-shaders.sh  the exact GLSL through glslc (CI's only look at it)
    tools/verify.sh         all of it, on a fresh universal build

Twelve passes at the defaults: height, two blur axes, eight morphology axes, shade.
Nothing on the GPU outlives a frame; the pendulum and the detector are the only state,
on the CPU, and a resize leaves them alone.

---

## The traps

Ordered by how much time they cost, all hit here on 2026-09-24.

**`InitGL` cleared a pending Kick, and `--pendulum` measured a lamp at rest for a
while without saying so.** The harness pressed Kick before `begin()` (InitGL), InitGL
zeroed `kickPending`, no swing happened, and the check reported "0 full cycles". Worse,
the swinging half of `--resize` compared two motionless lamps and passed with a
difference of exactly zero. InitGL no longer touches `kickPending` (a press before the
first frame is a press), and every harness press now comes after `begin()`, as an
operator's does. Lesson: a check that passes with 0.00e+00 on a *dynamic* claim
deserves a second look.

**The flat sheet under the lamp is a second, correct highlight.** On a flat sheet
`n = l = v` directly beneath the lamp, so the GGX lobe peaks there too. `--specular`
searched the whole frame for the brightest pixel; at 320×180 the dome's highlight won,
at 1280×720 a lamp placed in frame heights sat 864 px up and the ground's highlight
won by 514 px. The check now blacks out everything off the dome before searching, and
places the lamp in **pixels** so the geometry is identical at every raster.

**A wider raster made two Lambert planes not fit under Depth, and a `continue`
skipped them silently.** Four planes at 320×180, two at 1280×720, both "0 failed".
The planes are now defined by their rise across the frame as a fraction of Depth, so
all five exist at every raster, and a plane that does not fit is a FAIL.

**The unbounded paraboloid is a bullet, not a ball.** The first design used the
paraboloid without truncation, with the taps chosen so truncation was exact against
the height range. Working the ridge law through showed a tall ridge 9 px wide under a
4 px punch coming out 3 px tall: the paraboloid keeps constraining beyond R, a ball on
a shaft does not. Truncating at `|k| ≤ ⌊R⌋` gives the sphere's threshold exactly and
makes each pass cheaper (⌊R⌋ taps, not ⌈√(2RD)⌉).

**8-bit footage is terraces under a raking lamp.** A one-code step in a gentle
gradient is a 0.06 px riser, and a lamp at 45° lights every riser: the card's dome
came out as concentric rings. Contour hit the same thing. `Smooth` (σ 1 px by
default) exists for this and nothing else; the spec's control list did not have it.

**The pool under the lamp clips.** At normal incidence the specular is `E F0/4α²`;
at α 0.35 that is 1.9 for copper at Intensity 1. Defaults moved to α 0.5 and Intensity
0.9 (peak 0.86). Real footage is mostly dark, so on a dark clip the sheet IS a flat
copper sheet with a pool of light on it; the operator has Intensity and Roughness.

**The kick lands on the frame the hit arrives.** The onset is detected, the kick
applied, and then the frame's dt integrated, so the lamp has already moved 4.5 px on
the frame of a 3 rad/s kick at 180 rows. `--prime` first expected rest on that frame.
It is the intended latency; the check now expects rest the frame before.

**Inherited from the fleet, and all still true here:** `ScopedFBOBinding` does not
restore the viewport; every `ffglex::Scoped*` clears to 0 on exit, so every `Ensure()`
happens before anything binds; `FFGLFBO::Release()` leaks the colour texture; a
ranged parameter cannot have a ranged default; `SetTextParameter` must be overridden
for the About block; the core is an OBJECT library; `FFGLScopedFBOBinding.h` is not in
the umbrella header; Resolume's clock overflows a float; `layout`, `flat`, `half` and
friends are reserved. None of them bit this time because contour's code carried the
answers in.

---

## Would this hold on another rasteriser, at another raster?

One line per check. Every tolerance is derived, not fitted; every check ran at 320×180
and 1280×720 in `verify.sh`.

What makes them rasteriser-proof by construction: **every coordinate is an integer**
(`gl_FragCoord` and the viewport, never an interpolated uv); **every read that decides
a normal is `texelFetch`**; **every conversion is done on the CPU in double** and
handed over as a float uniform; **the input is a float texture**, so the height the
shader sees is the float the harness wrote times Depth; and the two reads that ARE
filtered (the shadow march and the occlusion) are declared as the model of the surface
between texels, and their checks carry the filtering's consequence (the cliff top at
the last high texel's centre). What a driver can still do differently is arithmetic:
GLSL 4.10 §8.2 bounds it, and each tolerance carries its share. Deliberately not
relied on: `pow` (Schlick is a written-out product), exact cancellation, `mix(a,b,1)`.

| check | what it measures | tolerance and where it comes from | raster dependence |
| --- | --- | --- | --- |
| `--lambert` | every pixel and channel of five planes under five lamps against `I (Lref/r)² n·l kd F0` | 3 float roundings per height sample (the picture's value, the luma dot, ×Depth) → 3 ulp(h_max) per gradient component through a two-pixel central difference; `\|∂n/∂g\| ≤ 1` → `3√2 ulp(h_max)` in n·l, times `I kd`; plus 64 × 2⁻²⁴ for normalize, dot, r, the division and the products. The Smooth plane excludes the kernel's 7-px reach off the clamped border | planes are a fraction of Depth across the frame, so slopes in px/px shrink at wider rasters and h_max is the same; measured 1.8e-7 against 4.6e-6 at both |
| `--specular` whole | the brightest dome pixel against the pixel holding the half-vector point | **exact pixel**. The product's peak is displaced from the half-vector point by the smooth factors by at most `a²G/4` rad (the GGX log-slope is `4θ/a²` near the axis; `G ≤ 4` rad⁻¹ for a lamp ≥ 45° up and ≥ 2R away, from `n·l`, `1/r²`, Smith and a negligible Fresnel), i.e. `a²·4R/3` px on a sphere of radius R with the lamp ≥ 2R away: **0.083 px at a = 1/32**; plus ~3e-3 px for the central difference against the analytic normal | none: the dome is 64 px and the lamp is placed in pixels at both rasters; the check asserts the geometry it assumes |
| `--specular` fractional | the same, the point off pixel centres | the lattice's half pixel plus the 0.083 px shift; measured 0.36, 0.50, 0.50 px | none |
| `--fresnel` | the pixel under the lamp on a flat black sheet, diffuse off, against `I F0/4α²` | 32 × 2⁻²⁴ × max(1, scale): the lamp lands within 1e-4 px of the pixel centre (a float parameter), so every cosine is 1 to under an ulp; what is left is π, the division and the products. Measured 5e-8 | none |
| `--shadow` | the run of exactly-zero pixels from the step on three rows against `xe + h(xe − Lx)/(Lz − h)` | **one march step + half a pixel**: the march samples the ray at 32 points over `fD·\|d_xy\|`, so the boundary is placed within `fD \|d_xy\| / 32` at the boundary pixel; the boundary is read from the pixel lattice, ±0.5. Zero is exact (no ambient; visibility multiplies both direct terms) | the lamp's height is in frame heights, so its elevation rises with H (36°→74°) and the step shrinks: worst 2.03 of 2.06 px at 320×180, 0.30 of 0.60 at 1280×720 |
| `--punch` | the peak of the worked ridge/square via Show Height against `min(H, d²/2R)` or H; a groove's floor against the stated 1-D open-close in double | 8 ulp(Depth)/Depth: k² exact, 1/2R exact at R = 4, one rounding per candidate per pass, eight passes. Measured **0.0e+00** | none: features are in pixels |
| `--pendulum` period | same-direction zero crossings of the lamp's x read from the picture against `T₀ S(θ̄)/√(1−ζ²)`, θ̄ the cycle's mean amplitude | 3e-4: a crossing time is off by the position's error (a parabola through a smooth even peak on a float readback, under 1e-3 px) over the velocity at the crossing (~250 px/s at 180 rows), plus the interpolation's cubic term (~1e-5 s); the series' next term is 2e-5 θ⁸; the decay across a cycle at ζ 0.01 moves the mean-amplitude period ~1e-5. Measured 3.9e-5 | the swing scales with H (57 px at 180) so the position term shrinks at wider rasters; measured alike |
| `--pendulum` decay | the log decrement between same-sign peaks against `2πζ/√(1−ζ²)` | `ζθ₀²/4` for the nonlinear pendulum's departure from exponential decay (an O(θ²) effect; the coefficient is a bound, not a derivation) + 2e-3 (a sampled peak is low by up to (ωΔt)²/8 = 6e-4 of itself, twice, in a log). Measured 6.1e-4 / 1.2e-3 | none beyond the above |
| `--prime` | the lamp's x over 60 frames of loud audio, a step, a scrub | 0.01 px on a lamp a kick moves by tens of pixels; the kick count exactly | none |
| `--resize` | the frame after a resize against a fresh instance; the swinging lamp's x/W against an unresized run | **every value equal**; one pixel of either raster (measured 0: the same radians, the same fraction) | resizes to 1.5W+1 × 0.75H+1 at any raster |
| `--controls` | nine mappings at 21 points, the metal table, the constants, the defaults | 1e-12 relative (two statements of one definition in double); the table exact | none (no GL) |
| `--profile` | the paraboloid against the sphere | the closed form to 1e-12 R; < 1% R to R/2; R/2 at the rim | none (no GL) |
| `--detector` | priming, one kick per hit, the second hit's size, the release τ, a jump, dt = 0 | exact where exact (kick 1.0, none while held), τ to 1e-9 from two frames' envelope ratio, the second kick to 1e-9 | none (no GL) |
| `--model` | the pendulum's own trace, at 0.5 m and 1 m | 1e-4 on the period, 1.5e-3 on the decrement (no picture in the way); a 200-second run of one-second frames stays finite | none (no GL) |
| `--clock` | a six-day millisecond clock against a fresh seconds one driving detector + pendulum over 3,000 frames | 1e-6 m: a double at 1.02e6 s resolves 1.2e-10 s; a float resolves 0.0625 s and a 1/60 s frame reads as 0 or 0.0625. Measured 6e-11 | none (no GL) |

What might still differ on another rasteriser: a driver whose bilinear filter is not
the bilinear the spec describes would move the cliff top the shadow is cast from by a
fraction of a texel — within the half-pixel already allowed; and a driver whose `sin`
differs (the occlusion's eight directions) is not on any check's path. A software
context is a different compiler for the same GLSL; `check-shaders.sh` covers syntax on
the GL-less runner and the rendered checks run with `--allow-no-gl` so a runner without
a context skips loudly.

### The negative controls

`rptest --negative` runs twelve against rendered checks and `--offline` five against
the model; `--perturb BITS` runs any check verbosely against one. Each perturbs the
*plugin's* model — a bit in `repousse::perturb`, zero in the shipped plugin — never
the harness's expectation. Two further bits, `kNoSpecular` and `kNoDiffuse`, are not
perturbations but isolations: `--lambert` runs with the specular off and `--specular`
and `--fresnel` with the diffuse off, so one term can be measured on its own.

| perturbation | what fails, measured at 320×180 |
| --- | --- |
| the 1/r² dropped (the spec's) | `--lambert`: every plane, errors of order 1 where r ≠ Lref |
| the GGX lobe evaluated on n·l instead of n·h | `--specular`: the peak moves to the point whose normal points AT the lamp, ~half the lamp's zenith angle away (tens of pixels) |
| every F0 replaced by 0.5 | `--fresnel`: every metal reads 0.5 |
| no shadow march (the spec's) | `--shadow`: the shadow run is 0 px on every row |
| a flat punch (b = 0) | `--punch`: every ridge narrower than the punch vanishes to 0 instead of d²/2R |
| the punch radius 10% wide | `--punch`: d²/2R off by 10% (2.8e-3 of Depth at d = 1) and the 9 px ridge now under the punch |
| sin θ → θ | `--pendulum`, `--model`: periods 2.7% short of the large-angle prediction |
| g 3% high | `--pendulum`, `--model`: periods 1.5% short |
| ζ 15% high | `--pendulum` (decay), `--model`: the decrement 0.36 against 0.31 |
| onset detector unprimed | `--prime`, `--detector`: a kick on frame 0 from a constant spectrum, and after the scrub |
| the old height buffers kept on resize | `--resize`: the frame after differs in most values |
| the pendulum reset on resize | `--resize`: the lamp snaps to rest and leaves the unresized run |
| the clock kept in float | `--clock`: the two lamps part company by centimetres |

Read what each failed on, not only that it did: the list above is from `--perturb`
runs, and every one fails for the reason it was built for.

### The mutation

One character of the shipped GLSL, on a clean committed tree (3fbdec1): in the shade
pass, `float dd = ndh * ndh * ( a2 - 1.0 ) + 1.0;` → `+ 1.1` — the GGX denominator's
constant. Caught at both rasters by `--fresnel` (all ten cases: every F0 read back at
about half, 0.487 for copper's 0.955 as the table then stood) and by `--specular` (two of six cases: the
broadened lobe let the smooth factors move the peak one pixel, past the whole-pixel
exactness and the fractional half-pixel bound). `--lambert` and `--shadow` passed,
correctly: the first runs with the specular off, the second measures a shadow.
Reverted with `git checkout source/Shaders.cpp`; the tree was clean before and after.

---

## Decisions taken without asking

- **The punch is a paraboloid tip on a square shank**, not a sphere on a round one.
  Reasoned above; the profile error is in closed form and `--profile` checks it.
- **Smooth was added** to the Sheet group, σ = 3p px, default σ 1 px. Not in the
  spec's control list; there for 8-bit terraces. Every physics check runs it at 0
  except the one Lambert plane that proves it leaves a plane a plane.
- **Units.** Depth and Punch Radius in pixels; Lamp X/Y fractions; Lamp Height and
  the cord in frame heights = metres; Swing in rad/s. All mappings linear or a clamp:
  Depth 64p, Punch 16p, Smooth 3p, α = max(1/64, p), Lamp Height max(0.02, 2p),
  Intensity 2p, Cord max(0.05, 2p), ζ = max(0.005, p), Swing 3p.
- **Intensity is normalised at the lamp's nominal height**: 1 is a white sheet under
  the lamp, at any raster.
- **Shadows is a strength (0..1)**, not a switch, and multiplies the diffuse and the
  specular; the ambient is unshadowed. Patina darkens diffuse and ambient only.
- **Metal mode** loses the clip's colour entirely (that is what a metal sheet does);
  the clip's alpha is kept, so a clip with transparency is a sheet with holes. **Clip
  mode** is a painted sheet: dielectric F0 0.04, the clip as albedo.
- **The metal table is computed, not quoted.** `tools/f0.py` derives each F0 from
  published complex refractive indices — Johnson & Christy 1972 for copper, silver and
  gold, Johnson & Christy 1974 for iron (the plugin's steel), Querry 1985 for 70/30
  brass (C260) — as tabulated in the refractiveindex.info database (CC0), integrated
  against the CIE 1931 2° observer under D65 and taken to linear sRGB; the data is
  committed under `tools/f0-data/` so the run is offline and reproducible. Gold's red
  comes out 1.038 (outside the sRGB gamut) and is clamped to 1. The first build quoted
  the widely reproduced *Real-Time Rendering* 4th ed. Table 9.2 from memory; the
  computed brass agrees with it to three decimals, copper and gold are within 0.03,
  silver and iron differ more (0.06 in blue for silver, 0.08 for iron), which is the
  choice of n,k dataset, not an error in either. `f0.py --check` runs in verify.sh.
- **The detector listens to bins 0–7 of 64** (regauss's Bass band) and assumes nothing
  about their law. There is no band control; the spec had none.
- **Kick direction** steps round the golden angle per kick, the first along +x.
- **Kicks land on the frame the hit arrives**, and the frame's dt is integrated after
  them.
- **Defaults:** Depth 16 px, Punch 4 px, Smooth σ 1 px, Copper, α 0.5, Patina 0.6,
  Metal colour; lamp at (0.3, 0.75) half a frame up, Intensity 0.9, Ambient 0.12,
  Shadows 1; cord 0.5 m (1.42 s), ζ 0.05 (Q 10), Swing 1.2 rad/s. Checked on six of
  Resolume's bundled demo clips through `--pipe`: dark clips are a flat sheet with a
  pool of light and the relief where the picture is; transparent clips keep their
  holes. None flooded.
- **No OpenFX, no browser demo, no factory presets** — not in scope for 0.1.0.
- `StoatworksAbout.h` is hand-written in the generated shape with `guide=""` and
  `page=""`; `ATTRIBUTIONS.md` likewise. The release step syncs both.

---

## What is genuinely verified, and what is assumed

**Verified, by measurement, on this machine (M4 Max, macOS 26.4, 2026-09-24):**

Everything in the table above, at 320×180 and 1280×720; the seventeen negative
controls; the mutation; all 20 controls live (`tools/sweep.py`); five shaders through
glslc; the `--pipe` contract including `head -c 1` giving exit 1; a universal bundle
(`x86_64 arm64`) exporting `plugMain` and carrying `RP01`; `oxbow probe` reporting
`SW Repousse` / `RP01` / `effect` and `oxbow selftest` rendering 120 frames through
`plugMain`. Render cost at the defaults, best of three runs of 60 frames after a
warm-up, `glFinish` both sides, on a GPU shared with seven other builds:

| | ms/frame | % of a 60 fps frame | Punch 16 px |
| --- | --- | --- | --- |
| 1280×720 | 0.31 | 1.8% | 0.55 |
| 1920×1080 | 0.51 | 3.1% | 1.21 |
| 3840×2160 | 2.61 | 15.7% | 5.38 |

**Assumed, or not done:**

- **Never loaded into Resolume.** Everything is the offline harness and `oxbow`. How
  the 21 controls read in the inspector, how the Kick event arrives, whether the
  Pendulum group's five make sense next to each other — untested.
- **No audio has ever reached it.** The detector has only met the harness's synthetic
  spectra. Whether Resolume's bins carry the level the 0.15 threshold and the ×4 gain
  assume is the first thing to check in a host.
- **The F0 table is only as good as its n,k data.** Johnson & Christy's films and
  Querry's alloy are one measurement each; other datasets in the same database give
  silver 0.95–0.98 and copper 0.93–0.96 in red. The plugin's numbers are those
  datasets' numbers, computed as `tools/f0.py` states, not a measurement of any sheet.
- **The Windows build has never been run**, or built.
- **The nonlinear decay bound `ζθ₀²/4`** is asserted as a bound on an O(θ²) effect,
  not derived; the decay check keeps θ₀ at 0.13 rad so it is 4e-4 of the decrement
  either way.
- **The specular shift bound's G ≤ 4 rad⁻¹** is an estimate of four log-derivatives
  under the geometry the check asserts, not a proof.
- Everything here comes from one M4 Max, never from CI — hosted macOS runners have no
  GPU, so `rptest`'s GL measurements cannot run there. The CI job compiles, runs
  `--offline`, and tries the rendered checks with `--allow-no-gl`.

---

## Open questions

- Should the punch be a true ball (a 2-D disc-footprint pass at small R, falling back
  to the separable tip beyond)? The only visible difference is on isolated dots.
- Is `Smooth` before the punch the right order? After it, it would soften the punch's
  own edges, which the model says are the tool's.
- The shadow march is 32 steps whatever the span; a long raking shadow at 4K spans
  hundreds of pixels and the boundary is placed to a few. A step length in pixels
  with a cap would be finer near the lamp and no worse far from it.
- Two planar pendulums are not a spherical pendulum; a kick off-axis while swinging
  precesses differently. Nobody will see it, but it is not the real thing.
- A band control for the detector, and a `Bin Law` option as needle has, once
  somebody measures Resolume's bins.

---

## Siblings

The CMake shape, `Info.plist.in`, `Diag`, `Clock`, `PassBuffer`, the harness's session,
`--pipe`, `--offline`, the perturb bits, `check-shaders.sh`, `sweep.py` and `verify.sh`
come from **contour** (by way of standards, tinsel, rebate and pitch); the audio
declaration from **regauss**; the oscillator's shape from **gaffer**'s Rattle. The
`--pipe` frame format and the `--script` cue file are identical across the fleet on
purpose, so one build script can film any of them.
