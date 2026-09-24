#include "Pendulum.h"

#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace repousse
{

void Pendulum::Kick( double rate, double direction )
{
	omega[ 0 ] += rate * std::cos( direction );
	omega[ 1 ] += rate * std::sin( direction );
}

double Pendulum::NextDirection()
{
	const double direction = kGoldenAngle * kicks;
	++kicks;
	return direction;
}

void Pendulum::Reset()
{
	theta[ 0 ] = theta[ 1 ] = 0.0;
	omega[ 0 ] = omega[ 1 ] = 0.0;
	kicks                   = 0;
}

void Pendulum::Update( double dt, int perturb )
{
	using namespace controls;
	const double g      = ( perturb & perturb::kGravityDetuned ) ? kGravity * 1.03 : kGravity;
	const double L      = std::max( kMinCord, config.cordMetres );
	const double omega0 = std::sqrt( g / L );
	const double zeta   = ( perturb & perturb::kDampingDetuned ) ? config.dampingRatio * 1.15 : config.dampingRatio;
	const bool linear   = ( perturb & perturb::kLinearPendulum ) != 0;

	const double bounded = std::clamp( dt, 0.0, kMaxFrame );
	const int steps      = static_cast< int >( std::ceil( bounded / kSubStep ) );
	const double h       = steps > 0 ? bounded / steps : 0.0;

	auto accel = [ & ]( double th, double om ) {
		return -omega0 * omega0 * ( linear ? th : std::sin( th ) ) - 2.0 * zeta * omega0 * om;
	};

	for( int s = 0; s < steps; ++s )
		for( int i = 0; i < 2; ++i )
		{
			const double th = theta[ i ], om = omega[ i ];
			const double k1t = om, k1w = accel( th, om );
			const double k2t = om + 0.5 * h * k1w, k2w = accel( th + 0.5 * h * k1t, om + 0.5 * h * k1w );
			const double k3t = om + 0.5 * h * k2w, k3w = accel( th + 0.5 * h * k2t, om + 0.5 * h * k2w );
			const double k4t = om + h * k3w, k4w = accel( th + h * k3t, om + h * k3w );
			theta[ i ] = th + h / 6.0 * ( k1t + 2.0 * k2t + 2.0 * k3t + k4t );
			omega[ i ] = om + h / 6.0 * ( k1w + 2.0 * k2w + 2.0 * k3w + k4w );
		}
}

double Pendulum::OffsetX() const
{
	return config.cordMetres * std::sin( theta[ 0 ] );
}

double Pendulum::OffsetY() const
{
	return config.cordMetres * std::sin( theta[ 1 ] );
}

double Pendulum::Rise() const
{
	return config.cordMetres * ( 2.0 - std::cos( theta[ 0 ] ) - std::cos( theta[ 1 ] ) );
}

} // namespace repousse
