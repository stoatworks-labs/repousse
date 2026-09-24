#pragma once

#include "Audio.h"
#include "Clock.h"
#include "PassBuffer.h"
#include "Pendulum.h"

#include <FFGLSDK.h>

#include <string>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

/**
	Repousse -- the picture hammered into a metal sheet from behind and lit
	by a lamp on a swinging cord, as an FFGL effect.

	**The one idea.** Two physical facts make the look, and both are in the
	model rather than in a filter. The punch sets the finest detail: a sheet
	can be pushed only as sharply as the tool that pushes it, so the height
	field built from luma is opened and closed by the punch's profile, and
	detail narrower than the punch is never formed. And metal is lit, not
	shaded: a point lamp at a real position with inverse-square falloff, a
	GGX microfacet highlight with the measured F0 of copper, brass, silver,
	gold or steel, a small diffuse term for the patina, and self-shadowing by
	a march over the same height field. The lamp hangs on a cord: a damped
	pendulum with the period its length gives it, kicked by the music.

	**Ten passes at most** (`Shaders.h`): the picture into a height buffer,
	eight one-axis morphology passes (or none), and the lit sheet. The sheet
	is a pure function of the frame; the only state across frames is the
	pendulum and the onset detector, on the CPU. See AGENTS.md for the
	punch's paraboloid, the traps and what is verified.
*/
class Repousse : public CFFGLPlugin
{
public:
	Repousse();

	//CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTime( double time ) override;

	char* GetTextParameter( unsigned int index ) override;

	/// Declared only so the About line can accept its own default.
	/// instantiateGL pushes every declared default back through the setters
	/// and deletes the whole instance if one fails, and CFFGLPlugin's
	/// SetTextParameter is a stub that returns exactly that failure.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	//--- test hooks. Read by rptest; the plugin's own operation never uses
	//--- them, and the perturbation is always 0 outside the harness.

	/// The harness DECLARES its clock unit rather than leaving the voting to
	/// infer one: it renders as fast as the GPU allows.
	void SetClockScaleForTest( double scale )
	{
		clock.SetScaleForTest( scale );
	}

	/// Negative-control hooks, a bitmask of `repousse::perturb`.
	void SetPerturbForTest( int bits )
	{
		perturb = bits;
	}

	/// The lamp's position the last frame used, in height-buffer pixels.
	/// Diagnostics only: the checks measure it out of the picture.
	void LampForTest( double& x, double& y, double& z ) const
	{
		x = lampPx[ 0 ];
		y = lampPx[ 1 ];
		z = lampPx[ 2 ];
	}

	/// How many kicks the pendulum has taken since InitGL.
	int KicksForTest() const
	{
		return kickCount;
	}

	/// Everything the operator can reach, in the order Resolume shows them.
	/// SetParamGroup collapses consecutive ids, so each group is a contiguous
	/// run, and the order is load-bearing: append only.
	enum ParamID : FFUInt32
	{
		//Sheet
		PT_DEPTH,
		PT_INVERT,
		PT_PUNCH,
		PT_SMOOTH,
		PT_METAL,
		PT_ROUGHNESS,
		PT_PATINA,
		PT_COLOUR,

		//Lamp
		PT_LAMP_X,
		PT_LAMP_Y,
		PT_LAMP_HEIGHT,
		PT_INTENSITY,
		PT_AMBIENT,
		PT_SHADOWS,

		//Pendulum
		PT_CORD,
		PT_DAMPING,
		PT_SWING,
		PT_KICK,
		PT_AUDIO_FFT,

		//Output
		PT_MIX,
		PT_SHOW_HEIGHT,

		//About. FFGL has no window, so the name, the version and the links are
		//parameters the host draws. Last, so no saved composition's ids shift.
		PT_ABOUT_FIRST,
		PT_COUNT = PT_ABOUT_FIRST + stoatworks::about::kParamCount
	};

private:
	bool ensureBuffers( int width, int height, bool morphing );
	void heightPass( const FFGLTextureStruct& picture, float depthPx );
	void blurPass( repousse::PassBuffer& from, repousse::PassBuffer& to, int dirX, int dirY );
	void morphPass( repousse::PassBuffer& from, repousse::PassBuffer& to, int dirX, int dirY, bool erode, int taps, float invTwoR );
	void uploadWeights( double sigma );

	ffglex::FFGLShader heightShader;
	ffglex::FFGLShader blurShader;
	ffglex::FFGLShader morphShader;
	ffglex::FFGLShader shadeShader;
	ffglex::FFGLScreenQuad quad;

	repousse::PassBuffer height;///< the relief before the punch
	repousse::PassBuffer pingA; ///< the morphology's two halves
	repousse::PassBuffer pingB;

	/// Weights[ k ] at distance k, normalised over -radius..radius.
	float weights[ 10 ] = {};
	int radius          = 0;
	double weightsSigma = -1.0;

	repousse::OnsetDetector onsets;
	repousse::Pendulum pendulum;
	repousse::Clock clock;
	double hostTime = -1.0;
	double lastNow  = 0.0;
	bool ticked     = false;
	int clockFrames = 0;

	float lastKickValue = 0.0f;
	bool kickPending    = false;
	int kickCount       = 0;

	double lampPx[ 3 ] = { 0.0, 0.0, 0.0 };

	int perturb = 0;

	/// Zero-initialised: the About block's ids are never stored to.
	float params[ PT_COUNT ] = {};

	/// GetTextParameter hands the host a bare pointer, so the string has to
	/// outlive the call.
	std::string aboutText;
};
