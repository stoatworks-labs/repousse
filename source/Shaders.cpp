#include "Shaders.h"

namespace repousse::shaders
{

const char* const kVertex = R"(#version 410 core

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
)";

//---------------------------------------------------------------------------
// height: the picture read as relief. One texel per host pixel, same rows.
//---------------------------------------------------------------------------
const char* const kHeight = R"(#version 410 core

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
)";

//---------------------------------------------------------------------------
// blur: one axis of the smoothing Gaussian. Weights[ k ] is the weight at
// distance k, normalised on the CPU in double over -Radius..Radius; the
// edge is clamped (the sheet carries on level past the frame). Summed as
// symmetric pairs, centre first.
//---------------------------------------------------------------------------
const char* const kBlur = R"(#version 410 core

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
)";

//---------------------------------------------------------------------------
// morph: one axis of a 1-D erosion or dilation by the punch. The punch's
// tip is the paraboloid -k^2 / 2R (the surface osculating a sphere of
// radius R at its apex: same curvature, exactly separable) on a shank of
// half-width R, so over |k| <= Taps = floor( R ):
//
//     erode:  min over k of h( p + k dir ) + k^2 / 2R
//     dilate: max over k of h( p + k dir ) - k^2 / 2R
//
// Beyond R the tool's shank is what meets the sheet, so nothing wider than
// the punch is touched. The edge is clamped (the sheet carries on level
// past the frame).
//---------------------------------------------------------------------------
const char* const kMorph = R"(#version 410 core

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
)";

//---------------------------------------------------------------------------
// shade: the lit sheet.
//
// The surface point is P = ( pixel centre, h ) in pixels; the lamp is at
// Lamp, also in pixels. The irradiance is Intensity ( LampRef / r )^2 times
// n.l -- normalised so a white Lambertian sheet directly under the lamp at
// its nominal height reads Intensity -- times the visibility from a march
// along the ray to the lamp over the height field. The BRDF is a Lambert
// term (the patina) and GGX / height-correlated Smith / Schlick with the
// metal's F0; the viewer looks straight down. The patina's occlusion is the
// mean sine of the horizon over eight directions, and darkens the diffuse
// and the ambient, never the highlight.
//---------------------------------------------------------------------------
const char* const kShade = R"(#version 410 core

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
)";

} // namespace repousse::shaders
