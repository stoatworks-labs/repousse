#pragma once

/**
	The lamp on its cord.

	A point lamp hanging from a cord of length L is a pendulum, and a
	pendulum has a period, 2 pi sqrt( L / g ), that nothing in the music can
	change. A kick -- an onset, or the Kick button -- adds angular velocity;
	the cord and gravity do the rest, and the damping decides how many swings
	a kick is worth (Q = 1 / 2 zeta).

	Two planar pendulums, one in the picture's x and one in its y, each the
	full large-angle equation

	    theta'' = -( g / L ) sin theta - 2 zeta omega0 theta'

	so a hard kick swings visibly slower than a soft one, as a real lamp
	does. Kicks are delivered along a direction that steps round the golden
	angle from one kick to the next, so a run of hits swings the lamp in a
	rosette rather than a line, and the same music swings it the same way on
	every machine and every replay.

	**Integrated in double, on the CPU, in frame-relative time.** Resolume's
	clock has been measured at ~499 million ms, where a float resolves
	~0.03 s; the shader is only ever handed a lamp position. Classical
	Runge-Kutta at a fixed 1/480 s substep, the elapsed TIME clamped rather
	than the step count, so a stalled host cannot spend a second in one
	Update and an unclamped frame cannot outrun the integrator.

	`Update` hands back the lamp's displacement in metres, and one frame
	height is one metre (Controls.h), so the picture and the period agree.
*/
namespace repousse
{
struct PendulumSettings
{
	double cordMetres   = 0.5;
	double dampingRatio = 0.05;
};

class Pendulum
{
public:
	void Configure( const PendulumSettings& settings )
	{
		config = settings;
	}

	/// Add angular velocity `rate` rad/s along the direction `direction`
	/// (radians in the picture plane, 0 = +x).
	void Kick( double rate, double direction );

	/// The next kick's direction, and step it round the golden angle.
	double NextDirection();

	/// Advance by `dt` seconds (clamped inside). `perturb` bits from
	/// repousse::perturb.
	void Update( double dt, int perturb );

	/// Everything to rest.
	void Reset();

	/// The lamp's horizontal displacement from the rest point, in metres.
	double OffsetX() const;
	double OffsetY() const;
	/// How far the lamp has risen from its lowest point, in metres:
	/// L ( 1 - cos theta ) per axis.
	double Rise() const;

	double AngleX() const
	{
		return theta[ 0 ];
	}
	double AngleY() const
	{
		return theta[ 1 ];
	}

	static constexpr double kSubStep   = 1.0 / 480.0;
	static constexpr double kMaxFrame  = 0.5;///< seconds integrated per Update at most
	/// The golden angle, radians.
	static constexpr double kGoldenAngle = 2.399963229728653;

private:
	PendulumSettings config;
	double theta[ 2 ] = { 0.0, 0.0 };
	double omega[ 2 ] = { 0.0, 0.0 };
	int kicks         = 0;
};

} // namespace repousse
