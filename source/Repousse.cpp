#include "Repousse.h"

#include "Controls.h"
#include "Diag.h"
#include "Shaders.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9). The symptom without it is an unknown-type error on
//ScopedFBOBinding and nothing else.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <cmath>
#include <string>

using namespace ffglex;
using namespace repousse;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< Repousse >,// Create method
	"RP01",                   // Plugin unique ID of maximum length 4.
	"SW Repousse",            // Plugin name
	2,                        // API major version number
	1,                        // API minor version number
	0,                        // Plugin major version number
	1,                        // Plugin minor version number
	FF_EFFECT,                // Plugin type
	"Hammers the picture into a metal sheet and lights it with a lamp on a swinging cord.\n\nThe brightness is read as relief, then worked with a punch: detail narrower than the punch is never formed, not blurred away. The sheet is lit, not shaded -- a point lamp with inverse-square falloff, a microfacet highlight with the measured reflectance of copper, brass, silver, gold or steel, self-shadowing off every ridge, and a patina that darkens the recesses. The lamp is a pendulum with the period its cord gives it; an onset in the audio, or the Kick button, sets it swinging, and the highlights slide across the relief as it does.\n\nStart with bold shapes and a low lamp.",// Plugin description
	"Repousse FFGL effect"    // About
);

namespace
{
/// glGetString returns nullptr with no current context; a log line must never
/// be the thing that brings the host down.
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

const char* const kMetalNames[]  = { "Copper", "Brass", "Silver", "Gold", "Steel" };
const char* const kColourNames[] = { "Metal", "Clip" };

/// Height is a continuous quantity differenced across one pixel, so it is
/// held in full float: a half-float store would put 2^-11 steps into the
/// sheet, and on a gentle slope that is a staircase with a highlight on
/// every riser.
constexpr GLint kHeightFormat = GL_R32F;
} // namespace

//---------------------------------------------------------------------------
Repousse::Repousse()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//The pendulum and the onset detector read the clock; they need the
	//host's time so an export's swing matches the preview's.
	SetTimeSupported( true );

	//---------------------------------------------------------------------
	// Defaults: chased copper under a lamp hung up and to the left, half a
	// frame above the sheet, on a half-metre cord that swings for about ten
	// cycles after a kick. A 16 px relief worked with a 4 px punch.
	//---------------------------------------------------------------------
	params[ PT_DEPTH ]     = 0.25f; //16 px
	params[ PT_INVERT ]    = 0.0f;
	params[ PT_PUNCH ]     = 0.25f; //4 px
	params[ PT_SMOOTH ]    = 1.0f / 3.0f;//sigma 1 px: 8-bit footage's terraces
	params[ PT_METAL ]     = static_cast< float >( controls::kCopper );
	params[ PT_ROUGHNESS ] = 0.4f;
	params[ PT_PATINA ]    = 0.6f;
	params[ PT_COLOUR ]    = static_cast< float >( controls::kColourMetal );

	params[ PT_LAMP_X ]      = 0.3f;
	params[ PT_LAMP_Y ]      = 0.75f;
	params[ PT_LAMP_HEIGHT ] = 0.25f;//0.5 frame heights
	params[ PT_INTENSITY ]   = 0.45f;//0.9
	params[ PT_AMBIENT ]     = 0.12f;
	params[ PT_SHADOWS ]     = 1.0f;

	params[ PT_CORD ]    = 0.25f;//0.5 m: a 1.42 s period
	params[ PT_DAMPING ] = 0.05f;//Q 10
	params[ PT_SWING ]   = 0.4f; //1.2 rad/s per full kick
	params[ PT_KICK ]    = 0.0f;

	params[ PT_MIX ]         = 1.0f;
	params[ PT_SHOW_HEIGHT ] = 0.0f;

	auto declareOptions = [ this ]( unsigned int id, const char* name, const char* const* names, int count ) {
		SetOptionParamInfo( id, name, static_cast< unsigned int >( count ), params[ id ] );
		for( int i = 0; i < count; ++i )
			SetParamElementInfo( id, static_cast< unsigned int >( i ), names[ i ], static_cast< float >( i ) );
	};

	SetParamInfof( PT_DEPTH, "Depth", FF_TYPE_STANDARD );
	SetParamInfo( PT_INVERT, "Invert", FF_TYPE_BOOLEAN, false );
	SetParamInfof( PT_PUNCH, "Punch Radius", FF_TYPE_STANDARD );
	SetParamInfof( PT_SMOOTH, "Smooth", FF_TYPE_STANDARD );
	declareOptions( PT_METAL, "Metal", kMetalNames, controls::kMetalCount );
	SetParamInfof( PT_ROUGHNESS, "Roughness", FF_TYPE_STANDARD );
	SetParamInfof( PT_PATINA, "Patina", FF_TYPE_STANDARD );
	declareOptions( PT_COLOUR, "Colour", kColourNames, controls::kColourCount );

	SetParamInfof( PT_LAMP_X, "Lamp X", FF_TYPE_STANDARD );
	SetParamInfof( PT_LAMP_Y, "Lamp Y", FF_TYPE_STANDARD );
	SetParamInfof( PT_LAMP_HEIGHT, "Lamp Height", FF_TYPE_STANDARD );
	SetParamInfof( PT_INTENSITY, "Intensity", FF_TYPE_STANDARD );
	SetParamInfof( PT_AMBIENT, "Ambient", FF_TYPE_STANDARD );
	SetParamInfof( PT_SHADOWS, "Shadows", FF_TYPE_STANDARD );

	SetParamInfof( PT_CORD, "Cord Length", FF_TYPE_STANDARD );
	SetParamInfof( PT_DAMPING, "Damping", FF_TYPE_STANDARD );
	SetParamInfof( PT_SWING, "Swing", FF_TYPE_STANDARD );
	SetParamInfo( PT_KICK, "Kick", FF_TYPE_EVENT, false );
	// The spectrum. Declared with a real element list so the host knows how
	// many bins to fill. With no audio routed every bin stays 0, nothing
	// ever rises above the baseline and the lamp hangs still.
	SetBufferParamInfo( PT_AUDIO_FFT, "Audio", controls::kAudioBins, FF_USAGE_FFT );
	for( int i = 0; i < controls::kAudioBins; ++i )
		SetParamElementInfo( PT_AUDIO_FFT, static_cast< unsigned int >( i ), "", 0.0f );

	SetParamInfof( PT_MIX, "Mix", FF_TYPE_STANDARD );
	SetParamInfo( PT_SHOW_HEIGHT, "Show Height", FF_TYPE_BOOLEAN, false );

	//SetParamGroup collapses consecutive ids under one header, so each group
	//is a contiguous run of the enum.
	struct Group
	{
		FFUInt32 first, last;
		const char* name;
	} const groups[] = {
		{ PT_DEPTH, PT_COLOUR, "Sheet" },
		{ PT_LAMP_X, PT_SHADOWS, "Lamp" },
		{ PT_CORD, PT_AUDIO_FFT, "Pendulum" },
		{ PT_MIX, PT_SHOW_HEIGHT, "Output" },
	};
	for( const Group& g : groups )
		for( FFUInt32 i = g.first; i <= g.last; ++i )
			SetParamGroup( i, g.name );

	// The About block. Declared inline: SetParamInfo is protected on
	// CFFGLPlugin and nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_FIRST; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );

	FFGLLog::LogToHost( "Created Repousse effect" );
	diag::init();
}

//---------------------------------------------------------------------------
FFResult Repousse::InitGL( const FFGLViewportStruct* vp )
{
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	struct
	{
		FFGLShader* shader;
		const char* fragment;
		const char* name;
	} const stages[] = {
		{ &heightShader, shaders::kHeight, "height" },
		{ &blurShader, shaders::kBlur, "blur" },
		{ &morphShader, shaders::kMorph, "morph" },
		{ &shadeShader, shaders::kShade, "shade" },
	};

	for( const auto& stage : stages )
	{
		if( stage.shader->Compile( shaders::kVertex, stage.fragment ) )
			continue;
		//FF_FAIL is invisible to an operator: the effect simply does nothing.
		//This line is the only record of which pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the effect will do nothing" );
		FFGLLog::LogToHost( "Repousse: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	clock.Reset();
	onsets.Reset();
	pendulum.Reset();
	weightsSigma = -1.0;
	ticked    = false;
	kickCount = 0;
	//kickPending survives: a Kick pressed before the first frame is a kick.

	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
bool Repousse::ensureBuffers( int width, int heightPx, bool working )
{
	//Linear on every buffer: the normal reads texels exactly with texelFetch
	//(filtering does not apply to it), the shadow march and the occlusion
	//read the surface between texels.
	if( !height.Ensure( width, heightPx, kHeightFormat, PassBuffer::Sampling::Linear ) )
		return false;
	if( !working )
		return true;
	return pingA.Ensure( width, heightPx, kHeightFormat, PassBuffer::Sampling::Linear )
	       && pingB.Ensure( width, heightPx, kHeightFormat, PassBuffer::Sampling::Linear );
}

void Repousse::heightPass( const FFGLTextureStruct& picture, float depthPx )
{
	ScopedFBOBinding fbo( height.GetGLID(), ScopedFBOBinding::RB_REVERT );
	height.ResizeViewPort();
	ScopedShaderBinding shader( heightShader.GetGLID() );
	ScopedSamplerActivation sampler( 0 );
	Scoped2DTextureBinding texture( picture.Handle );
	heightShader.Set( "InputTexture", 0 );
	heightShader.Set( "Invert", params[ PT_INVERT ] >= 0.5f ? 1 : 0 );
	heightShader.Set( "Depth", depthPx );
	quad.Draw();
}

void Repousse::uploadWeights( double sigma )
{
	if( sigma == weightsSigma )
		return;
	weightsSigma = sigma;

	//Radius ceil( 3 sigma ): the tail past it is 0.3% of the mass, and the
	//weights are renormalised over what is kept, so a plane stays exactly a
	//plane -- a blur that lost mass at the edge of its kernel would tilt the
	//sheet and light it.
	radius = sigma > 0.0 ? std::min( controls::kMaxBlurRadius, static_cast< int >( std::ceil( 3.0 * sigma ) ) ) : 0;
	double w[ 10 ] = {};
	double total   = 0.0;
	for( int k = 0; k <= radius; ++k )
	{
		w[ k ] = radius == 0 ? 1.0 : std::exp( -0.5 * k * k / ( sigma * sigma ) );
		total += k == 0 ? w[ k ] : 2.0 * w[ k ];
	}
	for( int k = 0; k < 10; ++k )
		weights[ k ] = k <= radius ? static_cast< float >( w[ k ] / total ) : 0.0f;
}

void Repousse::blurPass( PassBuffer& from, PassBuffer& to, int dirX, int dirY )
{
	ScopedFBOBinding fbo( to.GetGLID(), ScopedFBOBinding::RB_REVERT );
	to.ResizeViewPort();
	ScopedShaderBinding shader( blurShader.GetGLID() );
	ScopedSamplerActivation sampler( 0 );
	Scoped2DTextureBinding texture( from.TextureID() );
	blurShader.Set( "Source", 0 );
	blurShader.Set( "Width", static_cast< int >( from.GetWidth() ) );
	blurShader.Set( "Height", static_cast< int >( from.GetHeight() ) );
	blurShader.Set( "DirX", dirX );
	blurShader.Set( "DirY", dirY );
	blurShader.Set( "Radius", radius );
	//FFGLShader::Set has no array overload.
	glUniform1fv( glGetUniformLocation( blurShader.GetGLID(), "Weights" ), 10, weights );
	quad.Draw();
}

void Repousse::morphPass( PassBuffer& from, PassBuffer& to, int dirX, int dirY, bool erode, int taps, float invTwoR )
{
	ScopedFBOBinding fbo( to.GetGLID(), ScopedFBOBinding::RB_REVERT );
	to.ResizeViewPort();
	ScopedShaderBinding shader( morphShader.GetGLID() );
	ScopedSamplerActivation sampler( 0 );
	Scoped2DTextureBinding texture( from.TextureID() );
	morphShader.Set( "Source", 0 );
	morphShader.Set( "Width", static_cast< int >( from.GetWidth() ) );
	morphShader.Set( "Height", static_cast< int >( from.GetHeight() ) );
	morphShader.Set( "DirX", dirX );
	morphShader.Set( "DirY", dirY );
	morphShader.Set( "Taps", taps );
	morphShader.Set( "Erode", erode ? 1 : 0 );
	morphShader.Set( "InvTwoR", invTwoR );
	morphShader.Set( "Flat", ( perturb & perturb::kFlatPunch ) ? 1 : 0 );
	quad.Draw();
}

//---------------------------------------------------------------------------
FFResult Repousse::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	const FFGLTextureStruct& picture = *pGL->inputTextures[ 0 ];
	if( picture.Width == 0 || picture.Height == 0 )
		return FF_FAIL;

	//The host's viewport, before anything of ours changes it:
	//ScopedFBOBinding restores the framebuffer binding and only that.
	GLint hostViewport[ 4 ] = { 0, 0, 0, 0 };
	glGetIntegerv( GL_VIEWPORT, hostViewport );

	clock.SetFloatForTest( ( perturb & perturb::kClockFloat ) != 0 );
	clock.Update( hostTime );
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( hostTime ) + " scale="
		            + std::to_string( clock.ClockScale() ) + " seconds=" + std::to_string( clock.Now() ) );

	const int width    = static_cast< int >( picture.Width );
	const int heightPx = static_cast< int >( picture.Height );
	const bool resized = height.IsValid() && ( static_cast< int >( height.GetWidth() ) != width
	                                           || static_cast< int >( height.GetHeight() ) != heightPx );

	//---------------------------------------------------------------------
	// The lamp. CPU state, carried across a resize untouched: a clip
	// changing size does not stop a lamp swinging.
	//---------------------------------------------------------------------
	if( resized && ( perturb & perturb::kPendulumResetOnResize ) )
	{
		pendulum.Reset();
		onsets.Reset();
	}
	const double now = clock.Now();
	//A jump is one nominal frame (the clock says so); the first frame is no
	//time at all.
	const double dt = ticked ? std::max( 0.0, now - lastNow ) : 0.0;
	lastNow          = now;
	ticked           = true;

	float bins[ controls::kAudioBins ] = {};
	int binCount                       = 0;
	if( const ParamInfo* info = FindParamInfo( PT_AUDIO_FFT ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), controls::kAudioBins ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	const double onset = onsets.Update( bins, binCount, now, clock.Jumped(), perturb );

	PendulumSettings ps;
	ps.cordMetres   = controls::CordLengthFromParam( params[ PT_CORD ] );
	ps.dampingRatio = controls::DampingRatioFromParam( params[ PT_DAMPING ] );
	pendulum.Configure( ps );
	const double swingRate = controls::SwingRateFromParam( params[ PT_SWING ] );
	if( kickPending )
	{
		pendulum.Kick( swingRate, pendulum.NextDirection() );
		kickPending = false;
		++kickCount;
	}
	if( onset > 0.0 )
	{
		pendulum.Kick( swingRate * onset, pendulum.NextDirection() );
		++kickCount;
	}
	pendulum.Update( dt, perturb );

	//---------------------------------------------------------------------
	// Units. Every conversion in double, handed over as float uniforms. One
	// frame height is one metre.
	//---------------------------------------------------------------------
	const double depth   = controls::DepthPxFromParam( params[ PT_DEPTH ] );
	const double radius  = controls::PunchRadiusPxFromParam( params[ PT_PUNCH ] ) * ( ( perturb & perturb::kPunchDetuned ) ? 1.1 : 1.0 );
	const int taps       = controls::MorphTaps( radius );
	const bool morphing  = taps > 0;
	uploadWeights( controls::SmoothSigmaFromParam( params[ PT_SMOOTH ] ) );
	const bool blurring  = this->radius > 0;
	const bool working   = morphing || blurring;
	const double pxPerM  = heightPx / controls::kMetresPerFrameHeight;
	const double lampRef = controls::LampHeightFromParam( params[ PT_LAMP_HEIGHT ] ) * heightPx;
	lampPx[ 0 ]          = static_cast< double >( params[ PT_LAMP_X ] ) * width + pendulum.OffsetX() * pxPerM;
	lampPx[ 1 ]          = static_cast< double >( params[ PT_LAMP_Y ] ) * heightPx + pendulum.OffsetY() * pxPerM;
	lampPx[ 2 ]          = lampRef + pendulum.Rise() * pxPerM;

	const int metal        = controls::OptionIndex( params[ PT_METAL ], controls::kMetalCount );
	const int colourMode   = controls::OptionIndex( params[ PT_COLOUR ], controls::kColourCount );
	const controls::Rgb f0 = controls::MetalF0( metal );

	//---------------------------------------------------------------------
	// Buffers. Every allocation happens here, before anything binds a
	// texture: FFGLFBO::Initialise sizes its colour texture under a scoped
	// binding, and every ffglex Scoped* binding CLEARS to 0 on exit. Nothing
	// in them outlives a frame, so a resize simply reallocates.
	//---------------------------------------------------------------------
	if( !( resized && ( perturb & perturb::kResizeStale ) ) && !ensureBuffers( width, heightPx, working ) )
	{
		diag::error( "could not allocate the height buffers at " + std::to_string( width ) + "x" + std::to_string( heightPx ) );
		return FF_FAIL;
	}
	if( working && !pingB.IsValid() && !ensureBuffers( width, heightPx, true ) )
		return FF_FAIL;

	heightPass( picture, static_cast< float >( depth ) );
	PassBuffer* sheet = &height;
	auto next = [ & ]( PassBuffer* from ) { return from == &pingA ? &pingB : &pingA; };
	if( blurring )
	{
		blurPass( *sheet, pingA, 1, 0 );
		blurPass( pingA, pingB, 0, 1 );
		sheet = &pingB;
	}
	if( morphing )
	{
		const float invTwoR = static_cast< float >( 1.0 / ( 2.0 * radius ) );
		//The opening: erode, then dilate. Then the closing: dilate, then
		//erode. Each in x then y; the paraboloid is exactly separable.
		const bool erodes[ 8 ] = { true, true, false, false, false, false, true, true };
		for( int pass = 0; pass < 8; ++pass )
		{
			PassBuffer* to = next( sheet );
			morphPass( *sheet, *to, pass % 2 == 0 ? 1 : 0, pass % 2 == 0 ? 0 : 1, erodes[ pass ], taps, invTwoR );
			sheet = to;
		}
	}

	{
		glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
		glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );

		ScopedShaderBinding shader( shadeShader.GetGLID() );
		ScopedSamplerActivation s0( 0 );
		Scoped2DTextureBinding sheetTexture( sheet->TextureID() );
		ScopedSamplerActivation s1( 1 );
		Scoped2DTextureBinding input( picture.Handle );

		shadeShader.Set( "Height", 0 );
		shadeShader.Set( "InputTexture", 1 );
		shadeShader.Set( "HWidth", static_cast< int >( sheet->GetWidth() ) );
		shadeShader.Set( "HHeight", static_cast< int >( sheet->GetHeight() ) );
		shadeShader.Set( "VpX", hostViewport[ 0 ] );
		shadeShader.Set( "VpY", hostViewport[ 1 ] );
		shadeShader.Set( "VpW", hostViewport[ 2 ] );
		shadeShader.Set( "VpH", hostViewport[ 3 ] );

		shadeShader.Set( "Depth", static_cast< float >( depth ) );
		shadeShader.Set( "Lamp", static_cast< float >( lampPx[ 0 ] ), static_cast< float >( lampPx[ 1 ] ), static_cast< float >( lampPx[ 2 ] ) );
		shadeShader.Set( "LampRef", static_cast< float >( lampRef ) );
		shadeShader.Set( "Intensity", static_cast< float >( controls::IntensityFromParam( params[ PT_INTENSITY ] ) ) );
		shadeShader.Set( "Ambient", params[ PT_AMBIENT ] );
		shadeShader.Set( "Shadows", params[ PT_SHADOWS ] );
		shadeShader.Set( "Alpha", static_cast< float >( controls::RoughnessAlphaFromParam( params[ PT_ROUGHNESS ] ) ) );
		shadeShader.Set( "Patina", params[ PT_PATINA ] );
		if( colourMode == controls::kColourMetal )
		{
			shadeShader.Set( "F0", static_cast< float >( f0.r ), static_cast< float >( f0.g ), static_cast< float >( f0.b ) );
			shadeShader.Set( "DiffuseWeight", static_cast< float >( controls::kMetalDiffuse ) );
		}
		else
		{
			const float paint = static_cast< float >( controls::kPaintF0 );
			shadeShader.Set( "F0", paint, paint, paint );
			shadeShader.Set( "DiffuseWeight", 1.0f );
		}
		shadeShader.Set( "Tint", static_cast< float >( f0.r ), static_cast< float >( f0.g ), static_cast< float >( f0.b ) );
		shadeShader.Set( "ColourMode", colourMode );
		shadeShader.Set( "Steps", controls::kShadowSteps );
		shadeShader.Set( "Bias", static_cast< float >( controls::kShadowBias ) );
		shadeShader.Set( "AoCount", controls::kAoRadii );
		//FFGLShader::Set has no array overload.
		glUniform1fv( glGetUniformLocation( shadeShader.GetGLID(), "AoRadius" ), controls::kAoRadii, controls::kAoRadiusPx );
		shadeShader.Set( "ShowHeight", params[ PT_SHOW_HEIGHT ] >= 0.5f ? 1 : 0 );
		shadeShader.Set( "MixAmount", params[ PT_MIX ] );
		shadeShader.Set( "Perturb", perturb );
		quad.Draw();
	}

	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult Repousse::DeInitGL()
{
	heightShader.FreeGLResources();
	blurShader.FreeGLResources();
	morphShader.FreeGLResources();
	shadeShader.FreeGLResources();
	quad.Release();

	height.Destroy();
	pingA.Destroy();
	pingB.Destroy();
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult Repousse::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( index - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;

	if( index == PT_KICK )
	{
		//An event arrives as 1.0 on press and 0.0 on release; a kick is the
		//press. A host that never sends the release still kicks once.
		if( value >= 0.5f && lastKickValue < 0.5f )
			kickPending = true;
		lastKickValue = value;
	}

	params[ index ] = value;
	return FF_SUCCESS;
}

float Repousse::GetFloatParameter( unsigned int index )
{
	if( index >= PT_COUNT )
		return 0.0f;
	return params[ index ];
}

char* Repousse::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_FIRST )
	{
		aboutText = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutText.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult Repousse::SetTextParameter( unsigned int index, const char* value )
{
	// See the declaration: the base class fails, and a failed default deletes
	// the instance. The About line is display-only; it has to say so
	// successfully.
	if( index == PT_ABOUT_FIRST )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult Repousse::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}
