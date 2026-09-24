#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace repousse::controls
{
namespace
{
double unit( float value )
{
	return std::clamp( static_cast< double >( value ), 0.0, 1.0 );
}

const Rgb kF0[ kMetalCount ] = {
	{ 0.955, 0.638, 0.538 },//copper
	{ 0.910, 0.778, 0.423 },//brass (C260)
	{ 0.972, 0.960, 0.915 },//silver
	{ 1.000, 0.782, 0.344 },//gold
	{ 0.562, 0.565, 0.578 },//steel: the table's iron
};
} // namespace

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

double DepthPxFromParam( float value )
{
	return 64.0 * unit( value );
}

double PunchRadiusPxFromParam( float value )
{
	return 16.0 * unit( value );
}

double SmoothSigmaFromParam( float value )
{
	return 3.0 * unit( value );
}

double RoughnessAlphaFromParam( float value )
{
	return std::max( kMinAlpha, unit( value ) );
}

double LampHeightFromParam( float value )
{
	return std::max( kMinLampHeight, 2.0 * unit( value ) );
}

double IntensityFromParam( float value )
{
	return 2.0 * unit( value );
}

double CordLengthFromParam( float value )
{
	return std::max( kMinCord, 2.0 * unit( value ) );
}

double DampingRatioFromParam( float value )
{
	return std::max( kMinDamping, unit( value ) );
}

double SwingRateFromParam( float value )
{
	return 3.0 * unit( value );
}

int MorphTaps( double radiusPx )
{
	if( radiusPx < 1.0 )
		return 0;
	return std::min( kMaxMorphTaps, static_cast< int >( std::floor( radiusPx ) ) );
}

double PunchProfileError( double radius, double r )
{
	const double s = std::sqrt( std::max( 0.0, radius * radius - r * r ) );
	return r * r * r * r / ( 2.0 * radius * ( radius + s ) * ( radius + s ) );
}

const Rgb& MetalF0( int metal )
{
	return kF0[ std::clamp( metal, 0, kMetalCount - 1 ) ];
}

} // namespace repousse::controls
