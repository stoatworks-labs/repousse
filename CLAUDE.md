# repousse

The picture hammered into a metal sheet and lit by a lamp on a swinging cord. FFGL
**effect** for Resolume Arena/Avenue. C++/GLSL, CMake MODULE → universal `.bundle`
(macOS) + Windows `.dll`. MIT, intended home `github.com/stoatworks-labs/repousse`.

Read `AGENTS.md` before changing the punch, the lamp's law or the pendulum.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install into Arena: `cmake --install build`
- Render a frame offline: `./build/rptest --out /tmp/frame.png --size 1920x1080`
- Render it MOVING (the lamp is a pendulum; a single frame cannot show a swing):
  `./build/rptest --out /tmp/frame.png --frames 90`
- Set anything by name: `--set "Punch Radius=0.5" --set "Metal=3" --set "Kick=1"`
- List parameters: `./build/rptest --list`

## Verify
- Everything (~2 min, fresh universal build): `tools/verify.sh`
- No GPU needed: `./build/rptest --offline` (`--controls`, `--profile`, `--detector`,
  `--model`, `--clock`, `--names`, and their negative controls)
- The lamp's law on planes: `./build/rptest --lambert`
- The highlight at the half-vector point: `./build/rptest --specular`
- Each metal's F0 at normal incidence: `./build/rptest --fresnel`
- The shadow behind a step: `./build/rptest --shadow`
- The punch's ridge law: `./build/rptest --punch`
- The period and decay, read from the picture: `./build/rptest --pendulum`
- Primed on the first frame: `./build/rptest --prime`
- A resize mid-run: `./build/rptest --resize`
- Every check can fail: `./build/rptest --negative` (`--perturb BITS` runs one verbosely)
- Every check takes `--size`; run at 320x180 (CI's raster) as well as your own.
- CI's renderer: `RPTEST_RENDERER=software ./build/rptest --resize --size 320x180` (Apple's
  software renderer, not repeatable at the last bit; verify.sh runs this too).
- No dead controls: `python3 tools/sweep.py` (`--size WxH`, `--jobs N`)
- The shaders through glslc: `tools/check-shaders.sh build/rptest`
- The browser demo's shaders are the plugin's, character for character: `python3 demo/tools/check_shaders.py`
  (in verify.sh). The demo's CPU half (`demo/plugin.js`) is a hand port; only a reader checks it.
- Deploy the demo: `cf-run npx wrangler deploy` from the repo root (a push to main also deploys it);
  verify by content: `curl -s 'https://repousse-demo.stoatworks-labs.com/?cb=1' | grep -o '<title>[^<]*'`
- Footage: `ffmpeg ... -f rawvideo -pix_fmt rgba - | ./build/rptest --pipe --size WxH --script cues.txt | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -i - out.mov`

## Notes
- **Every conversion is on the CPU in double**; the shaders get pixels, a lamp
  position in pixels and float uniforms. `Controls.cpp` is the one place a control
  becomes a unit. One frame height is one metre (that ties the pendulum's period to
  the distance it swings).
- **The punch is a paraboloid tip on a shank of half-width R**, not a ball. It is
  exactly separable (eight 1-D passes), has the sphere's tip curvature, and ends at
  R, so anything wider than the punch keeps its full height. AGENTS.md has the
  profile error against the sphere in closed form.
- **Normals read texels with `texelFetch`; the shadow march and the occlusion read
  between texels** (bilinear). Both are the stated model; do not "fix" one to match
  the other.
- **The shadow is cast from the centre of the last high texel**, because the surface
  between texels is bilinear. `--shadow` predicts from there.
- **The kick lands on the frame the hit arrives.** `--prime` expects rest the frame
  before and motion on it.
- **A Kick pressed before `InitGL` survives it.** InitGL used to clear it and the
  harness's `--pendulum` silently measured a lamp at rest; it now presses after.
- **The lamp's intensity is normalised at the lamp's nominal height**: a white
  Lambertian sheet directly under the lamp reads `Intensity`. The pendulum's rise
  changes `r`, not the reference.
- Every host parameter is 0..1; `SetParamInfo` clamps a STANDARD default before
  `SetParamRange` can widen it. Options are their element index.
- `patch`, `sample`, `input`, `output`, `filter`, `common`, `active`, `half`,
  `layout`, `flat` are GLSL reserved words. A shader that will not compile is
  `InitGL FAILED` in `rptest` and a black clip in the host; `Diag` logs which pass.
- Override `SetTextParameter` to return FF_SUCCESS for the About block or no host can
  instantiate the plugin at all.
- `repousse_core` is an OBJECT library, not STATIC: the plugin registers itself from a
  file-scope constructor nothing references by name.
- `ScopedFBOBinding` does not restore the viewport; every `ffglex::Scoped*` clears to 0
  on exit; `FFGLFBO::Release()` leaks the colour texture. `PassBuffer` and the order of
  `ProcessOpenGL` exist because of those three.
- Resolume's clock overflows a float (~499 million ms). `Clock` keeps it in double,
  frame-relative; nothing absolute reaches a shader.
- macOS build must be universal. Verify with `lipo`, never the build log.
- FFGL id is `RP01`, name `SW Repousse`.

## Not done yet
- Never loaded into Resolume. Not installed into Extra Effects. No release tag, not
  on the website; `StoatworksAbout.h` (with `guide=""`) and `ATTRIBUTIONS.md` are
  provisional hand copies.
- No OpenFX port, no factory presets.
- **The browser demo's CPU half is a port**: `Controls.cpp`, `Pendulum.cpp`, the blur
  weights and the pass order of `ProcessOpenGL` are a hand port in `demo/plugin.js`, and
  nothing checks it. Change any of those and change `demo/plugin.js` by hand. The five
  shaders themselves are checked (`demo/tools/check_shaders.py`). No audio reaches the
  page; the onset detector is not ported and only the Kick button swings the lamp.

## Browser demo

`demo/` is the page at **repousse-demo.stoatworks-labs.com**, deployed from
`wrangler.toml` with `cf-run npx wrangler deploy` and by `deploy.yml` on a push to
main — no build step; what is committed is what is served. The host is a Worker
route plus a proxied `AAAA 100::` record (the zone is at its custom-domain limit).
`demo/vendor/` is copied in by
`~/Projects/infrastructure/stoatworks-backend/resolume-demo/sync.sh repousse` and is not
a place to edit. The shader literals in `demo/plugin.js` must stay the plugin's:
`python3 demo/tools/check_shaders.py` (run by `tools/verify.sh`); after a change to
`source/Shaders.cpp`, re-splice them by script rather than by hand (see AGENTS.md,
*The browser demo*). Serve it locally with `python3 -m http.server` in `demo/`.

## Diagnostics

`source/Diag.{h,cpp}` — log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/repousse/repousse.YYYY-MM-DD.log
    %LOCALAPPDATA%\repousse\logs\                      (Windows)

`REPOUSSE_LOG_DIR` overrides the location. It records the GL strings, which pass
failed to compile if one did, and one line at frame 60 with the host's raw clock.
