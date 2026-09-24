#pragma once

/**
	The passes. Every read that decides a normal is `texelFetch` at an
	integer coordinate computed from `gl_FragCoord`, so nothing there depends
	on where a rasteriser's interpolated uv lands or on a texture unit's
	filtering precision; the shadow march and the occlusion read the height
	between texels (bilinear), which is the stated model of the sheet's
	surface between its samples. The only floating-point work a driver can
	do differently is the arithmetic itself, which GLSL 4.10 section 8.2
	bounds.

	  height    the host picture -> height in pixels (R32F, the picture's
	            size): Rec. 709 luma, inverted or not, clamped to 0..1, times
	            Depth
	  blur      height -> height, one axis of a separable Gaussian whose
	            weights the CPU computed in double. Run twice before the
	            punch, or skipped at Smooth 0
	  morph     height -> height, one axis of a 1-D erosion or dilation by
	            the punch's profile -k^2 / 2R. Eight passes make the opening
	            (erode x, erode y, dilate x, dilate y) then the closing, or
	            none at Punch Radius 0
	  shade     height -> the host's framebuffer: normal, lamp, shadow march,
	            occlusion, GGX, Fresnel, patina, ambient, mix

	Rows stay in GL's order throughout (row 0 is the bottom of the picture),
	and the lamp's position is in the height buffer's pixels. Only the
	harness, which builds its pictures top first, flips.
*/
namespace repousse::shaders
{

extern const char* const kVertex;
extern const char* const kHeight;
extern const char* const kBlur;
extern const char* const kMorph;
extern const char* const kShade;

} // namespace repousse::shaders
