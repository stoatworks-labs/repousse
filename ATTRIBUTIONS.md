# Attributions

Repousse is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is normally generated — the master lists live in the `stoatworks-backend` repo and
are pushed out by `scripts/sync-attributions.py`. This copy is a provisional hand
copy in the generated shape (2026-09-24); the first real sync overwrites it.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### PassBuffer — Stoatworks tinsel

<https://github.com/stoatworks-labs/tinsel>  
Licence: MIT  
Copyright: Stoatworks Labs

The off-screen buffer wrapper is tinsel's, by way of rebate, standards and contour; the lesson that every `ffglex::Scoped*` binding clears rather than restores, and that buffers are allocated before anything binds, is tinsel's.

### Harness shape, --pipe contract, the host clock and the negative-control pattern — Stoatworks contour and standards

<https://github.com/stoatworks-labs/contour>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness shape (a session round the real plugin class in a headless CGL context, a synthetic 60 fps clock, checks read out of the picture), the --pipe contract with SIGPIPE ignored, --offline, check-shaders.sh, the host clock (readout's unit voting, kept in double), the perturb-bit negative controls and the separable blur come from contour, by way of standards, pitch and slowscan.

### Audio declaration and onset detector — Stoatworks regauss and contour

<https://github.com/stoatworks-labs/regauss>  
Licence: MIT  
Copyright: Stoatworks Labs

The 64-bin FFT declaration, the sqrt-of-bin mean and the bass range are regauss's; the primed baseline-flux detector is contour's SeaDrive, reworked to deliver one kick per hit.

### Damped oscillator with substepped integration — Stoatworks gaffer

<https://github.com/stoatworks-labs/gaffer>  
Licence: MIT  
Copyright: Stoatworks Labs

The shape of the pendulum — kicks as velocity impulses, a fixed substep, the elapsed time clamped rather than the step count — is gaffer's Rattle, with the linear spring replaced by the large-angle pendulum and semi-implicit Euler by classical Runge-Kutta.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned to b1afaf9.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Repoussé and chasing

Sheet metal worked from behind with punches and from the front with chasing tools, then lit. The two facts the model rests on — the punch sets the finest detail, and metal is lit rather than shaded — are the craft's, not any one piece's.

## Standards and published specifications

What the implementation is measured against.

- **P. B. Johnson and R. W. Christy, "Optical constants of the noble metals", *Phys. Rev. B* 6, 4370–4379 (1972), and "Optical constants of transition metals: Ti, V, Cr, Mn, Fe, Co, Ni, and Pd", *Phys. Rev. B* 9, 5056–5070 (1974); M. R. Querry, "Optical constants", *Contractor Report* CRDC-CR-85034 (1985)** — the complex refractive indices of copper, silver and gold (1972), iron (1974, used here for steel) and 70/30 brass (1985) from which the metal table's F0 is computed, as tabulated in the [refractiveindex.info](https://refractiveindex.info) database (public domain, CC0 1.0). The computation is `tools/f0.py`: F0(λ) = ((n−1)² + k²)/((n+1)² + k²) integrated against the CIE 1931 2° colour-matching functions under illuminant D65 (both as published by [CVRL](http://www.cvrl.org)), taken to linear sRGB with the IEC 61966-2-1 matrix; gold's red, which lies outside the sRGB gamut, is clamped to 1.
- **Bruce Walter, Stephen R. Marschner, Hongsong Li and Kenneth E. Torrance, "Microfacet Models for Refraction through Rough Surfaces" (EGSR 2007)** — the GGX normal distribution.
- **Eric Heitz, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs" (JCGT 2014)** — the height-correlated Smith visibility term, in the form V = 1 / ( 2 ( n·l √( n·v² ( 1 − α² ) + α² ) + n·v √( n·l² ( 1 − α² ) + α² ) ) ).
- **Christophe Schlick, "An Inexpensive BRDF Model for Physically-based Rendering" (Computer Graphics Forum 1994)** — the Fresnel approximation.
- **The large-angle pendulum period series**, T / T₀ = 1 + θ₀²/16 + 11θ₀⁴/3072 + 173θ₀⁶/737280 (the expansion of the complete elliptic integral of the first kind), as in any mechanics text.
- **Melissa E. O'Neill, "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation" (Harvey Mudd College, 2014)** — the pcg_hash output mix the harness uses for its noise, written out rather than copied from anyone's source.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
