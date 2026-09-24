#include "Audio.h"

#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace repousse
{

double OnsetDetector::Update( const float* bins, int binCount, double now, bool jumped, int perturb )
{
	const int to = std::min( controls::kOnsetBins, binCount );
	double sum   = 0.0;
	int counted  = 0;
	for( int i = 0; bins != nullptr && i < to; ++i, ++counted )
		sum += std::sqrt( std::max( 0.0, static_cast< double >( bins[ i ] ) ) );
	const double x = counted > 0 ? std::clamp( sum / counted, 0.0, 1.0 ) : 0.0;
	instant        = x;

	if( !primed || jumped )
	{
		//Prime: what is playing now is the baseline, not an onset. The
		//negative control starts from silence instead, as an unprimed
		//detector would.
		const bool fromSilence = ( perturb & perturb::kUnprimed ) != 0;
		baseline               = fromSilence ? 0.0 : x;
		lastFlux               = 0.0;
		envelope               = 0.0;
		last                   = now;
		primed                 = true;
	}

	const double dt = std::max( 0.0, now - last );
	last            = now;

	//The rise above the baseline as it stood before this frame.
	const double flux = std::max( 0.0, x - baseline );
	baseline += ( x - baseline ) * ( 1.0 - std::exp( -dt / kBaselineTime ) );

	const double strength = std::min( 1.0, flux * kGain );
	const double decayed  = envelope * std::exp( -dt / kRelease );

	double kick = 0.0;
	if( flux >= lastFlux && strength > decayed && strength >= kThreshold )
	{
		kick     = strength - decayed;
		envelope = strength;
	}
	else
		envelope = decayed;
	lastFlux = flux;
	return kick;
}

} // namespace repousse
