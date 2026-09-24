/**
 * Repousse — browser demo.
 *
 * The picture hammered into a metal sheet from behind and lit by a lamp on a
 * swinging cord. Two physical facts make the look, from `source/Repousse.h`:
 * the punch sets the finest detail (a sheet can be pushed only as sharply as
 * the tool that pushes it, so detail narrower than the punch is never formed),
 * and metal is lit, not shaded — a point lamp at a real position with
 * inverse-square falloff, a GGX highlight with the measured F0 of the metal,
 * self-shadowing by a march over the height field, and a patina in the
 * recesses. The lamp hangs on a cord: a damped pendulum with the period its
 * length gives it.
 *
 * Like galvo and teletext, this plugin is **not only a shader**, and the two
 * halves of the page are not equally faithful:
 *
 *   The shaders are the plugin's. `VERTEX`, `HEIGHT`, `BLUR`, `MORPH` and
 *   `SHADE` below are `kVertex`, `kHeight`, `kBlur`, `kMorph` and `kShade`
 *   from `source/Shaders.cpp`, copied across unedited, `#version` line and
 *   all; the kit's `port()` swaps that line for ES 3.00 and adds the precision
 *   qualifiers, nothing else. `demo/tools/check_shaders.py` compares them
 *   character for character and `tools/verify.sh` runs it.
 *
 *   The CPU half is a PORT — of `Controls.cpp` (every conversion from the
 *   host's 0..1 to pixels, frame heights, metres and rad/s, the metal table,
 *   the morphology tap count), `Pendulum.cpp` (two planar pendulums, the
 *   large-angle equation, classical Runge-Kutta at a fixed 1/480 s substep,
 *   the elapsed time clamped to 0.5 s, the golden-angle kick direction), the
 *   blur weight table in `Repousse::uploadWeights`, and the pass sequence of
 *   `Repousse::ProcessOpenGL` (height, two blur axes, the eight morphology
 *   axes of an opening then a closing, the lit sheet). Function for function,
 *   in JavaScript doubles as the plugin integrates in C++ doubles. Nothing
 *   checks a port but a reader. `rptest --model`, `--pendulum`, `--punch`,
 *   `--lambert` and the rest check the C++ originals and have no idea this
 *   page exists.
 *
 * ------------------------------------------------------ what is NOT here
 *
 * **No audio.** The plugin declares an `Audio` FFT buffer (64 bins) and an
 * onset detector (`Audio.cpp`) that kicks the lamp on a hit. A browser page
 * has no host handing it FFT bins, so the detector is not ported and the
 * `Audio` input is not on the page: nothing kicks the lamp but the Kick
 * button. **The Kick button is under the canvas**, not in the inspector: the
 * kit has no control for an FF_TYPE_EVENT parameter. **The plugin's clock
 * vote** (`Clock.cpp`, which infers whether Resolume's clock is in ms or s)
 * never runs; the page's clock is declared seconds, as the harness declares
 * it. **The `Perturb` test hooks** are always 0 in a host and are held at 0
 * here. **The About block is absent**, as on every page in this suite.
 *
 * And what every page in this suite is not: this is the plugin's shaders and
 * a port of its C++, not the plugin. No Resolume, no composition, no FFGL,
 * and GLSL ES 3.00 in a browser rather than desktop GL 4.1 core.
 */

import { mountDemo } from './vendor/demo.js';
import { GLError, Program, PassBuffer, bindTexture } from './vendor/gl.js';

//---------------------------------------------------------------------------
// Shaders — verbatim from source/Shaders.cpp, including the plugin's own
// `#version 410 core` line. Do not edit here: `demo/tools/check_shaders.py`
// compares each literal with the C++ character for character, and the
// literals are spliced in by script from the C++ (see AGENTS.md, *The browser
// demo*). A `${` would be interpolated by the literal; the check refuses one.
//---------------------------------------------------------------------------

const VERTEX = `#version 410 core

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
`;

const HEIGHT = `#version 410 core

uniform sampler2D InputTexture;
uniform int Invert;
uniform float Depth;    //pixels of height for a white pixel

out vec4 fragColor;

void main()
{
	vec4 c  = texelFetch( InputTexture, ivec2( gl_FragCoord.xy ), 0 );
	float l = clamp( dot( c.rgb, vec3( 0.2126, 0.7152, 0.0722 ) ), 0.0, 1.0 );//Rec. 709 luma
	if( Invert != 0 )
		l = 1.0 - l;
	fragColor = vec4( l * Depth, 0.0, 0.0, 1.0 );
}
`;

const BLUR = `#version 410 core

uniform sampler2D Source;
uniform int Width;
uniform int Height;
uniform int DirX;
uniform int DirY;
uniform int Radius;
uniform float Weights[ 10 ];

out vec4 fragColor;

float at( ivec2 p )
{
	return texelFetch( Source, clamp( p, ivec2( 0 ), ivec2( Width - 1, Height - 1 ) ), 0 ).r;
}

void main()
{
	ivec2 p   = ivec2( gl_FragCoord.xy );
	ivec2 dir = ivec2( DirX, DirY );
	float sum = Weights[ 0 ] * at( p );
	for( int k = 1; k <= Radius; ++k )
		sum += Weights[ k ] * ( at( p - k * dir ) + at( p + k * dir ) );
	fragColor = vec4( sum, 0.0, 0.0, 1.0 );
}
`;

const MORPH = `#version 410 core

uniform sampler2D Source;
uniform int Width;
uniform int Height;
uniform int DirX;
uniform int DirY;
uniform int Taps;
uniform int Erode;      //1 erode, 0 dilate
uniform float InvTwoR;  //1 / 2R, in 1 / pixels
uniform int Flat;       //negative control: the punch a flat disc

out vec4 fragColor;

float at( ivec2 p )
{
	return texelFetch( Source, clamp( p, ivec2( 0 ), ivec2( Width - 1, Height - 1 ) ), 0 ).r;
}

void main()
{
	ivec2 p    = ivec2( gl_FragCoord.xy );
	ivec2 dir  = ivec2( DirX, DirY );
	float best = at( p );
	for( int k = 1; k <= Taps; ++k )
	{
		float b = Flat != 0 ? 0.0 : float( k * k ) * InvTwoR;
		float a = at( p - k * dir );
		float c = at( p + k * dir );
		if( Erode != 0 )
			best = min( best, min( a, c ) + b );
		else
			best = max( best, max( a, c ) - b );
	}
	fragColor = vec4( best, 0.0, 0.0, 1.0 );
}
`;

const SHADE = `#version 410 core

uniform sampler2D Height;
uniform sampler2D InputTexture;
uniform int HWidth;          //the height buffer's size
uniform int HHeight;
uniform int VpX;
uniform int VpY;
uniform int VpW;
uniform int VpH;

uniform float Depth;         //the tallest height there can be, pixels
uniform vec3 Lamp;           //pixels: x, y across the picture, z above it
uniform float LampRef;       //the lamp's nominal height, pixels
uniform float Intensity;
uniform float Ambient;
uniform float Shadows;       //0 none .. 1 full
uniform float Alpha;         //GGX roughness
uniform float Patina;
uniform vec3 F0;             //the specular's normal-incidence reflectance
uniform vec3 Tint;           //the metal's colour for its diffuse and ambient
uniform float DiffuseWeight;
uniform int ColourMode;      //0 metal, 1 clip
uniform int Steps;           //shadow march samples
uniform float Bias;          //shadow bias, pixels of height
uniform int AoCount;         //radii
uniform float AoRadius[ 4 ];
uniform int ShowHeight;
uniform float MixAmount;
uniform int Perturb;

out vec4 fragColor;

const float kPi = 3.14159265358979323846;

float heightAt( ivec2 p )
{
	return texelFetch( Height, clamp( p, ivec2( 0 ), ivec2( HWidth - 1, HHeight - 1 ) ), 0 ).r;
}

//The surface between samples: bilinear, clamped at the frame's edge.
float heightAtPx( vec2 q )
{
	return texture( Height, q / vec2( HWidth, HHeight ) ).r;
}

void main()
{
	int X   = int( gl_FragCoord.x ) - VpX;
	int Y   = int( gl_FragCoord.y ) - VpY;
	ivec2 p = ivec2( ( ( 2 * X + 1 ) * HWidth ) / ( 2 * VpW ), ( ( 2 * Y + 1 ) * HHeight ) / ( 2 * VpH ) );

	float h   = heightAt( p );
	vec4 clip = texelFetch( InputTexture, p, 0 );

	if( ShowHeight != 0 )
	{
		float g   = Depth > 0.0 ? h / Depth : 0.0;
		fragColor = vec4( g, g, g, 1.0 );
		return;
	}

	//--- the normal: a central difference, one-sided on the frame's edge
	//columns and rows (a clamped central difference there would halve the
	//slope).
	ivec2 lo  = max( p - ivec2( 1 ), ivec2( 0 ) );
	ivec2 hi  = min( p + ivec2( 1 ), ivec2( HWidth - 1, HHeight - 1 ) );
	vec2 span = vec2( max( hi - lo, ivec2( 1 ) ) );
	vec2 g    = vec2( heightAt( ivec2( hi.x, p.y ) ) - heightAt( ivec2( lo.x, p.y ) ),
	                  heightAt( ivec2( p.x, hi.y ) ) - heightAt( ivec2( p.x, lo.y ) ) ) / span;
	vec3 n    = normalize( vec3( -g, 1.0 ) );

	//--- the lamp.
	vec3 P    = vec3( vec2( p ) + 0.5, h );
	vec3 d    = Lamp - P;
	float r2  = dot( d, d );
	float r   = sqrt( r2 );
	vec3 l    = d / r;
	float ndl = max( dot( n, l ), 0.0 );
	float E   = ( Perturb & 1 ) != 0 ? Intensity : Intensity * ( LampRef * LampRef ) / r2;

	//--- visibility: march toward the lamp until the ray clears the tallest
	//relief there can be. Steps samples over that span, whatever its length.
	float vis = 1.0;
	if( ( Perturb & 2 ) == 0 && Shadows > 0.0 && ndl > 0.0 && Lamp.z > h )
	{
		float fD = clamp( ( Depth - h ) / ( Lamp.z - h ), 0.0, 1.0 );
		for( int i = 0; i < Steps; ++i )
		{
			float f  = fD * float( i + 1 ) / float( Steps );
			vec2 q   = P.xy + f * d.xy;
			float z  = h + f * d.z;
			if( heightAtPx( q ) > z + Bias )
			{
				vis = 0.0;
				break;
			}
		}
	}
	vis = 1.0 - Shadows * ( 1.0 - vis );

	//--- the patina's occlusion: the horizon in eight directions.
	float ao = 1.0;
	if( Patina > 0.0 )
	{
		float occ = 0.0;
		for( int k = 0; k < 8; ++k )
		{
			float ang = float( k ) * kPi * 0.25;
			vec2 dir  = vec2( cos( ang ), sin( ang ) );
			float mt  = 0.0;
			for( int j = 0; j < AoCount; ++j )
				mt = max( mt, ( heightAtPx( P.xy + dir * AoRadius[ j ] ) - h ) / AoRadius[ j ] );
			occ += mt / sqrt( 1.0 + mt * mt );
		}
		ao = 1.0 - occ * 0.125;
	}
	float recess = mix( 1.0, ao, Patina );

	//--- the BRDF. The viewer looks straight down.
	vec3 v    = vec3( 0.0, 0.0, 1.0 );
	vec3 hv   = normalize( l + v );
	float ndh = ( Perturb & 4 ) != 0 ? ndl : max( dot( n, hv ), 0.0 );
	float ndv = max( n.z, 1e-4 );
	float vdh = max( dot( v, hv ), 0.0 );
	float a2  = Alpha * Alpha;
	float dd  = ndh * ndh * ( a2 - 1.0 ) + 1.0;
	float D   = a2 / ( kPi * dd * dd );
	float V   = 0.5 / ( ndl * sqrt( ndv * ndv * ( 1.0 - a2 ) + a2 ) + ndv * sqrt( ndl * ndl * ( 1.0 - a2 ) + a2 ) );
	vec3 f0   = ( Perturb & 8 ) != 0 ? vec3( 0.5 ) : F0;
	float t   = 1.0 - vdh;
	float t2  = t * t;
	vec3 F    = f0 + ( 1.0 - f0 ) * t2 * t2 * t;

	vec3 albedo = ColourMode == 0 ? Tint : clip.rgb;
	vec3 spec   = ( Perturb & 8192 ) != 0 ? vec3( 0.0 ) : kPi * E * ndl * D * V * F;
	vec3 diff   = ( Perturb & 16384 ) != 0 ? vec3( 0.0 ) : E * ndl * DiffuseWeight * albedo * recess;
	vec3 amb    = Ambient * albedo * recess;
	vec3 colour = ( diff + spec ) * vis + amb;

	vec4 sheet = vec4( colour, clip.a );
	if( MixAmount >= 1.0 )
	{
		fragColor = sheet;
		return;
	}
	fragColor = mix( clip, sheet, MixAmount );
}
`;

//---------------------------------------------------------------------------
// Controls.cpp, ported. Every host parameter is 0..1 (SetParamInfo clamps a
// STANDARD default into 0..1 before a range can be attached); an option is its
// element index. Every mapping is linear or a clamp, so Roughness 0.5 is an
// alpha of 0.5 and Depth 0.25 is 16 px, exactly as in the plugin.
//---------------------------------------------------------------------------

const K_GRAVITY = 9.80665; // m/s^2
const K_METRES_PER_FRAME_HEIGHT = 1.0;
const K_MIN_ALPHA = 1.0 / 64.0;
const K_MIN_LAMP_HEIGHT = 0.02; // frame heights
const K_MIN_CORD = 0.05; // metres
const K_MIN_DAMPING = 0.005;
const K_MAX_MORPH_TAPS = 64;
const K_MAX_BLUR_RADIUS = 9;
const K_SHADOW_STEPS = 32;
const K_SHADOW_BIAS = 0.01;
const K_METAL_DIFFUSE = 0.2;
const K_PAINT_F0 = 0.04;
const K_AO_RADII = 4;
const K_AO_RADIUS_PX = new Float32Array([2.0, 5.0, 9.0, 14.0]);

const METAL_NAMES = ['Copper', 'Brass', 'Silver', 'Gold', 'Steel'];
const COLOUR_NAMES = ['Metal', 'Clip'];

// Normal-incidence reflectance, linear sRGB: the table in Controls.cpp, which
// tools/f0.py derives from published n,k and checks in verify.sh.
const METAL_F0 = [
  [0.932, 0.623, 0.522], // copper: Johnson & Christy 1972
  [0.910, 0.778, 0.423], // brass: Querry 1985, Cu70Zn30 (C260)
  [0.989, 0.984, 0.977], // silver: Johnson & Christy 1972
  [1.000, 0.728, 0.365], // gold: Johnson & Christy 1972, red clamped from 1.038
  [0.530, 0.513, 0.494], // steel: iron, Johnson & Christy 1974
];

const unit = (value) => Math.min(1.0, Math.max(0.0, value));

// std::lround, then clamp. Math.round agrees with lround on every non-negative
// half, which is all a 0..count-1 option can hold.
const optionIndex = (value, count) => Math.min(count - 1, Math.max(0, Math.round(value)));

const depthPxFromParam = (v) => 64.0 * unit(v);
const punchRadiusPxFromParam = (v) => 16.0 * unit(v);
const smoothSigmaFromParam = (v) => 3.0 * unit(v);
const roughnessAlphaFromParam = (v) => Math.max(K_MIN_ALPHA, unit(v));
const lampHeightFromParam = (v) => Math.max(K_MIN_LAMP_HEIGHT, 2.0 * unit(v));
const intensityFromParam = (v) => 2.0 * unit(v);
const cordLengthFromParam = (v) => Math.max(K_MIN_CORD, 2.0 * unit(v));
const dampingRatioFromParam = (v) => Math.max(K_MIN_DAMPING, unit(v));
const swingRateFromParam = (v) => 3.0 * unit(v);

// How many taps each side a 1-D morphology pass takes: floor( R ), 0 under a
// pixel, at most kMaxMorphTaps.
function morphTaps(radiusPx) {
  if (radiusPx < 1.0) return 0;
  return Math.min(K_MAX_MORPH_TAPS, Math.floor(radiusPx));
}

const metalF0 = (metal) => METAL_F0[Math.min(METAL_F0.length - 1, Math.max(0, metal))];

//---------------------------------------------------------------------------
// Pendulum.cpp, ported. Two planar pendulums, one in x and one in y, each the
// full large-angle equation
//
//     theta'' = -( g / L ) sin theta - 2 zeta omega0 theta'
//
// integrated by classical Runge-Kutta at a fixed 1/480 s substep with the
// elapsed TIME clamped to 0.5 s (not the step count), so a stalled tab cannot
// spend a second in one update. Kicks step round the golden angle from one to
// the next, the first along +x. JavaScript numbers are IEEE doubles, which is
// what the plugin integrates in.
//---------------------------------------------------------------------------

const K_SUB_STEP = 1.0 / 480.0;
const K_MAX_FRAME = 0.5; // seconds integrated per update at most
const K_GOLDEN_ANGLE = 2.399963229728653;

class Pendulum {
  constructor() {
    this.cordMetres = 0.5;
    this.dampingRatio = 0.05;
    this.theta = [0.0, 0.0];
    this.omega = [0.0, 0.0];
    this.kicks = 0;
  }

  configure({ cordMetres, dampingRatio }) {
    this.cordMetres = cordMetres;
    this.dampingRatio = dampingRatio;
  }

  // Add angular velocity `rate` rad/s along `direction` (radians in the
  // picture plane, 0 = +x).
  kick(rate, direction) {
    this.omega[0] += rate * Math.cos(direction);
    this.omega[1] += rate * Math.sin(direction);
  }

  // The next kick's direction, and step it round the golden angle.
  nextDirection() {
    const direction = K_GOLDEN_ANGLE * this.kicks;
    this.kicks += 1;
    return direction;
  }

  reset() {
    this.theta[0] = this.theta[1] = 0.0;
    this.omega[0] = this.omega[1] = 0.0;
    this.kicks = 0;
  }

  // Advance by `dt` seconds (clamped inside). The plugin's `perturb` bits are
  // always 0 in a host; the detuned and linearised variants are not ported.
  update(dt) {
    const g = K_GRAVITY;
    const L = Math.max(K_MIN_CORD, this.cordMetres);
    const omega0 = Math.sqrt(g / L);
    const zeta = this.dampingRatio;

    const bounded = Math.min(K_MAX_FRAME, Math.max(0.0, dt));
    const steps = Math.ceil(bounded / K_SUB_STEP);
    const h = steps > 0 ? bounded / steps : 0.0;

    const accel = (th, om) => -omega0 * omega0 * Math.sin(th) - 2.0 * zeta * omega0 * om;

    for (let s = 0; s < steps; s += 1) {
      for (let i = 0; i < 2; i += 1) {
        const th = this.theta[i];
        const om = this.omega[i];
        const k1t = om;
        const k1w = accel(th, om);
        const k2t = om + 0.5 * h * k1w;
        const k2w = accel(th + 0.5 * h * k1t, om + 0.5 * h * k1w);
        const k3t = om + 0.5 * h * k2w;
        const k3w = accel(th + 0.5 * h * k2t, om + 0.5 * h * k2w);
        const k4t = om + h * k3w;
        const k4w = accel(th + h * k3t, om + h * k3w);
        this.theta[i] = th + (h / 6.0) * (k1t + 2.0 * k2t + 2.0 * k3t + k4t);
        this.omega[i] = om + (h / 6.0) * (k1w + 2.0 * k2w + 2.0 * k3w + k4w);
      }
    }
  }

  // The lamp's horizontal displacement from the rest point, in metres.
  offsetX() { return this.cordMetres * Math.sin(this.theta[0]); }
  offsetY() { return this.cordMetres * Math.sin(this.theta[1]); }
  // How far the lamp has risen from its lowest point: L ( 1 - cos theta ) per axis.
  rise() { return this.cordMetres * (2.0 - Math.cos(this.theta[0]) - Math.cos(this.theta[1])); }
}

//---------------------------------------------------------------------------
// The lamp's state across frames — the only state the plugin carries between
// frames apart from the onset detector, which has nothing to listen to here.
// Module-level so the Kick button under the canvas can reach it.
//---------------------------------------------------------------------------

const lamp = {
  pendulum: new Pendulum(),
  kickPending: false, // Repousse::kickPending: a press, delivered on the next frame
  kickCount: 0,
  ticked: false,
  lastNow: 0.0,
  px: [0, 0, 0], // the position the last frame used, in height-buffer pixels
  cordMetres: 0.5,
};

//---------------------------------------------------------------------------
// The chain — Repousse::ProcessOpenGL, in the plugin's order.
//---------------------------------------------------------------------------

function createRenderer(gl, quad) {
  // The plugin samples its R32F height buffers with linear filtering (the
  // shadow march and the occlusion read between texels). In WebGL2 a float
  // texture with a LINEAR filter is incomplete without this extension and
  // reads as zero — a plausible wrong picture, which is exactly what this
  // page must not show.
  if (!gl.getExtension('OES_texture_float_linear')) {
    throw new GLError('OES_texture_float_linear is missing. The plugin samples its float height buffer with linear filtering for the shadow march and the patina, and without it those reads would silently return zero.');
  }

  const heightShader = new Program(gl, VERTEX, HEIGHT, 'height');
  const blurShader = new Program(gl, VERTEX, BLUR, 'blur');
  const morphShader = new Program(gl, VERTEX, MORPH, 'morph');
  const shadeShader = new Program(gl, VERTEX, SHADE, 'shade');

  // Linear on every buffer, as the plugin's: the normal reads texels exactly
  // with texelFetch (filtering does not apply), the shadow march and the
  // occlusion read the surface between texels. R32F, never half: a half-float
  // store would put 2^-11 steps into the sheet.
  const height = new PassBuffer(gl, { filter: 'linear' });
  const pingA = new PassBuffer(gl, { filter: 'linear' });
  const pingB = new PassBuffer(gl, { filter: 'linear' });

  // Repousse::uploadWeights: Weights[ k ] at distance k, normalised over
  // -radius..radius in double, radius ceil( 3 sigma ) capped at 9.
  const weights = new Float32Array(10);
  let radius = 0;
  let weightsSigma = -1.0;
  function uploadWeights(sigma) {
    if (sigma === weightsSigma) return;
    weightsSigma = sigma;
    radius = sigma > 0.0 ? Math.min(K_MAX_BLUR_RADIUS, Math.ceil(3.0 * sigma)) : 0;
    const w = new Array(10).fill(0.0);
    let total = 0.0;
    for (let k = 0; k <= radius; k += 1) {
      w[k] = radius === 0 ? 1.0 : Math.exp((-0.5 * k * k) / (sigma * sigma));
      total += k === 0 ? w[k] : 2.0 * w[k];
    }
    for (let k = 0; k < 10; k += 1) weights[k] = k <= radius ? w[k] / total : 0.0;
  }

  function heightPass(picture, depthPx, invert) {
    height.bind();
    heightShader.use();
    bindTexture(gl, 0, picture.texture);
    heightShader.setSampler('InputTexture', 0);
    heightShader.setInt('Invert', invert ? 1 : 0);
    heightShader.set('Depth', depthPx);
    quad.draw();
  }

  function blurPass(from, to, dirX, dirY) {
    to.bind();
    blurShader.use();
    bindTexture(gl, 0, from.texture);
    blurShader.setSampler('Source', 0);
    blurShader.setInt('Width', from.width);
    blurShader.setInt('Height', from.height);
    blurShader.setInt('DirX', dirX);
    blurShader.setInt('DirY', dirY);
    blurShader.setInt('Radius', radius);
    blurShader.setArray('Weights', weights, 1);
    quad.draw();
  }

  function morphPass(from, to, dirX, dirY, erode, taps, invTwoR) {
    to.bind();
    morphShader.use();
    bindTexture(gl, 0, from.texture);
    morphShader.setSampler('Source', 0);
    morphShader.setInt('Width', from.width);
    morphShader.setInt('Height', from.height);
    morphShader.setInt('DirX', dirX);
    morphShader.setInt('DirY', dirY);
    morphShader.setInt('Taps', taps);
    morphShader.setInt('Erode', erode ? 1 : 0);
    morphShader.set('InvTwoR', invTwoR);
    morphShader.setInt('Flat', 0); // perturb::kFlatPunch, always 0 in a host
    quad.draw();
  }

  return {
    render({ input, params, width: vpW, height: vpH, time }) {
      const p = (id) => params.get(id);
      const picture = input;
      const width = picture.width;
      const heightPx = picture.height;

      //------------------------------------------------------------------
      // The lamp. CPU state, carried across a resize untouched. The page's
      // clock is the kit's `time`, seconds since the page started, paused
      // by Pause and stepped by Step; a Restart sends it to 0, which reads
      // as a jump and integrates no time, as the plugin's clock does.
      //------------------------------------------------------------------
      const now = time;
      const dt = lamp.ticked ? Math.max(0.0, now - lamp.lastNow) : 0.0;
      lamp.lastNow = now;
      lamp.ticked = true;

      const pendulum = lamp.pendulum;
      lamp.cordMetres = cordLengthFromParam(p('cord'));
      pendulum.configure({
        cordMetres: lamp.cordMetres,
        dampingRatio: dampingRatioFromParam(p('damping')),
      });
      const swingRate = swingRateFromParam(p('swing'));
      if (lamp.kickPending) {
        pendulum.kick(swingRate, pendulum.nextDirection());
        lamp.kickPending = false;
        lamp.kickCount += 1;
      }
      // An onset from the audio would kick here, scaled by its strength.
      // There is no audio on this page.
      pendulum.update(dt);

      //------------------------------------------------------------------
      // Units. Every conversion in double, handed over as float uniforms.
      // One frame height is one metre.
      //------------------------------------------------------------------
      const depth = depthPxFromParam(p('depth'));
      const punchRadius = punchRadiusPxFromParam(p('punch'));
      const taps = morphTaps(punchRadius);
      const morphing = taps > 0;
      uploadWeights(smoothSigmaFromParam(p('smooth')));
      const blurring = radius > 0;
      const working = morphing || blurring;
      const pxPerM = heightPx / K_METRES_PER_FRAME_HEIGHT;
      const lampRef = lampHeightFromParam(p('lampHeight')) * heightPx;
      lamp.px[0] = p('lampX') * width + pendulum.offsetX() * pxPerM;
      lamp.px[1] = p('lampY') * heightPx + pendulum.offsetY() * pxPerM;
      lamp.px[2] = lampRef + pendulum.rise() * pxPerM;

      const metal = optionIndex(p('metal'), METAL_NAMES.length);
      const colourMode = optionIndex(p('colour'), COLOUR_NAMES.length);
      const f0 = metalF0(metal);

      //------------------------------------------------------------------
      // Buffers, at the picture's size: the host hands the plugin an input
      // the size of the output, and the shade pass maps the viewport onto
      // the height buffer as the plugin's does.
      //------------------------------------------------------------------
      height.ensure(width, heightPx, gl.R32F);
      if (working) {
        pingA.ensure(width, heightPx, gl.R32F);
        pingB.ensure(width, heightPx, gl.R32F);
      }

      gl.disable(gl.BLEND);
      heightPass(picture, depth, p('invert') > 0.5);
      let sheet = height;
      const next = (from) => (from === pingA ? pingB : pingA);
      if (blurring) {
        blurPass(sheet, pingA, 1, 0);
        blurPass(pingA, pingB, 0, 1);
        sheet = pingB;
      }
      if (morphing) {
        const invTwoR = 1.0 / (2.0 * punchRadius);
        // The opening: erode, then dilate. Then the closing: dilate, then
        // erode. Each in x then y; the paraboloid is exactly separable.
        const erodes = [true, true, false, false, false, false, true, true];
        for (let pass = 0; pass < 8; pass += 1) {
          const to = next(sheet);
          morphPass(sheet, to, pass % 2 === 0 ? 1 : 0, pass % 2 === 0 ? 0 : 1, erodes[pass], taps, invTwoR);
          sheet = to;
        }
      }

      //------------------------------------------------------------------
      // The lit sheet, to the canvas: the host's framebuffer and viewport.
      //------------------------------------------------------------------
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, vpW, vpH);
      shadeShader.use();
      bindTexture(gl, 0, sheet.texture);
      bindTexture(gl, 1, picture.texture);
      shadeShader.setSampler('Height', 0);
      shadeShader.setSampler('InputTexture', 1);
      shadeShader.setInt('HWidth', sheet.width);
      shadeShader.setInt('HHeight', sheet.height);
      shadeShader.setInt('VpX', 0);
      shadeShader.setInt('VpY', 0);
      shadeShader.setInt('VpW', vpW);
      shadeShader.setInt('VpH', vpH);

      shadeShader.set('Depth', depth);
      shadeShader.set('Lamp', lamp.px[0], lamp.px[1], lamp.px[2]);
      shadeShader.set('LampRef', lampRef);
      shadeShader.set('Intensity', intensityFromParam(p('intensity')));
      shadeShader.set('Ambient', p('ambient'));
      shadeShader.set('Shadows', p('shadows'));
      shadeShader.set('Alpha', roughnessAlphaFromParam(p('roughness')));
      shadeShader.set('Patina', p('patina'));
      if (colourMode === 0) {
        shadeShader.set('F0', f0[0], f0[1], f0[2]);
        shadeShader.set('DiffuseWeight', K_METAL_DIFFUSE);
      } else {
        shadeShader.set('F0', K_PAINT_F0, K_PAINT_F0, K_PAINT_F0);
        shadeShader.set('DiffuseWeight', 1.0);
      }
      shadeShader.set('Tint', f0[0], f0[1], f0[2]);
      shadeShader.setInt('ColourMode', colourMode);
      shadeShader.setInt('Steps', K_SHADOW_STEPS);
      shadeShader.set('Bias', K_SHADOW_BIAS);
      shadeShader.setInt('AoCount', K_AO_RADII);
      shadeShader.setArray('AoRadius', K_AO_RADIUS_PX, 1);
      shadeShader.setInt('ShowHeight', p('showHeight') > 0.5 ? 1 : 0);
      shadeShader.set('MixAmount', p('mix'));
      shadeShader.setInt('Perturb', 0);
      quad.draw();
    },
  };
}

//---------------------------------------------------------------------------
// The parameters, as the constructor declares them: same names, same groups,
// same order, same defaults, same element lists. The two the kit cannot draw
// are said: Kick (FF_TYPE_EVENT) is the button under the canvas, and Audio
// (an FFT buffer) has nothing to carry it here.
//---------------------------------------------------------------------------

const std = (id, name, def, group, extra = {}) => ({ id, name, type: 'standard', default: def, group, ...extra });
const opt = (id, name, elements, def, group, hint) => ({ id, name, type: 'option', elements, default: def, group, hint });
const bool = (id, name, def, group, hint) => ({ id, name, type: 'boolean', default: def, group, hint });

const periodSeconds = (cordMetres) => 2.0 * Math.PI * Math.sqrt(cordMetres / K_GRAVITY);

const demo = mountDemo({
  name: 'Repousse',
  pluginId: 'RP01',
  kind: 'effect',
  tagline:
    'The picture hammered into a metal sheet and lit by a lamp on a swinging cord. The brightness is read as relief and worked with a punch — detail narrower than the punch is never formed, not blurred away — then lit rather than shaded: a point lamp with inverse-square falloff, a microfacet highlight with the measured reflectance of copper, brass, silver, gold or steel, self-shadowing off every ridge, and a patina in the recesses. The lamp is a pendulum with the period its cord gives it; press Kick and the highlights slide across the relief as it swings. The shaders here are the plugin’s own; the controls’ units and the pendulum are a port of its C++.',
  repo: 'https://github.com/stoatworks-labs/repousse',
  page: 'https://stoatworks-labs.com/software/repousse/',

  // The stock sentence says "same maths", which is only half true here: the
  // five shaders are the plugin's, the pendulum and the unit conversions
  // between them are a port.
  blurb:
    'It is Repousse’s own GLSL — the height, blur, punch and shade passes — ported from the repository to WebGL2, with the CPU half (every control’s conversion to pixels, metres and rad/s, and the lamp’s pendulum, integrated by Runge-Kutta at 1/480 s) ported to JavaScript by hand; nothing checks that port but a reader. It runs on generated clips in this page, with the plugin’s own parameters and no install. There is no audio here, so only the Kick button swings the lamp.',

  // The height buffers are R32F, as the plugin's: a half-float or byte store
  // would put steps into the sheet and light every riser.
  needFloat: true,

  params: [
    std('depth', 'Depth', 0.25, 'Sheet', {
      display: (v) => `${depthPxFromParam(v).toFixed(1)} px for a white pixel`,
      hint: 'The height the brightest pixel is raised, 0–64 px. The relief is a thing done to the picture’s pixels, so it is measured in them: the same clip at 4K is hammered finer, not deeper.',
    }),
    bool('invert', 'Invert', 0, 'Sheet',
      'Read dark as high instead of bright.'),
    std('punch', 'Punch Radius', 0.25, 'Sheet', {
      display: (v) => {
        const r = punchRadiusPxFromParam(v);
        const taps = morphTaps(r);
        return `${r.toFixed(1)} px — ${taps === 0 ? 'no morphology' : `${taps} tap${taps === 1 ? '' : 's'} a side`}`;
      },
      hint: 'The tip radius of the punch, 0–16 px. The relief is opened and closed by a paraboloid tip on a shank of that half-width, so detail narrower than the punch is never formed and anything wider keeps its full height. Under one pixel there is nothing to do.',
    }),
    std('smooth', 'Smooth', 1.0 / 3.0, 'Sheet', {
      display: (v) => `σ ${smoothSigmaFromParam(v).toFixed(2)} px`,
      hint: 'A Gaussian over the height before the punch, σ 0–3 px. Not detail control — that is the punch — but the answer to 8-bit footage: a one-code step in a gentle gradient is a 0.06 px riser, and a raking lamp lights every riser as a terrace.',
    }),
    opt('metal', 'Metal', METAL_NAMES, 0, 'Sheet',
      'Which metal’s normal-incidence reflectance the highlight uses, from published complex refractive indices (tools/f0.py in the repository). Gold’s red is clamped to 1.'),
    std('roughness', 'Roughness', 0.5, 'Sheet', {
      display: (v) => `GGX α ${roughnessAlphaFromParam(v).toFixed(3)}`,
      hint: 'The microfacet roughness, straight through as the GGX alpha with a floor of 1/64. At 0.5 the pool under the lamp peaks at 0.86, unclipped.',
    }),
    std('patina', 'Patina', 0.6, 'Sheet',
      { hint: 'How much the recesses darken: the mean sine of the horizon over eight directions, applied to the diffuse and the ambient, never the highlight.' }),
    opt('colour', 'Colour', COLOUR_NAMES, 0, 'Sheet',
      'Metal: the metal’s own colour for the diffuse and ambient, with a metal’s small diffuse share. Clip: the clip as a painted sheet — its colours as the albedo, a dielectric F0 of 0.04 and a full diffuse term.'),

    std('lampX', 'Lamp X', 0.3, 'Lamp', {
      display: (v) => `${v.toFixed(2)} of the frame’s width`,
      hint: 'Where the lamp hangs at rest, as a fraction of the frame. The pendulum swings it from here.',
    }),
    std('lampY', 'Lamp Y', 0.75, 'Lamp', {
      display: (v) => `${v.toFixed(2)} of the frame’s height`,
      hint: 'Where the lamp hangs at rest, as a fraction of the frame, from the bottom.',
    }),
    std('lampHeight', 'Lamp Height', 0.25, 'Lamp', {
      display: (v) => `${lampHeightFromParam(v).toFixed(2)} frame heights`,
      hint: 'The lamp’s nominal height above the sheet, 0.02–2 frame heights. Low is a raking light that throws long shadows; high is even.',
    }),
    std('intensity', 'Intensity', 0.45, 'Lamp', {
      display: (v) => intensityFromParam(v).toFixed(2),
      hint: 'The irradiance directly under the lamp at its nominal height, 0–2: at 1 a white Lambertian sheet there reads white. Inverse-square from there; the pendulum’s rise changes the distance, not the reference.',
    }),
    std('ambient', 'Ambient', 0.12, 'Lamp',
      { hint: 'Light from nowhere in particular, darkened by the patina in the recesses.' }),
    std('shadows', 'Shadows', 1.0, 'Lamp',
      { hint: 'How much of the self-shadowing to keep: a march of 32 samples along the ray to the lamp over the height field. 0 is none, 1 is full.' }),

    std('cord', 'Cord Length', 0.25, 'Pendulum', {
      display: (v) => `${cordLengthFromParam(v).toFixed(2)} m — period ${periodSeconds(cordLengthFromParam(v)).toFixed(2)} s`,
      hint: 'The lamp’s cord, 0.05–2 m. One frame height is one metre, so the period 2π√(L/g) is 0.45–2.8 s and the distance the lamp is seen to swing agrees with it.',
    }),
    std('damping', 'Damping', 0.05, 'Pendulum', {
      display: (v) => `ζ ${dampingRatioFromParam(v).toFixed(3)} — Q ${(1.0 / (2.0 * dampingRatioFromParam(v))).toFixed(1)}`,
      hint: 'The damping ratio: how many swings a kick is worth (Q = 1/2ζ). Floor 0.005.',
    }),
    std('swing', 'Swing', 0.4, 'Pendulum', {
      display: (v) => `${swingRateFromParam(v).toFixed(2)} rad/s per full kick`,
      hint: 'The angular velocity one full-strength kick adds, 0–3 rad/s. A hard kick swings visibly slower than a soft one, as a real lamp does. The Kick button is under the picture; the plugin’s Audio input, which would kick it on an onset, has nothing to carry it in a browser.',
    }),

    std('mix', 'Mix', 1.0, 'Output',
      { hint: 'The lit sheet over the clip.' }),
    bool('showHeight', 'Show Height', 0, 'Output',
      'The worked relief as grey, for setting the punch. What the lamp sees.'),
  ],

  // Bold shapes and a low lamp are what the sheet shows best; the geometry
  // card makes the punch readable and the ramps show what Smooth is for.
  sources: ['scene', 'grid', 'bars', 'ramp', 'spot', 'detail', 'alpha'],

  // The plugin ships no factory presets. These are the page's own, expressed
  // entirely in the plugin's parameters and reachable with the controls.
  presets: {
    'Silver under a high lamp': { metal: 2, lampHeight: 0.6, roughness: 0.3, patina: 0.3 },
    'Gold, coarse punch': { metal: 3, punch: 0.75, depth: 0.5 },
    'Raking steel': { metal: 4, lampHeight: 0.06, intensity: 0.7, shadows: 1.0 },
    'Painted sheet': { colour: 1, patina: 0.4 },
    'Long cord, light damping': { cord: 1.0, damping: 0.01, swing: 0.6 },
    'The relief alone': { showHeight: 1 },
  },

  differences: [
    'The CPU half of this plugin is a PORT, not the plugin’s own code. Controls.cpp (every conversion from the host’s 0..1 to pixels, frame heights, metres and rad/s, the metal table and the punch’s tap count), Pendulum.cpp (two planar large-angle pendulums, classical Runge-Kutta at a fixed 1/480 s substep, the elapsed time clamped to 0.5 s, kicks stepping round the golden angle), the blur weight table and the pass order of Repousse::ProcessOpenGL are ported here function for function, in JavaScript doubles as the plugin’s C++ doubles. Nothing checks a port but a reader; the repository’s rptest checks the C++ against the model’s own period, decay and ridge law and has never heard of this page.',
    'The GPU half is not a port. The height, blur, morphology and shade passes and the shared vertex shader are the plugin’s own GLSL, and demo/tools/check_shaders.py fails the repository’s verify script if a character of any of the five drifts. The height buffers are R32F with linear sampling, as the plugin’s, which is why the page needs EXT_color_buffer_float and OES_texture_float_linear and refuses to start without them.',
    'There is no audio on this page. The plugin declares an Audio FFT input (64 bins, of which the onset detector listens to the lowest eight) and kicks the lamp on an onset; a browser has no host handing it bins, so the detector in Audio.cpp is not ported and the Audio input is not shown. Only the Kick button swings the lamp. In embed mode there is no button, so the lamp hangs still.',
    'Kick is an FF_TYPE_EVENT parameter in the plugin. The kit has no control for an event, so it is the button under the picture rather than a row in the inspector. As in the plugin, a press is delivered on the next rendered frame; while the page is paused the kick is taken and the lamp moves when time next advances.',
    'The pendulum runs on the page’s clock — the kit’s time, seconds since the page started, paused by Pause and stepped by Step — with its unit declared as seconds, as the repository’s harness declares it. The plugin’s vote on whether Resolume’s clock is in milliseconds or seconds (Clock.cpp) never runs here. Restart sends the clock to zero, which the port reads as a jump and integrates no time, as the plugin does; it does not put the lamp back to rest, which the plugin does not either.',
    'The plugin’s Perturb test hooks (the flat punch, the linear pendulum, the detuned gravity and damping, the isolations) are held at 0, as they are in a host.',
    'The plugin’s proof — the lamp’s law on planes, the highlight at the half-vector point, each metal’s F0, the shadow behind a step, the punch’s ridge law, the period and decay read from the picture — is an offline harness in the repository. Nothing on this page measures anything; the line under the picture reports where the ported pendulum put the lamp.',
  ],

  createRenderer,
});

//---------------------------------------------------------------------------
// Under the canvas: the Kick button — the plugin's own FF_TYPE_EVENT control,
// which the kit's inspector cannot draw — and a line reporting where the
// ported pendulum put the lamp. Skipped in embed mode, where there is no
// reader and no button.
//---------------------------------------------------------------------------
if (demo && !new URLSearchParams(window.location.search).has('embed')) {
  const stage = document.querySelector('.stage');
  if (stage) {
    const row = document.createElement('div');
    row.className = 'transport';
    const kick = document.createElement('button');
    kick.type = 'button';
    kick.className = 'btn';
    kick.textContent = 'Kick';
    kick.title = 'The plugin’s Kick event: one push of the lamp at the Swing rate, in the next golden-angle direction.';
    kick.addEventListener('click', () => {
      // Repousse::SetFloatParameter: an event's press sets kickPending; the
      // next ProcessOpenGL delivers it. A paused page needs that frame asked
      // for.
      lamp.kickPending = true;
      if (!demo.state.playing) demo.redraw();
    });
    const label = document.createElement('span');
    label.className = 'transport__field';
    label.textContent = 'Kick — the plugin’s event control, a button here because the inspector has no event row. No audio reaches this page.';
    row.append(kick, label);

    const line = document.createElement('p');
    line.className = 'stage__status';
    stage.append(row, line);
    setInterval(() => {
      if (!lamp.ticked) return;
      const th = lamp.pendulum;
      const period = periodSeconds(Math.max(K_MIN_CORD, lamp.cordMetres));
      line.textContent =
        `Lamp at (${lamp.px[0].toFixed(0)}, ${lamp.px[1].toFixed(0)}) px, ${lamp.px[2].toFixed(0)} px above the sheet; `
        + `swing ${(th.theta[0] * 180 / Math.PI).toFixed(1)}° in x, ${(th.theta[1] * 180 / Math.PI).toFixed(1)}° in y; `
        + `${lamp.kickCount} kick${lamp.kickCount === 1 ? '' : 's'}; period ${period.toFixed(2)} s from a ${lamp.cordMetres.toFixed(2)} m cord.`;
    }, 250);
  }
}
