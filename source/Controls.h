#pragma once

/**
	What a host parameter means, in the sheet's own units.

	Every ranged host parameter is 0..1 (SetParamInfo clamps a STANDARD
	default into 0..1 before a range can be attached), and an option
	parameter's range reads back 0..1 whatever its element count -- so an
	option is its element INDEX, rounded and clamped here, never a fraction
	of a range.

	The units:

	  pixels         Depth (the height the brightest pixel is raised) and
	                 Punch Radius. The relief is a thing done to the picture's
	                 pixels, so it is measured in them; the same clip at 4K is
	                 hammered finer, not deeper.
	  frame heights  Lamp X/Y (fractions of the frame), Lamp Height and the
	                 pendulum's cord. ONE FRAME HEIGHT IS ONE METRE: that is
	                 what ties the pendulum's period (seconds, from metres and
	                 g) to the distance the lamp is seen to swing.
	  metres         Cord Length, through that scale.
	  rad/s          Swing, the angular velocity one full-strength kick adds.

	Every mapping is linear or a clamp so a harness can choose an exactly
	representable value (Roughness 0.5 is an alpha of 0.5; Depth 0.25 is 16
	px), and every mapping is stated independently in tools/rptest, which
	reads the plugin only through the picture.
*/
namespace repousse::controls
{

/// An option's stored value, as an index into its `count` elements.
int OptionIndex( float value, int count );

/// Depth: the height of a white pixel, in pixels: 64 p.
double DepthPxFromParam( float value );

/// Punch Radius: the tip radius of the punch, in pixels: 16 p. Zero skips
/// the opening and closing entirely.
double PunchRadiusPxFromParam( float value );

/// Smooth: the sigma in pixels of a Gaussian run over the height before
/// the punch, 3 p. Not detail control -- that is the punch -- but the
/// answer to 8-bit footage: a one-code step in a gentle gradient is a
/// 0.06 px riser, and a raking lamp lights every riser as a terrace.
double SmoothSigmaFromParam( float value );

/// Roughness: the GGX alpha, max( kMinAlpha, p ).
double RoughnessAlphaFromParam( float value );

/// Lamp Height in frame heights, max( kMinLampHeight, 2 p ).
double LampHeightFromParam( float value );

/// Intensity: the irradiance directly under the lamp at its nominal height,
/// on a white Lambertian sheet: 2 p.
double IntensityFromParam( float value );

/// Cord Length in metres, max( kMinCord, 2 p ). The period is 2 pi sqrt( L / g ).
double CordLengthFromParam( float value );

/// Damping: the damping ratio zeta, max( kMinDamping, p ). Q = 1 / ( 2 zeta ).
double DampingRatioFromParam( float value );

/// Swing: the angular velocity a full kick adds, in rad/s: 3 p.
double SwingRateFromParam( float value );

/// How many taps each side a 1-D morphology pass takes: floor( R ). The
/// punch is a paraboloid TIP on a shank of half-width R, so its profile
/// ends at |k| = R -- a slot wider than the punch lets the whole tool in,
/// and a ridge wider than the punch keeps its full height. Under one pixel
/// there is nothing to do (a pixel is the finest detail there is), so 0.
int MorphTaps( double radiusPx );

/// The bound on the paraboloid punch against the sphere of the same tip
/// radius, at distance r from the tip (both in the same unit):
/// r^4 / ( 2 R ( R + sqrt( R^2 - r^2 ) )^2 ), for r <= R.
double PunchProfileError( double radius, double r );

constexpr double kGravity              = 9.80665;///< m/s^2
constexpr double kMetresPerFrameHeight = 1.0;
constexpr double kMinAlpha             = 1.0 / 64.0;
constexpr double kMinLampHeight        = 0.02;///< frame heights
constexpr double kMinCord              = 0.05;///< metres
constexpr double kMinDamping           = 0.005;
constexpr int kMaxMorphTaps            = 64;
/// The largest radius the smoothing's weight table holds: ceil( 3 sigma ).
constexpr int kMaxBlurRadius = 9;

/// The shadow march: this many samples between the surface point and where
/// the ray to the lamp clears the tallest possible relief.
constexpr int kShadowSteps = 32;
/// The terrain has to stand this far (in pixels of height) above the ray to
/// shadow it -- enough to keep a plane facing the lamp free of acne, a
/// hundredth of a pixel on a shadow's edge.
constexpr double kShadowBias = 0.01;

/// The metal's own diffuse term: the patina's share. Metals have next to no
/// diffuse reflection; tarnish does.
constexpr double kMetalDiffuse = 0.2;
/// The dielectric F0 of a painted sheet (Colour = Clip).
constexpr double kPaintF0 = 0.04;

/// The horizon-based occlusion: eight directions, four radii in pixels.
constexpr int kAoDirections               = 8;
constexpr int kAoRadii                    = 4;
constexpr float kAoRadiusPx[ kAoRadii ]   = { 2.0f, 5.0f, 9.0f, 14.0f };

/// The audio: 64 FFT bins asked of the host; the onset detector listens to
/// the lowest kOnsetBins of them (regauss's Bass band) whatever law the bins
/// follow. Nobody has measured Resolume's bins.
constexpr int kAudioBins = 64;
constexpr int kOnsetBins = 8;

/// What Metal stores, and each metal's F0 in linear sRGB.
enum Metal
{
	kCopper = 0,
	kBrass,
	kSilver,
	kGold,
	kSteel,
	kMetalCount
};

struct Rgb
{
	double r, g, b;
};

/// Normal-incidence reflectance, linear sRGB, computed by tools/f0.py from
/// published complex refractive indices (Johnson & Christy 1972 for Cu, Ag,
/// Au; Johnson & Christy 1974 for Fe, which is Steel; Querry 1985 for the
/// Cu70Zn30 brass), integrated against the CIE 1931 2-degree observer under
/// D65. Gold's red is clamped to 1. `python3 tools/f0.py --check` re-derives
/// the table and compares it with this one.
const Rgb& MetalF0( int metal );

/// What Colour stores.
enum ColourMode
{
	kColourMetal = 0,
	kColourClip,
	kColourCount
};

} // namespace repousse::controls

/**
	Harness hooks. Each bit perturbs the PLUGIN's model -- never the harness's
	expectation -- so `rptest --negative` can prove each check fails against a
	wrong plugin. Two of them (kNoSpecular, kNoDiffuse) are not perturbations
	but isolations: they switch one term off so the other can be measured on
	its own. Always 0 outside the harness.
*/
namespace repousse::perturb
{
constexpr int kNoInverseSquare        = 1 << 0; ///< the lamp's 1/r^2 dropped
constexpr int kNoShadowMarch          = 1 << 1; ///< visibility always 1
constexpr int kSpecularOnLight        = 1 << 2; ///< the GGX lobe evaluated on n.l, not n.h
constexpr int kF0Grey                 = 1 << 3; ///< every metal's F0 replaced by 0.5
constexpr int kFlatPunch              = 1 << 4; ///< the punch a flat disc: no paraboloid
constexpr int kLinearPendulum         = 1 << 5; ///< sin( theta ) replaced by theta
constexpr int kDampingDetuned         = 1 << 6; ///< zeta 15% high
constexpr int kGravityDetuned         = 1 << 7; ///< g 3% high
constexpr int kUnprimed               = 1 << 8; ///< the onset detector starts from silence
constexpr int kResizeStale            = 1 << 9; ///< a resize keeps the old height buffers
constexpr int kClockFloat             = 1 << 10;///< the clock kept in float
constexpr int kPendulumResetOnResize  = 1 << 11;///< a resize puts the lamp back to rest
constexpr int kPunchDetuned           = 1 << 12;///< the punch radius 10% wide
constexpr int kNoSpecular             = 1 << 13;///< isolation: specular off
constexpr int kNoDiffuse              = 1 << 14;///< isolation: diffuse off
} // namespace repousse::perturb
