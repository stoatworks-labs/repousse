#pragma once

/**
	The onset detector: 64 FFT bins in, one kick out.

	CPU only, no GL, so `rptest --detector` checks it with no context at all.

	  instantaneous   the mean of sqrt( bin ) over the lowest kOnsetBins bins.
	                  sqrt because bin magnitudes bunch against zero
	                  (regauss's reason); the mean because a peak hops between
	                  bins. Nobody has measured Resolume's bins, so nothing
	                  here assumes they are linear in anything: "the lowest
	                  eight of sixty-four" is the whole assumption.
	  flux            the rise of the instantaneous value above a quick
	                  (0.1 s) baseline, times kGain, capped at 1.
	  kick            a kick is the ARRIVAL of a hit, not its level: the
	                  frame on which the flux is still rising and has climbed
	                  above what is left of the last kick's envelope, and above
	                  kThreshold. Its strength is how far it climbed. A held
	                  note kicks once; a drone never kicks.

	**Primed on the first frame and after every jump.** An unprimed baseline
	starts at silence, so the first frame of a clip with audio reads as the
	loudest onset there has ever been, and the lamp is kicked on every clip
	trigger. Priming sets the baseline to the instantaneous value and the
	envelope to zero. A dt of zero is a frame with no release, never a snap.
*/
namespace repousse
{
class OnsetDetector
{
public:
	/// Forget everything; the next Update primes.
	void Reset()
	{
		primed = false;
	}

	/// One frame. `bins` may be null (no audio routed), which reads as
	/// silence. `jumped` re-primes. Returns the kick strength, 0..1, or 0
	/// when this frame is not the arrival of a hit.
	double Update( const float* bins, int binCount, double now, bool jumped, int perturb );

	/// The band's instantaneous value last frame.
	double Instantaneous() const
	{
		return instant;
	}

	/// The kick envelope as it stands after the last frame.
	double Envelope() const
	{
		return envelope;
	}

	static constexpr double kBaselineTime = 0.1;
	static constexpr double kRelease      = 0.3;
	static constexpr double kGain         = 4.0;
	static constexpr double kThreshold    = 0.15;

private:
	bool primed     = false;
	double last     = 0.0;
	double baseline = 0.0;
	double lastFlux = 0.0;
	double envelope = 0.0;
	double instant  = 0.0;
};

} // namespace repousse
