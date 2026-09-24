/**
	rptest -- render Repousse offline, and read the physics back out of it.

	How bright a plane of known slope is under a point lamp, where the
	highlight on a hemisphere sits, what a metal reflects at normal
	incidence, how long the shadow behind a step is, how much of a narrow
	ridge the punch leaves, and how fast a kicked lamp swings are facts with
	one right answer each. Every check here drives the REAL plugin class
	through a headless GL context and measures the answer out of the picture
	it made:

		rptest --out /tmp/frame.png     a picture, on the moving relief card
		rptest --list                   every parameter, its kind and default
		rptest --lambert                planes of known slope read
		                                Intensity ( Lref / r )^2 n.l kd F0 under
		                                a grid of lamps
		rptest --specular               the highlight on a hemisphere peaks on
		                                the pixel holding the half-vector point
		                                (whole) or within the lattice's half
		                                pixel of it (fractional)
		rptest --fresnel                at normal incidence, each metal reflects
		                                its tabulated F0
		rptest --shadow                 the shadow behind a step is
		                                h ( xe - Lx ) / ( Lz - h ), within one
		                                march step and half a pixel
		rptest --punch                  a ridge narrower than the punch comes out
		                                min( H, d^2 / 2R ) tall, a wider one H
		rptest --pendulum               a kicked lamp's period, from its position
		                                in the picture, is 2 pi sqrt( L / g )
		                                with the large-angle series; its decay
		                                matches Q
		rptest --prime                  loud audio from the first frame does not
		                                kick; a step does; a scrub re-primes
		rptest --resize                 a resize mid-run keeps the lamp swinging
		                                and leaves no stale buffer
		rptest --negative               every check above can FAIL
		rptest --offline                the checks that need no GL
		rptest --bench                  the render cost
		rptest --dump-shaders DIR       the exact GLSL the plugin compiles
		rptest --pipe                   raw frames in, raw frames out

	Every check takes --size; run each at the raster you care about and at
	320x180, which is CI's. AGENTS.md has one line per check on where each
	tolerance comes from.

	The control mappings, the metal table, the lamp law, the punch law and
	the pendulum's period are stated HERE, from the README's definitions, and
	never read out of Controls.h or the shaders: a constant typed wrong there
	has to show up as a failed check, not as an agreement.
*/

#include "Audio.h"
#include "Clock.h"
#include "Controls.h"
#include "Pendulum.h"
#include "Repousse.h"
#include "Shaders.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{
namespace pb = repousse::perturb;

int g_checks   = 0;
int g_failures = 0;

constexpr double kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// A PNG writer. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( std::vector< unsigned char >& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

bool writePng( const std::string& path, int width, int height, const std::vector< unsigned char >& rgba )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = 0; y < height; ++y )
	{
		raw.push_back( 0 );
		const unsigned char* row = rgba.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	std::vector< unsigned char > ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.push_back( 8 );
	ihdr.push_back( 6 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	ihdr.push_back( 0 );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	FILE* file = fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = fwrite( png.data(), 1, png.size(), file );
	fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// The mappings and the constants, stated from the README -- not read out of
// Controls.h.
//---------------------------------------------------------------------------
namespace stated
{
double depthPx( double p ) { return 64.0 * p; }
double punchPx( double p ) { return 16.0 * p; }
double smoothSigma( double p ) { return 3.0 * p; }
double alpha( double p ) { return std::max( 1.0 / 64.0, p ); }
double lampHeight( double p ) { return std::max( 0.02, 2.0 * p ); }
double intensity( double p ) { return 2.0 * p; }
double cord( double p ) { return std::max( 0.05, 2.0 * p ); }
double zeta( double p ) { return std::max( 0.005, p ); }
double swing( double p ) { return 3.0 * p; }
constexpr double kGravity          = 9.80665;
constexpr double kMetresPerHeight  = 1.0;
constexpr double kMetalDiffuse     = 0.2;
constexpr double kPaintF0          = 0.04;
constexpr int kShadowSteps         = 32;
/// Real-Time Rendering 4th ed., Table 9.2, linear sRGB. Steel is iron.
struct Rgb
{
	double r, g, b;
};
const Rgb kF0[ 5 ] = { { 0.955, 0.638, 0.538 }, { 0.910, 0.778, 0.423 }, { 0.972, 0.960, 0.915 }, { 1.000, 0.782, 0.344 }, { 0.562, 0.565, 0.578 } };
const char* const kMetalName[ 5 ] = { "Copper", "Brass", "Silver", "Gold", "Steel" };
/// The detector: kicks are four times the rise above a 0.1 s baseline,
/// released in 0.3 s, gated at 0.15.
constexpr double kBaselineTime = 0.1;
constexpr double kRelease      = 0.3;
constexpr double kGain         = 4.0;
constexpr double kThreshold    = 0.15;
/// The large-angle period series, T / T0 = 1 + th^2/16 + 11 th^4/3072 + 173 th^6/737280.
double periodFactor( double th )
{
	const double t2 = th * th;
	return 1.0 + t2 / 16.0 + 11.0 * t2 * t2 / 3072.0 + 173.0 * t2 * t2 * t2 / 737280.0;
}
} // namespace stated

/// One unit in the last place of a float at magnitude v.
double ulpf( double v )
{
	const float f = static_cast< float >( std::fabs( v ) );
	return static_cast< double >( std::nextafter( f, INFINITY ) - f );
}

//---------------------------------------------------------------------------
// GL plumbing.
//---------------------------------------------------------------------------
CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute software[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};

	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( software, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}

	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;

	CGLSetCurrentContext( context );
	return context;
}

GLuint makeTexture( int width, int height, GLint internalFormat, GLenum type, const void* pixels )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, width, height, 0, GL_RGBA, type, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

GLuint makeFramebuffer( GLuint texture )
{
	GLuint fbo = 0;
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
	return fbo;
}

template< typename T >
std::vector< T > flipRows( const std::vector< T >& image, int width, int height )
{
	std::vector< T > flipped( image.size() );
	const size_t stride = static_cast< size_t >( width ) * 4;
	for( int y = 0; y < height; ++y )
		std::copy( image.begin() + static_cast< long >( ( height - 1 - y ) * stride ),
		           image.begin() + static_cast< long >( ( height - y ) * stride ),
		           flipped.begin() + static_cast< long >( y * stride ) );
	return flipped;
}

//---------------------------------------------------------------------------
// Parameters by display name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	unsigned int type;
	float value;
	float low;
	float high;
};

const char* kindName( const NamedParameter& p )
{
	if( p.index >= Repousse::PT_ABOUT_FIRST )
		return "about";
	switch( p.type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_INTEGER: return "integer";
	case FF_TYPE_BUFFER: return "buffer";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_RED: return "red";
	case FF_TYPE_GREEN: return "green";
	case FF_TYPE_BLUE: return "blue";
	default: return "other";
	}
}

std::vector< NamedParameter > listParameters( Repousse& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < Repousse::PT_COUNT; ++i )
	{
		const char* const name = plugin.GetParamName( i );
		NamedParameter p;
		p.name  = name ? name : "?";
		p.index = i;
		p.type  = plugin.GetParamType( i );
		p.value = plugin.GetFloatParameter( i );
		p.low   = 0.0f;
		p.high  = 1.0f;
		//An option's range reads back 0..1 whatever its element count, so
		//the element count is the range. An integer's range is its own.
		if( p.type == FF_TYPE_OPTION )
			p.high = static_cast< float >( std::max( 1u, plugin.GetNumParamElements( i ) ) - 1u );
		else if( p.type == FF_TYPE_INTEGER )
		{
			const RangeStruct r = plugin.GetParamRange( i );
			p.low               = r.min;
			p.high              = r.max;
		}
		list.push_back( p );
	}
	return list;
}

int indexOfParameter( Repousse& plugin, const std::string& name )
{
	for( const NamedParameter& p : listParameters( plugin ) )
		if( p.name == name )
			return static_cast< int >( p.index );
	return -1;
}

bool applySetting( Repousse& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.rfind( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name = assignment.substr( 0, equals );
	const int index        = indexOfParameter( plugin, name );
	if( index < 0 )
	{
		error = "no parameter called '" + name + "'";
		return false;
	}
	plugin.SetFloatParameter( static_cast< unsigned int >( index ), std::strtof( assignment.substr( equals + 1 ).c_str(), nullptr ) );
	return true;
}

bool set( Repousse& plugin, const char* name, float value )
{
	std::string error;
	char buffer[ 64 ];
	std::snprintf( buffer, sizeof( buffer ), "%.9g", value );
	if( applySetting( plugin, std::string( name ) + "=" + buffer, error ) )
		return true;
	std::fprintf( stderr, "%s\n", error.c_str() );
	return false;
}

//---------------------------------------------------------------------------
// A session: the plugin, its input and output, and a 60 fps clock.
//---------------------------------------------------------------------------
struct Session
{
	Repousse plugin;
	int width        = 0;
	int height       = 0;
	double fps       = 60.0;
	bool floatOutput = true;

	GLuint sourceTexture = 0;
	GLuint outputTexture = 0;
	GLuint outputFBO     = 0;
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };
	ProcessOpenGLStruct process    = {};

	void makeTargets()
	{
		sourceTexture = makeTexture( width, height, GL_RGBA32F, GL_FLOAT, nullptr );
		outputTexture = floatOutput ? makeTexture( width, height, GL_RGBA32F, GL_FLOAT, nullptr )
		                            : makeTexture( width, height, GL_RGBA8, GL_UNSIGNED_BYTE, nullptr );
		outputFBO     = makeFramebuffer( outputTexture );

		inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
		inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
		inputStruct.Handle                              = sourceTexture;
		inputs[ 0 ]                                     = &inputStruct;

		process.numInputTextures = 1;
		process.inputTextures    = inputs;
		process.HostFBO          = outputFBO;
	}

	void dropTargets()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
		outputFBO = outputTexture = sourceTexture = 0;
	}

	bool begin( int w, int h )
	{
		width  = w;
		height = h;
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( width );
		viewport.height             = static_cast< FFUInt32 >( height );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see the diagnostics log for which shader\n" );
			return false;
		}
		makeTargets();
		return true;
	}

	/// What a host does when the clip changes size: the SAME instance handed
	/// a differently sized input. No DeInitGL.
	void resize( int w, int h )
	{
		dropTargets();
		width  = w;
		height = h;
		makeTargets();
	}

	/// Every bin of the spectrum, as a host would set them.
	void spectrum( const std::vector< float >& bins )
	{
		for( size_t i = 0; i < bins.size(); ++i )
			plugin.SetParamElementValue( Repousse::PT_AUDIO_FFT, static_cast< unsigned int >( i ), bins[ i ] );
	}

	bool renderAtTime( double seconds )
	{
		plugin.SetClockScaleForTest( 1.0 );
		plugin.SetTime( seconds );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glViewport( 0, 0, width, height );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		glClear( GL_COLOR_BUFFER_BIT );
		const bool ok = plugin.ProcessOpenGL( &process ) == FF_SUCCESS;
		if( !ok )
			std::fprintf( stderr, "ProcessOpenGL failed at t = %.4f\n", seconds );
		return ok;
	}

	bool renderAt( int64_t frame )
	{
		return renderAtTime( static_cast< double >( frame ) / fps );
	}

	void upload( const std::vector< unsigned char >& pixels )
	{
		const std::vector< unsigned char > flipped = flipRows( pixels, width, height );
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, flipped.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void upload( const std::vector< float >& pixels )
	{
		const std::vector< float > flipped = flipRows( pixels, width, height );
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, flipped.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	bool render( int64_t frame, const std::vector< unsigned char >& pixels )
	{
		upload( pixels );
		return renderAt( frame );
	}

	bool render( int64_t frame, const std::vector< float >& pixels )
	{
		upload( pixels );
		return renderAt( frame );
	}

	std::vector< unsigned char > readBack()
	{
		std::vector< unsigned char > pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		return flipRows( pixels, width, height );
	}

	/// The whole output, top row first, as floats.
	std::vector< float > readBackFloat()
	{
		std::vector< float > pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return flipRows( pixels, width, height );
	}

	void end()
	{
		plugin.DeInitGL();
		dropTargets();
	}
};

const char* verdict( bool ok )
{
	return ok ? "ok" : "FAIL";
}

int report( bool ok, bool quiet, const char* format, ... ) __attribute__( ( format( printf, 3, 4 ) ) );
int report( bool ok, bool quiet, const char* format, ... )
{
	++g_checks;
	if( !ok )
		++g_failures;
	//Quiet is a negative control's run: its failures are the point, and the
	//summary line says so. --perturb runs the same thing verbosely.
	if( !quiet )
	{
		va_list args;
		va_start( args, format );
		std::vprintf( format, args );
		va_end( args );
		std::printf( "  %s\n", verdict( ok ) );
	}
	return ok ? 0 : 1;
}

/// A grey picture whose value at each pixel is v( x, up ), alpha 1, top row
/// first; `up` is the row counted from the bottom, as GL counts it. The
/// plugin's height is Depth times the Rec. 709 luma of the grey, which is v
/// to a few float ulps.
std::vector< float > relief( int width, int height, const std::function< double( int, int ) >& v )
{
	std::vector< float > img( static_cast< size_t >( width ) * height * 4 );
	for( int r = 0; r < height; ++r )
		for( int x = 0; x < width; ++x )
		{
			const float g = static_cast< float >( std::clamp( v( x, height - 1 - r ), 0.0, 1.0 ) );
			float* px     = img.data() + ( static_cast< size_t >( r ) * width + x ) * 4;
			px[ 0 ] = px[ 1 ] = px[ 2 ] = g;
			px[ 3 ]                     = 1.0f;
		}
	return img;
}

/// The parameter values every check starts from: a 16 px relief, no punch,
/// silver at alpha 0.5, no patina, the lamp in the middle half a frame up at
/// Intensity 1, no ambient, shadows on, the pendulum at rest with a half
/// metre cord, no mix. Each check moves what it is about.
struct Baseline
{
	float depth      = 0.25f;//16 px
	float invert     = 0.0f;
	float punch      = 0.0f;
	float smooth     = 0.0f;
	float metal      = 2.0f;//Silver
	float roughness  = 0.5f;//alpha 0.5: 4 alpha^2 = 1
	float patina     = 0.0f;
	float colour     = 0.0f;//Metal
	float lampX      = 0.5f;
	float lampY      = 0.5f;
	float lampHeight = 0.25f;//0.5 frame heights
	float intensity  = 0.5f; //1
	float ambient    = 0.0f;
	float shadows    = 1.0f;
	float cord       = 0.25f;//0.5 m
	float damping    = 0.05f;
	float swing      = 0.0f;
	float mix        = 1.0f;
	float showHeight = 0.0f;
};

void apply( Repousse& p, const Baseline& b )
{
	set( p, "Depth", b.depth );
	set( p, "Invert", b.invert );
	set( p, "Punch Radius", b.punch );
	set( p, "Smooth", b.smooth );
	set( p, "Metal", b.metal );
	set( p, "Roughness", b.roughness );
	set( p, "Patina", b.patina );
	set( p, "Colour", b.colour );
	set( p, "Lamp X", b.lampX );
	set( p, "Lamp Y", b.lampY );
	set( p, "Lamp Height", b.lampHeight );
	set( p, "Intensity", b.intensity );
	set( p, "Ambient", b.ambient );
	set( p, "Shadows", b.shadows );
	set( p, "Cord Length", b.cord );
	set( p, "Damping", b.damping );
	set( p, "Swing", b.swing );
	set( p, "Mix", b.mix );
	set( p, "Show Height", b.showHeight );
}

/// Render one still relief and read it back. The sheet is a pure function
/// of the frame apart from the lamp's swing, so one frame is the picture.
bool renderStill( int width, int height, const Baseline& b, int perturb, const std::vector< float >& img, std::vector< float >& out )
{
	Session s;
	apply( s.plugin, b );
	s.plugin.SetPerturbForTest( perturb );
	if( !s.begin( width, height ) )
		return false;
	const bool ok = s.render( 0, img );
	if( ok )
		out = s.readBackFloat();
	s.end();
	return ok;
}

/// Pixel ( x, up ) of a top-first float readback, channel ch.
double at( const std::vector< float >& out, int width, int height, int x, int up, int ch = 0 )
{
	return out[ ( static_cast< size_t >( height - 1 - up ) * width + x ) * 4 + ch ];
}

/// The brightest pixel (red channel) and its parabolic sub-pixel refinement
/// along x and up, in pixel-centre coordinates (pixel x's centre is x + 0.5).
struct Peak
{
	int px, py;
	double x, y;
	double value;
};

Peak brightest( const std::vector< float >& out, int width, int height )
{
	Peak p { 0, 0, 0.0, 0.0, -1.0 };
	for( int up = 0; up < height; ++up )
		for( int x = 0; x < width; ++x )
		{
			const double v = at( out, width, height, x, up );
			if( v > p.value )
			{
				p.value = v;
				p.px    = x;
				p.py    = up;
			}
		}
	auto vertex = [ & ]( double a, double b, double c ) {
		const double d = a - 2.0 * b + c;
		return d < 0.0 ? 0.5 * ( a - c ) / d : 0.0;
	};
	p.x = p.px + 0.5;
	p.y = p.py + 0.5;
	if( p.px > 0 && p.px < width - 1 )
		p.x += vertex( at( out, width, height, p.px - 1, p.py ), p.value, at( out, width, height, p.px + 1, p.py ) );
	if( p.py > 0 && p.py < height - 1 )
		p.y += vertex( at( out, width, height, p.px, p.py - 1 ), p.value, at( out, width, height, p.px, p.py + 1 ) );
	return p;
}

//---------------------------------------------------------------------------
// --lambert
//
// Planes h = c + a x + b y (pixels of height over pixels), specular off,
// no ambient, no patina, so every pixel IS the lamp's law:
//
//     out = Intensity ( Lref / r )^2 max( 0, n.l ) kd F0
//
// with n = normalize( -a, -b, 1 ), r the distance from the pixel's centre
// at its height to the lamp, Lref the lamp's nominal height. A grid of lamp
// positions and heights over four planes.
//
// Tolerance: the plane's heights reach the shader as floats (the picture's
// value, the luma dot, the multiply by Depth: three roundings, 3 ulp of
// h_max per sample); a central difference over two pixels carries that
// straight into each gradient component; |dn/dg| <= 1 lifts it into n.l;
// times the largest irradiance the frame can carry, plus 64 ulps for the
// normalize, the dot, r, the division and the products.
//---------------------------------------------------------------------------
int runLambert( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures = 0;
	Baseline b;
	const double D  = stated::depthPx( b.depth );
	const double I  = stated::intensity( b.intensity );
	const double kd = stated::kMetalDiffuse;
	const stated::Rgb f0 = stated::kF0[ 2 ];//Silver

	//Planes by their rise across the frame, as a fraction of Depth, so the
	//same four exist at every raster (the slope in height per pixel is then
	//raster-dependent, and is printed).
	//The last plane goes through Smooth at sigma 2 px (radius 6): a
	//normalised kernel leaves a plane a plane, so the lamp's law must hold
	//unchanged wherever the kernel does not reach the clamped border.
	struct Plane
	{
		double riseX, riseY;//fraction of Depth across the width, the height
		float smooth;
		int margin;
	} const planes[] = { { 0.0, 0.0, 0.0f, 0 }, { 0.8, 0.0, 0.0f, 0 }, { 0.0, -0.6, 0.0f, 0 }, { 0.45, 0.45, 0.0f, 0 }, { 0.5, -0.5, 2.0f / 3.0f, 7 } };
	struct LampCase
	{
		float x, y, h;
	} const lamps[] = { { 0.5f, 0.5f, 0.25f }, { 0.2f, 0.8f, 0.25f }, { 0.8f, 0.3f, 0.5f }, { 0.5f, 0.5f, 0.1f }, { 0.1f, 0.1f, 0.4f } };

	for( const Plane& plane : planes )
	{
		struct
		{
			double a, b;
		} const pl = { plane.riseX * D / ( width - 1 ), plane.riseY * D / ( height - 1 ) };
		//The plane's lowest corner at 0: the picture's value is h / D in 0..1.
		const double lo = std::min( { 0.0, pl.a * ( width - 1 ), pl.b * ( height - 1 ), pl.a * ( width - 1 ) + pl.b * ( height - 1 ) } );
		const double c  = -lo;
		const double hmax = c + std::max( 0.0, pl.a * ( width - 1 ) ) + std::max( 0.0, pl.b * ( height - 1 ) );
		if( hmax > D )
			return report( false, quiet, "lambert: plane rise (%.2f, %.2f) of Depth does not fit under Depth", plane.riseX, plane.riseY );
		std::vector< float > img = relief( width, height, [ & ]( int x, int y ) { return ( c + pl.a * x + pl.b * y ) / D; } );
		const double nx = -pl.a, ny = -pl.b, nz = 1.0;
		const double nl = std::sqrt( nx * nx + ny * ny + nz * nz );

		int grid = 0;
		double worst = 0.0, worstTol = 0.0;
		for( const LampCase& lc : lamps )
		{
			Baseline bb  = b;
			bb.lampX     = lc.x;
			bb.lampY     = lc.y;
			bb.lampHeight = lc.h;
			bb.smooth    = plane.smooth;
			std::vector< float > out;
			if( !renderStill( width, height, bb, perturb | pb::kNoSpecular, img, out ) )
				return failures + 1;
			const double Lx = static_cast< double >( lc.x ) * width, Ly = static_cast< double >( lc.y ) * height;
			const double Lref = stated::lampHeight( lc.h ) * height;
			const double tol  = I * kd * 1.0 * 3.0 * std::sqrt( 2.0 ) * ulpf( hmax ) + 64.0 * std::ldexp( 1.0, -24 );
			for( int up = plane.margin; up < height - plane.margin; ++up )
				for( int x = plane.margin; x < width - plane.margin; ++x )
				{
					const double h  = c + pl.a * x + pl.b * up;
					const double dx = Lx - ( x + 0.5 ), dy = Ly - ( up + 0.5 ), dz = Lref - h;
					const double r2 = dx * dx + dy * dy + dz * dz;
					const double ndl = std::max( 0.0, ( nx * dx + ny * dy + nz * dz ) / ( nl * std::sqrt( r2 ) ) );
					const double E   = I * Lref * Lref / r2;
					const double ch[ 3 ] = { f0.r, f0.g, f0.b };
					for( int k = 0; k < 3; ++k )
					{
						const double e = std::fabs( at( out, width, height, x, up, k ) - E * ndl * kd * ch[ k ] );
						if( e > worst )
						{
							worst    = e;
							worstTol = tol;
						}
					}
				}
			++grid;
		}
		failures += report( worst <= ( worstTol > 0.0 ? worstTol : 1.0 ), quiet,
		                    "lambert: plane slope (%.4f, %.4f) px/px%s, %d lamps: worst error %.2e (tol %.2e) over every pixel and channel",
		                    pl.a, pl.b, plane.smooth > 0.0f ? " through Smooth (sigma 2 px)" : "", grid, worst, worstTol );
	}
	return failures;
}

//---------------------------------------------------------------------------
// --specular
//
// A hemisphere of radius R = 64 px (Depth 64, the picture the cap's height
// over R). Diffuse off. For a chosen surface point P with normal n, the
// half-vector point is where n bisects the lamp and the viewer, so the lamp
// must lie along the mirror direction 2 ( n.v ) n - v from P; the harness
// places it there at a chosen height, and the highlight must peak at P.
//
//   whole        P is a pixel centre: the brightest pixel must be that one,
//                exactly. The lobe's peak is displaced from the half-vector
//                point by the smooth factors (1/r^2, n.l, Smith, Fresnel) by
//                at most a^2 G / 4 radians (the GGX lobe's log-slope is
//                4 theta / a^2 near its axis; G <= 4 per radian for a lamp
//                at least 45 degrees up and at least 2R away), which on a
//                sphere of radius R with the lamp at least 2R away is at
//                most a^2 4R / 3 pixels: 0.083 px at a = 1/32. Under half a
//                pixel, so the pixel is exact.
//   fractional   P is a pixel centre plus (0.3, 0.2): the brightest pixel is
//                within half a pixel of P, plus that shift.
//
// The negative control evaluates the lobe on n.l instead of n.h: the peak
// moves to the point whose normal points AT the lamp, half the lamp's
// zenith angle away.
//---------------------------------------------------------------------------
int runSpecular( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures = 0;
	const double R = 64.0;
	if( height < 2 * R + 4 || width < 2 * R + 4 )
		return report( false, quiet, "specular: raster %dx%d too small for a %g px hemisphere", width, height, R );
	Baseline b;
	b.depth     = 1.0f;//64 px
	b.roughness = 1.0f / 32.0f;
	b.shadows   = 0.0f;
	const double a       = stated::alpha( b.roughness );
	const double shiftPx = a * a * 4.0 * R / 3.0;

	struct Case
	{
		const char* kind;
		double px, py;      //surface point from the dome's centre, pixels
		double lampZ;       //the lamp's height in PIXELS, so the geometry is the same at every raster
	};
	const double cx = std::floor( width / 2.0 ) + 0.5, cy = std::floor( height / 2.0 ) + 0.5;//dome centre on a pixel centre
	//Lamp heights put the lamp at least 2R = 128 px from P, which the shift
	//bound assumes (and the check asserts).
	const Case cases[] = {
		{ "whole-px", 18.0, 0.0, 216.0 },   { "whole-px", 12.0, 12.0, 234.0 },   { "whole-px", -12.0, 8.0, 198.0 },
		{ "fractional", 18.3, 0.2, 216.0 }, { "fractional", 11.7, 12.4, 234.0 }, { "fractional", -11.6, 7.7, 198.0 },
	};
	std::vector< float > img = relief( width, height, [ & ]( int x, int y ) {
		const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
		const double r2 = dx * dx + dy * dy;
		return r2 < R * R ? std::sqrt( 1.0 - r2 / ( R * R ) ) : 0.0;
	} );
	for( const Case& c : cases )
	{
		const double Px = cx + c.px, Py = cy + c.py;
		const double dx = c.px, dy = c.py;
		const double Pz = std::sqrt( R * R - dx * dx - dy * dy );
		//The sphere's normal, and the mirror of the viewer ( 0, 0, 1 ) in it.
		const double nx = dx / R, ny = dy / R, nz = Pz / R;
		const double rx = 2.0 * nz * nx, ry = 2.0 * nz * ny, rz = 2.0 * nz * nz - 1.0;
		const double Lz = c.lampZ;
		const double t  = ( Lz - Pz ) / rz;
		const double Lx = Px + t * rx, Ly = Py + t * ry;
		const double dist = std::sqrt( t * t );//|L - P| = t since r is unit
		const double elevation = std::asin( ( Lz - Pz ) / dist ) * 180.0 / kPi;
		Baseline bb    = b;
		bb.lampX       = static_cast< float >( Lx / width );
		bb.lampY       = static_cast< float >( Ly / height );
		bb.lampHeight  = static_cast< float >( Lz / height / 2.0 );
		//The lamp's stated height maps through max( 0.02, 2p ): p = Lz / 2H.
		if( Lx < 0.0 || Lx > width || Ly < 0.0 || Ly > height )
			return report( false, quiet, "specular: case puts the lamp off the frame (%.1f, %.1f)", Lx, Ly );
		std::vector< float > out;
		if( !renderStill( width, height, bb, perturb | pb::kNoDiffuse, img, out ) )
			return failures + 1;
		//The flat sheet directly beneath the lamp is a second, correct
		//highlight (n = l = v there). This check is about the dome, so the
		//search stays on it: everything off the dome is blacked out first.
		for( int up = 0; up < height; ++up )
			for( int x = 0; x < width; ++x )
			{
				const double ddx = x + 0.5 - cx, ddy = up + 0.5 - cy;
				if( ddx * ddx + ddy * ddy >= ( R - 3.0 ) * ( R - 3.0 ) )
					out[ ( static_cast< size_t >( height - 1 - up ) * width + x ) * 4 ] = 0.0f;
			}
		const Peak pk = brightest( out, width, height );
		const bool whole = std::strcmp( c.kind, "whole-px" ) == 0;
		bool ok;
		double off = std::hypot( pk.px + 0.5 - Px, pk.py + 0.5 - Py );
		if( whole )
			ok = pk.px == static_cast< int >( std::floor( Px ) ) && pk.py == static_cast< int >( std::floor( Py ) );
		else
			ok = std::fabs( pk.px + 0.5 - Px ) <= 0.5 + shiftPx && std::fabs( pk.py + 0.5 - Py ) <= 0.5 + shiftPx;
		failures += report( ok && dist >= 2.0 * R && elevation >= 45.0, quiet,
		                    "specular: %-10s half-vector point (%.1f, %.1f), lamp %.0f px away at %.0f deg: brightest pixel (%d, %d), %.2f px off (shift bound %.3f px%s)",
		                    c.kind, Px, Py, dist, elevation, pk.px, pk.py, off, shiftPx, whole ? ", pixel must be exact" : " + half a pixel" );
	}
	return failures;
}

//---------------------------------------------------------------------------
// --fresnel
//
// A flat black sheet (h = 0), the lamp directly above a pixel at its nominal
// height, diffuse off, no ambient. There n = l = v = h, r = Lref, so
//
//     out = Intensity F0 / ( 4 alpha^2 )
//
// (D = 1 / pi alpha^2, Smith's V = 1/4, the lamp's factor 1). At alpha 0.5
// that is Intensity F0 per channel; at alpha 0.25 four times it. Every
// metal. Tolerance 32 ulps: the lamp lands within 1e-4 px of the pixel's
// centre (the position is a float parameter), so every cosine is 1 to well
// under an ulp, and what is left is pi, the division and the products.
//---------------------------------------------------------------------------
int runFresnel( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures = 0;
	std::vector< float > img = relief( width, height, []( int, int ) { return 0.0; } );
	const int px = width / 2, py = height / 2;
	for( int metal = 0; metal < 5; ++metal )
		for( const float rough : { 0.5f, 0.25f } )
		{
			Baseline b;
			b.metal     = static_cast< float >( metal );
			b.roughness = rough;
			b.lampX     = static_cast< float >( ( px + 0.5 ) / width );
			b.lampY     = static_cast< float >( ( py + 0.5 ) / height );
			b.intensity = 0.25f;//0.5
			std::vector< float > out;
			if( !renderStill( width, height, b, perturb | pb::kNoDiffuse, img, out ) )
				return failures + 1;
			const double a     = stated::alpha( rough );
			const double I     = stated::intensity( b.intensity );
			const double scale = I / ( 4.0 * a * a );
			const stated::Rgb f0 = stated::kF0[ metal ];
			const double want[ 3 ] = { f0.r * scale, f0.g * scale, f0.b * scale };
			double worst = 0.0;
			double got[ 3 ];
			for( int k = 0; k < 3; ++k )
			{
				got[ k ] = at( out, width, height, px, py, k );
				worst    = std::max( worst, std::fabs( got[ k ] - want[ k ] ) );
			}
			const double tol = 32.0 * std::ldexp( 1.0, -24 ) * std::max( 1.0, scale );
			failures += report( worst <= tol, quiet, "fresnel: %-6s alpha %.2f: F0 read back (%.4f, %.4f, %.4f), stated (%.3f, %.3f, %.3f), worst %.2e (tol %.2e)",
			                    stated::kMetalName[ metal ], a, got[ 0 ] / scale, got[ 1 ] / scale, got[ 2 ] / scale, f0.r, f0.g, f0.b, worst, tol );
		}
	return failures;
}

//---------------------------------------------------------------------------
// --shadow
//
// A step: h for x < x0, 0 from x0 on, flat along y. The surface between
// texels is bilinear, so the cliff's top edge is the CENTRE of the last high
// texel, xe = x0 - 0.5. A lamp at ( Lx, Ly, Lz ) on the high side. A ground
// point ( x, y ) is in shadow while the ray to the lamp passes under the
// edge, which ends at
//
//     x_end = xe + h ( xe - Lx ) / ( Lz - h )
//
// on every row -- the edge is a line, so the boundary is one. That is
// h / tan( theta ) for theta the lamp's elevation seen from the edge. Shadow
// is measured as the run of exactly-zero pixels from x0 along three rows
// (no ambient: a shadowed pixel is 0.0 and a lit one is not).
//
// Tolerance: the march samples the ray at Steps points between the pixel
// and where the ray clears Depth, so the boundary is placed within one
// step's horizontal length, fD |d_xy| / Steps, at the boundary pixel; plus
// half a pixel for the pixel lattice the boundary is read from.
//---------------------------------------------------------------------------
int runShadow( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures = 0;
	struct Case
	{
		float depth;     //h
		double back;     //lamp this many px behind the edge
		float lampZ;     //frame heights
	} const cases[] = { { 0.25f, 100.0, 0.25f }, { 0.125f, 60.0, 0.25f }, { 0.5f, 120.0, 0.3f }, { 0.25f, 40.0, 0.15f } };
	const int x0 = width * 2 / 3;
	for( const Case& c : cases )
	{
		const double h  = stated::depthPx( c.depth );
		const double xe = x0 - 0.5;
		const double Lx = xe - c.back;
		if( Lx < 1.0 )
			continue;
		const double Lz = stated::lampHeight( c.lampZ ) * height;
		Baseline b;
		b.depth      = c.depth;
		b.lampX      = static_cast< float >( Lx / width );
		b.lampY      = 0.5f;
		b.lampHeight = c.lampZ;
		std::vector< float > img = relief( width, height, [ & ]( int x, int ) { return x < x0 ? 1.0 : 0.0; } );
		std::vector< float > out;
		if( !renderStill( width, height, b, perturb, img, out ) )
			return failures + 1;

		const double Ly    = 0.5 * height;
		const double xEnd  = xe + h * ( xe - Lx ) / ( Lz - h );
		const double theta = std::atan2( Lz - h, xe - Lx ) * 180.0 / kPi;
		bool all = true;
		double worstOff = 0.0, worstTol = 0.0;
		int runAtRow = 0;
		for( const int up : { height / 2, height / 2 - height / 4, height / 2 + height / 4 } )
		{
			int n = 0;
			while( x0 + n < width && at( out, width, height, x0 + n, up ) == 0.0 )
				++n;
			const double boundary = x0 + n;
			//The march step at the boundary's two pixels: fD |d_xy| / Steps.
			const double dyy = Ly - ( up + 0.5 );
			double step = 0.0;
			for( const double xc : { boundary - 0.5, boundary + 0.5 } )
			{
				const double dxx = Lx - xc;
				step = std::max( step, ( h / Lz ) * std::hypot( dxx, dyy ) / stated::kShadowSteps );
			}
			const double tol = step + 0.5;
			const double off = std::fabs( boundary - xEnd );
			if( off > worstOff )
			{
				worstOff = off;
				worstTol = tol;
			}
			all = all && off <= tol && n > 0 && x0 + n < width;
			if( up == height / 2 )
				runAtRow = n;
		}
		failures += report( all, quiet, "shadow: step %4.1f px, lamp %.0f px back at %.0f px (%.1f deg): shadow %d px on the lamp's row, boundary stated %.2f, worst off %.2f over three rows (tol %.2f = step + half a pixel)",
		                    h, c.back, Lz, theta, runAtRow, xEnd - x0, worstOff, worstTol );
	}
	return failures;
}

//---------------------------------------------------------------------------
// --punch
//
// Ridges of width w and squares of side w at height H = Depth, the punch of
// radius R, Show Height on so the picture IS the worked relief in units of
// Depth. The opening by the paraboloid tip -k^2 / 2R on a shank of
// half-width R leaves a peak of
//
//     min( H, d^2 / 2R )   if d <= floor( R ),   H otherwise,
//
// where d = ceil( w / 2 ) is the distance from the feature's middle to the
// nearest outside pixel; the closing after it does not move a peak. A
// separate case is a groove in a plateau, whose floor after the open-close
// is compared with the same 1-D operation computed in double from the
// stated profile -- a check of the closing and the order, not of the law.
//
// Tolerance: every candidate is h + k^2 / 2R with k^2 exact and 1 / 2R
// exact at R = 4, one rounding per candidate per pass, eight passes: 8 ulps
// of Depth, over Depth.
//---------------------------------------------------------------------------
double statedOpenClose1D( const std::vector< double >& h, double R, int taps, int x )
{
	auto morph = [ & ]( const std::vector< double >& in, bool erode ) {
		std::vector< double > out( in.size() );
		const int n = static_cast< int >( in.size() );
		for( int i = 0; i < n; ++i )
		{
			double best = in[ static_cast< size_t >( i ) ];
			for( int k = 1; k <= taps; ++k )
			{
				const double b = k * k / ( 2.0 * R );
				const double a = in[ static_cast< size_t >( std::clamp( i - k, 0, n - 1 ) ) ];
				const double c = in[ static_cast< size_t >( std::clamp( i + k, 0, n - 1 ) ) ];
				best = erode ? std::min( best, std::min( a, c ) + b ) : std::max( best, std::max( a, c ) - b );
			}
			out[ static_cast< size_t >( i ) ] = best;
		}
		return out;
	};
	std::vector< double > g = morph( morph( h, true ), false );
	g                       = morph( morph( g, false ), true );
	return g[ static_cast< size_t >( x ) ];
}

int runPunch( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures = 0;
	Baseline b;
	b.depth      = 0.25f;//16 px
	b.punch      = 0.25f;//4 px
	b.showHeight = 1.0f;
	const double H = stated::depthPx( b.depth );
	const double R = stated::punchPx( b.punch );
	const int taps = static_cast< int >( std::floor( R ) );
	const double tol = 8.0 * ulpf( H ) / H;
	const int x0 = width / 3, y0 = height / 3;

	for( const int w : { 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15 } )
		for( const bool square : { false, true } )
		{
			std::vector< float > img = relief( width, height, [ & ]( int x, int y ) {
				const bool inX = x >= x0 && x < x0 + w;
				const bool inY = y >= y0 && y < y0 + w;
				return ( inX && ( !square || inY ) ) ? 1.0 : 0.0;
			} );
			std::vector< float > out;
			if( !renderStill( width, height, b, perturb, img, out ) )
				return failures + 1;
			double peak = 0.0;
			for( int up = 0; up < height; ++up )
				for( int x = 0; x < width; ++x )
					peak = std::max( peak, at( out, width, height, x, up ) );
			const int d = ( w + 1 ) / 2;
			const double stated = ( d <= taps ? std::min( H, d * d / ( 2.0 * R ) ) : H ) / H;
			failures += report( std::fabs( peak - stated ) <= tol, quiet, "punch: %s %2d px wide, R %.0f: peak %.6f of Depth, stated %.6f (%s), off %.1e (tol %.1e)",
			                    square ? "square" : "ridge ", w, R, peak, stated, d <= taps ? "narrower than the punch" : "wider: kept", std::fabs( peak - stated ), tol );
		}

	//A groove: a plateau at H with a w-wide slot to 0.
	for( const int w : { 3, 5, 9 } )
	{
		std::vector< double > line( static_cast< size_t >( width ), H );
		for( int x = x0; x < x0 + w; ++x )
			line[ static_cast< size_t >( x ) ] = 0.0;
		std::vector< float > img = relief( width, height, [ & ]( int x, int ) { return line[ static_cast< size_t >( x ) ] / H; } );
		std::vector< float > out;
		if( !renderStill( width, height, b, perturb, img, out ) )
			return failures + 1;
		const int mid = x0 + w / 2;
		const double got = at( out, width, height, mid, height / 2 );
		const double want = statedOpenClose1D( line, R, taps, mid ) / H;
		failures += report( std::fabs( got - want ) <= tol, quiet, "punch: groove %d px wide: floor %.6f of Depth, the stated open-close in double %.6f, off %.1e (tol %.1e)",
		                    w, got, want, std::fabs( got - want ), tol );
	}
	return failures;
}

//---------------------------------------------------------------------------
// The lamp's position, read out of a flat sheet: the brightest point is
// directly beneath it (both terms peak where n = l = v), refined by a
// parabola through the three brightest columns and rows.
//---------------------------------------------------------------------------
struct Trace
{
	std::vector< double > t, x, y;
};

/// Zero crossings of x - rest, linearly interpolated, and the signed peaks
/// between them.
struct Swing
{
	std::vector< double > crossings;//times, alternating direction
	std::vector< double > peaks;    //|amplitude| between successive crossings
	std::vector< double > peakTimes;
};

Swing analyse( const Trace& tr, double rest )
{
	Swing s;
	for( size_t i = 1; i < tr.t.size(); ++i )
	{
		const double a = tr.x[ i - 1 ] - rest, b = tr.x[ i ] - rest;
		if( ( a < 0.0 && b >= 0.0 ) || ( a > 0.0 && b <= 0.0 ) )
			s.crossings.push_back( tr.t[ i - 1 ] + ( tr.t[ i ] - tr.t[ i - 1 ] ) * a / ( a - b ) );
	}
	for( size_t k = 1; k < s.crossings.size(); ++k )
	{
		double best = 0.0, when = 0.0;
		for( size_t i = 0; i < tr.t.size(); ++i )
			if( tr.t[ i ] >= s.crossings[ k - 1 ] && tr.t[ i ] <= s.crossings[ k ] && std::fabs( tr.x[ i ] - rest ) > best )
			{
				best = std::fabs( tr.x[ i ] - rest );
				when = tr.t[ i ];
			}
		s.peaks.push_back( best );
		s.peakTimes.push_back( when );
	}
	return s;
}

/// The period and decay checks on a trace, shared by --pendulum (from the
/// picture) and --model (from the CPU model): L in metres, pxPerM the
/// scale of the trace's x, `periodTol` relative, `decayTol` absolute on the
/// log decrement.
int judgeSwing( const Trace& tr, double rest, double L, double zeta, double pxPerM, double periodTol, double decayFrac, bool quiet, const char* who, int& cycles )
{
	int failures = 0;
	const Swing s  = analyse( tr, rest );
	const double T0 = 2.0 * kPi * std::sqrt( L / stated::kGravity );
	cycles          = 0;
	double worst    = 0.0;
	for( size_t k = 0; k + 2 < s.crossings.size() && k + 1 < s.peaks.size(); k += 2 )
	{
		const double T   = s.crossings[ k + 2 ] - s.crossings[ k ];
		const double amp = 0.5 * ( s.peaks[ k ] + s.peaks[ k + 1 ] ) / pxPerM;//metres
		if( amp >= L )
			continue;
		const double th0  = std::asin( amp / L );
		const double want = T0 * stated::periodFactor( th0 ) / std::sqrt( 1.0 - zeta * zeta );
		worst             = std::max( worst, std::fabs( T / want - 1.0 ) );
		++cycles;
		if( !quiet && periodTol > 0.0 )
			std::printf( "%s: cycle %d: period %.5f s, amplitude %.4f rad, stated %.5f s (T0 %.5f, large-angle x%.5f): off %.1e\n", who, cycles, T, th0, want, T0,
			             stated::periodFactor( th0 ), std::fabs( T / want - 1.0 ) );
	}
	//A tolerance of 0 means "not this run's question": the other half is
	//judged on its own trace, at its own amplitude and damping.
	if( periodTol > 0.0 )
		failures += report( cycles >= 2 && worst <= periodTol, quiet, "%s: %d full cycles, period within %.1e of 2 pi sqrt( L / g ) with the large-angle series (tol %.1e)", who, cycles, worst, periodTol );
	if( decayFrac <= 0.0 )
		return failures;

	//Decay: the log decrement between successive same-sign peaks.
	const double wantDelta = 2.0 * kPi * zeta / std::sqrt( 1.0 - zeta * zeta );
	double worstDelta = 0.0;
	int pairs         = 0;
	double th0max     = 0.0;
	for( size_t k = 0; k + 2 < s.peaks.size(); ++k )
	{
		if( s.peaks[ k + 2 ] <= 0.0 )
			continue;
		const double delta = std::log( s.peaks[ k ] / s.peaks[ k + 2 ] );
		worstDelta         = std::max( worstDelta, std::fabs( delta - wantDelta ) );
		th0max             = std::max( th0max, std::asin( std::min( 1.0, s.peaks[ k ] / pxPerM / L ) ) );
		++pairs;
	}
	const double decayTol = wantDelta * th0max * th0max / 4.0 + decayFrac;
	failures += report( pairs >= 2 && worstDelta <= decayTol, quiet,
	                    "%s: log decrement over %d peak pairs within %.1e of 2 pi zeta / sqrt( 1 - zeta^2 ) = %.4f (Q %.1f; tol %.1e: zeta th0^2 / 4 nonlinearity + measurement)",
	                    who, pairs, worstDelta, wantDelta, 1.0 / ( 2.0 * zeta ), decayTol );
	return failures;
}

//---------------------------------------------------------------------------
// --pendulum
//
// A flat black sheet, no ambient, the lamp at rest in the middle; Kick
// pressed before the first frame along +x (the first kick's direction). The
// lamp's x is read out of every frame as the brightest point, for six
// seconds at 60 fps. Then:
//
//   period   between same-direction zero crossings, against
//            2 pi sqrt( L / g ) ( 1 + th^2/16 + 11 th^4/3072 + ... ) / sqrt( 1 - zeta^2 )
//            with th the cycle's mean amplitude read from the picture.
//            Swing 1 (3 rad/s on a 0.5 m cord) makes th ~ 0.68 rad: the
//            correction is 2.9%. Damping 0.01 so the amplitude barely
//            changes within a cycle.
//            Tolerance 3e-4: a crossing time is off by the position's error
//            (under 1e-3 px, from a parabola through a smooth even peak on
//            a float readback) over the velocity at the crossing, and by the
//            interpolation's cubic term (~1e-5 s); the series' next term is
//            2e-5 th^8; the decay across a cycle at zeta 0.01 moves the
//            mean-amplitude period by ~1e-5.
//   decay    the log decrement between successive same-sign peaks, Swing
//            0.2 (th ~ 0.13) and Damping 0.05 (Q 10), against
//            2 pi zeta / sqrt( 1 - zeta^2 ); tolerance the nonlinear
//            correction bound zeta th^2 / 4 plus 2e-3 (a sampled peak is low
//            by up to ( w dt )^2 / 8 = 6e-4 of itself, twice, in a log).
//   still    y never moves (the kick is along x), and the frame before the
//            kick took effect has the lamp at rest.
//---------------------------------------------------------------------------
Trace traceLamp( Session& s, const std::vector< float >& img, int frames )
{
	Trace tr;
	for( int f = 0; f < frames; ++f )
	{
		s.render( f, img );
		const Peak p = brightest( s.readBackFloat(), s.width, s.height );
		tr.t.push_back( f / s.fps );
		tr.x.push_back( p.x );
		tr.y.push_back( p.y );
	}
	return tr;
}

int runPendulum( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures = 0;
	std::vector< float > img = relief( width, height, []( int, int ) { return 0.0; } );
	const double pxPerM = height / stated::kMetresPerHeight;
	struct Case
	{
		const char* what;
		float swing, damping;
		double periodTol, decayFrac;
		bool judgePeriod, judgeDecay;
	} const cases[] = {
		{ "pendulum (period)", 1.0f, 0.01f, 3e-4, 0.0, true, false },
		{ "pendulum (decay) ", 0.2f, 0.05f, 0.0, 2e-3, false, true },
	};
	for( const Case& c : cases )
	{
		Session s;
		Baseline b;
		b.swing   = c.swing;
		b.damping = c.damping;
		apply( s.plugin, b );
		s.plugin.SetPerturbForTest( perturb );
		if( !s.begin( width, height ) )
			return failures + 1;
		//Pressed after InitGL, as an operator's press arrives.
		set( s.plugin, "Kick", 1.0f );
		const Trace tr = traceLamp( s, img, 360 );
		s.end();
		const double rest = 0.5 * width;
		double yDrift = 0.0;
		for( double y : tr.y )
			yDrift = std::max( yDrift, std::fabs( y - 0.5 * height ) );
		const double L    = stated::cord( b.cord );
		const double zeta = stated::zeta( c.damping );
		int cycles = 0;
		failures += judgeSwing( tr, rest, L, zeta, pxPerM, c.judgePeriod ? c.periodTol : 0.0, c.judgeDecay ? c.decayFrac : 0.0, quiet, c.what, cycles );
		failures += report( yDrift <= 0.01 && std::fabs( tr.x[ 0 ] - rest ) <= 0.01, quiet, "%s: the lamp's y never moves (%.4f px) and frame 0 has it at rest (%.4f px off)", c.what, yDrift,
		                    std::fabs( tr.x[ 0 ] - rest ) );
	}
	return failures;
}

//---------------------------------------------------------------------------
// --prime
//
//   primed   a loud constant spectrum from frame 0, Swing 1: the lamp does
//            not move in 60 frames. An unprimed detector kicks on frame 0.
//   step     silence, then loud from frame 30: the lamp is at rest through
//            frame 30 and away from it by frame 45.
//   scrub    loud and steady, the clock jumps back 5 s at frame 20: no kick.
// Tolerance: 0.01 px on a lamp that a kick moves by tens of pixels.
//---------------------------------------------------------------------------
int runPrime( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures = 0;
	std::vector< float > img = relief( width, height, []( int, int ) { return 0.0; } );
	std::vector< float > loud( 64, 0.64f ), silent( 64, 0.0f );
	const double rest = 0.5 * width;
	auto session = [ & ]( Session& s ) {
		Baseline b;
		b.swing = 1.0f;
		apply( s.plugin, b );
		s.plugin.SetPerturbForTest( perturb );
		return s.begin( width, height );
	};
	{
		Session s;
		if( !session( s ) )
			return failures + 1;
		s.spectrum( loud );
		double worst = 0.0;
		for( int f = 0; f < 60; ++f )
		{
			s.render( f, img );
			worst = std::max( worst, std::fabs( brightest( s.readBackFloat(), width, height ).x - rest ) );
		}
		failures += report( worst <= 0.01 && s.plugin.KicksForTest() == 0, quiet, "prime: loud from the first frame: the lamp stays within %.4f px of rest over 60 frames, %d kicks", worst,
		                    s.plugin.KicksForTest() );
		s.end();
	}
	{
		Session s;
		if( !session( s ) )
			return failures + 1;
		//The kick lands on the frame the hit arrives: the lamp is at rest on
		//frame 29 and already moving on frame 30 (one frame of a 3 rad/s
		//kick on a half-metre cord is 4.5 px at 180 rows).
		double at29 = 0.0, at30 = 0.0, at45 = 0.0;
		for( int f = 0; f <= 45; ++f )
		{
			s.spectrum( f < 30 ? silent : loud );
			s.render( f, img );
			const double x = brightest( s.readBackFloat(), width, height ).x - rest;
			if( f == 29 )
				at29 = x;
			if( f == 30 )
				at30 = x;
			if( f == 45 )
				at45 = x;
		}
		failures += report( std::fabs( at29 ) <= 0.01 && std::fabs( at30 ) > 0.5 && std::fabs( at45 ) > 10.0 && s.plugin.KicksForTest() == 1, quiet,
		                    "prime: a step at frame 30: at rest the frame before (%.4f px), %.1f px on it, %.1f px away fifteen frames later, %d kick", at29, at30, at45,
		                    s.plugin.KicksForTest() );
		s.end();
	}
	{
		Session s;
		if( !session( s ) )
			return failures + 1;
		s.spectrum( loud );
		double worst = 0.0;
		for( int f = 0; f < 40; ++f )
		{
			s.upload( img );
			s.renderAtTime( f < 20 ? 10.0 + f / 60.0 : 5.0 + f / 60.0 );
			worst = std::max( worst, std::fabs( brightest( s.readBackFloat(), width, height ).x - rest ) );
		}
		failures += report( worst <= 0.01, quiet, "prime: loud and steady across a scrub back 5 s: the lamp stays within %.4f px", worst );
		s.end();
	}
	return failures;
}

//---------------------------------------------------------------------------
// --resize
//
// The same instance handed a differently sized clip mid-run, as a host does:
//   - with the lamp at rest, the first frame after it is byte for byte a
//     fresh instance's render of the same picture at the new size, with the
//     punch, the patina and the shadows all on -- no buffer of the old size
//     survives into it;
//   - with the lamp swinging (Kick at frame 0), its position as a fraction
//     of the width follows an unresized run at the new size to within a
//     pixel of either raster: the pendulum is carried through.
//---------------------------------------------------------------------------
std::vector< float > hills( int width, int height )
{
	return relief( width, height, [ & ]( int x, int y ) {
		const double u = static_cast< double >( x ) / width, v = static_cast< double >( y ) / height;
		return 0.5 + 0.3 * std::sin( 7.0 * u + 2.0 * v ) * std::cos( 5.0 * v - u ) + 0.1 * u;
	} );
}

int runResize( int width, int height, int perturb = 0, bool quiet = false )
{
	int failures = 0;
	Baseline b;
	b.punch   = 0.25f;
	b.patina  = 0.6f;
	b.ambient = 0.1f;
	const int w2 = width * 3 / 2 + 1, h2 = height * 3 / 4 + 1;

	{
		Session s;
		apply( s.plugin, b );
		s.plugin.SetPerturbForTest( perturb );
		if( !s.begin( width, height ) )
			return 1;
		const std::vector< float > first = hills( width, height );
		for( int f = 0; f < 20; ++f )
			s.render( f, first );
		s.resize( w2, h2 );
		const std::vector< float > second = hills( w2, h2 );
		s.render( 20, second );
		const std::vector< float > after = s.readBackFloat();
		s.end();

		Session fresh;
		apply( fresh.plugin, b );
		if( !fresh.begin( w2, h2 ) )
			return 1;
		fresh.render( 0, second );
		const std::vector< float > expect = fresh.readBackFloat();
		fresh.end();

		size_t differ = after.size() == expect.size() ? 0 : after.size();
		for( size_t i = 0; i < std::min( after.size(), expect.size() ); ++i )
			differ += after[ i ] != expect[ i ] ? 1 : 0;
		failures += report( differ == 0, quiet, "resize: %dx%d -> %dx%d mid-run, lamp at rest: the first frame after it against a fresh instance: %zu of %zu values differ",
		                    width, height, w2, h2, differ, expect.size() );
	}
	{
		Baseline bb = b;
		bb.punch    = 0.0f;
		bb.patina   = 0.0f;
		bb.ambient  = 0.0f;
		bb.swing    = 1.0f;
		std::vector< float > flat1 = relief( width, height, []( int, int ) { return 0.0; } );
		std::vector< float > flat2 = relief( w2, h2, []( int, int ) { return 0.0; } );

		Session s;
		apply( s.plugin, bb );
		s.plugin.SetPerturbForTest( perturb );
		if( !s.begin( width, height ) )
			return 1;
		set( s.plugin, "Kick", 1.0f );
		for( int f = 0; f < 20; ++f )
			s.render( f, flat1 );
		s.resize( w2, h2 );
		std::vector< double > resized;
		for( int f = 20; f < 50; ++f )
		{
			s.render( f, flat2 );
			resized.push_back( brightest( s.readBackFloat(), w2, h2 ).x / w2 );
		}
		s.end();

		Session u;
		apply( u.plugin, bb );
		if( !u.begin( w2, h2 ) )
			return 1;
		set( u.plugin, "Kick", 1.0f );
		double worst = 0.0;
		for( int f = 0; f < 50; ++f )
		{
			u.render( f, flat2 );
			if( f >= 20 )
				worst = std::max( worst, std::fabs( brightest( u.readBackFloat(), w2, h2 ).x / w2 - resized[ static_cast< size_t >( f - 20 ) ] ) );
		}
		u.end();
		const double tol = 1.0 / std::min( width, w2 );
		failures += report( worst <= tol, quiet, "resize: the lamp swinging through it follows an unresized run at %dx%d to %.2e of the width (tol one pixel, %.2e)", w2, h2, worst, tol );
	}
	return failures;
}

//---------------------------------------------------------------------------
// Offline: no GL.
//---------------------------------------------------------------------------
int runControls( bool quiet = false )
{
	using namespace repousse::controls;
	int failures = 0;
	double worst = 0.0;
	for( int i = 0; i <= 20; ++i )
	{
		const float p  = static_cast< float >( i ) / 20.0f;
		const double q = static_cast< double >( p );
		const double pairs[][ 2 ] = {
			{ DepthPxFromParam( p ), stated::depthPx( q ) },         { PunchRadiusPxFromParam( p ), stated::punchPx( q ) },
			{ SmoothSigmaFromParam( p ), stated::smoothSigma( q ) }, { RoughnessAlphaFromParam( p ), stated::alpha( q ) },    { LampHeightFromParam( p ), stated::lampHeight( q ) },
			{ IntensityFromParam( p ), stated::intensity( q ) },     { CordLengthFromParam( p ), stated::cord( q ) },
			{ DampingRatioFromParam( p ), stated::zeta( q ) },       { SwingRateFromParam( p ), stated::swing( q ) },
		};
		for( const auto& pr : pairs )
			worst = std::max( worst, std::fabs( pr[ 0 ] - pr[ 1 ] ) / std::max( 1.0, std::fabs( pr[ 1 ] ) ) );
	}
	failures += report( worst <= 1e-12, quiet, "controls: nine mappings at 21 points each against the README's statement: worst relative %.1e", worst );

	double f0Worst = 0.0;
	for( int m = 0; m < kMetalCount; ++m )
	{
		const Rgb& got = MetalF0( m );
		f0Worst = std::max( { f0Worst, std::fabs( got.r - stated::kF0[ m ].r ), std::fabs( got.g - stated::kF0[ m ].g ), std::fabs( got.b - stated::kF0[ m ].b ) } );
	}
	failures += report( f0Worst == 0.0 && kMetalDiffuse == stated::kMetalDiffuse && kPaintF0 == stated::kPaintF0 && kShadowSteps == stated::kShadowSteps
	                        && kGravity == stated::kGravity && kMetresPerFrameHeight == stated::kMetresPerHeight,
	                    quiet, "controls: five metals' F0 as tabulated (worst %.1e); the patina's diffuse %.2f, paint F0 %.2f, %d march steps, g %.5f, %g m per frame height",
	                    f0Worst, kMetalDiffuse, kPaintF0, kShadowSteps, kGravity, kMetresPerFrameHeight );

	int tapsOk = 0;
	for( const double r : { 0.0, 0.5, 0.99, 1.0, 1.5, 4.0, 4.9, 16.0 } )
		tapsOk += MorphTaps( r ) == static_cast< int >( std::floor( r ) ) || ( r < 1.0 && MorphTaps( r ) == 0 ) ? 1 : 0;
	failures += report( tapsOk == 8, quiet, "controls: the punch's taps are floor( R ), none under a pixel (%d of 8)", tapsOk );

	Repousse plugin;
	const double L = CordLengthFromParam( plugin.GetFloatParameter( Repousse::PT_CORD ) );
	failures += report( std::fabs( L - 0.5 ) < 1e-12 && DepthPxFromParam( plugin.GetFloatParameter( Repousse::PT_DEPTH ) ) == 16.0
	                        && PunchRadiusPxFromParam( plugin.GetFloatParameter( Repousse::PT_PUNCH ) ) == 4.0,
	                    quiet, "controls: defaults: cord %.2f m (period %.3f s), Depth 16 px, Punch Radius 4 px", L, 2.0 * kPi * std::sqrt( L / kGravity ) );
	return failures;
}

int runProfile( bool quiet = false )
{
	using namespace repousse::controls;
	int failures = 0;
	//The paraboloid tip against the sphere of the same radius: the stated
	//closed form is the difference, and it is under 1% of R out to R / 2.
	double worstForm = 0.0, worstHalf = 0.0, rim = 0.0;
	for( const double R : { 1.0, 2.5, 4.0, 9.0, 16.0 } )
		for( int i = 0; i <= 1000; ++i )
		{
			const double r      = R * i / 1000.0;
			const double sphere = std::sqrt( R * R - r * r ) - R;
			const double parab  = -r * r / ( 2.0 * R );
			const double diff   = parab - sphere;
			worstForm           = std::max( worstForm, std::fabs( diff - PunchProfileError( R, r ) ) / R );
			if( r <= 0.5 * R )
				worstHalf = std::max( worstHalf, diff / R );
			if( i == 1000 )
				rim = diff / R;
		}
	failures += report( worstForm <= 1e-12 && worstHalf < 0.01 && std::fabs( rim - 0.5 ) < 1e-12, quiet,
	                    "profile: paraboloid - sphere is r^4 / 2R ( R + sqrt( R^2 - r^2 ) )^2 (worst %.1e R), %.2f%% of R out to R/2, R/2 at the rim", worstForm, 100.0 * worstHalf );
	return failures;
}

int runDetector( int perturb = 0, bool quiet = false )
{
	using repousse::OnsetDetector;
	int failures = 0;
	std::vector< float > loud( 64, 0.64f ), silent( 64, 0.0f );
	{
		OnsetDetector d;
		double kicks = 0.0;
		for( int f = 0; f < 120; ++f )
			kicks += d.Update( loud.data(), 64, f / 60.0, false, perturb );
		failures += report( kicks == 0.0, quiet, "detector: loud from the first frame: kicks summing to %.3g over two seconds (primed)", kicks );
	}
	{
		//A step from silence kicks once, at full strength, and never again
		//while held; the envelope then releases by e^( -dt / tau ).
		OnsetDetector d;
		double at30 = 0.0, later = 0.0, e90 = 0.0, e91 = 0.0;
		int kickFrames = 0;
		for( int f = 0; f <= 91; ++f )
		{
			const double k = d.Update( f < 30 ? silent.data() : loud.data(), 64, f / 60.0, false, perturb );
			if( f == 30 )
				at30 = k;
			else if( f > 30 )
				later += k;
			if( k > 0.0 )
				++kickFrames;
			if( f == 90 )
				e90 = d.Envelope();
			if( f == 91 )
				e91 = d.Envelope();
		}
		const double tau = -( 1.0 / 60.0 ) / std::log( e91 / e90 );
		failures += report( at30 == 1.0 && later == 0.0 && kickFrames == 1 && std::fabs( tau - stated::kRelease ) <= 1e-9, quiet,
		                    "detector: a step from silence: one kick of %.3f on frame 30, none while held (%.3g), envelope tau %.6f s (stated %.3f)", at30, later, tau, stated::kRelease );
	}
	{
		//Two hits: the second, arriving while the first's envelope has
		//released halfway, kicks by the rise above what is left.
		OnsetDetector d;
		std::vector< double > k;
		for( int f = 0; f <= 60; ++f )
		{
			const bool hit = ( f >= 10 && f < 12 ) || ( f >= 40 && f < 42 );
			k.push_back( d.Update( hit ? loud.data() : silent.data(), 64, f / 60.0, false, perturb ) );
		}
		const double expectSecond = 1.0 - std::exp( -( 30.0 / 60.0 ) / stated::kRelease );
		failures += report( k[ 10 ] == 1.0 && std::fabs( k[ 40 ] - expectSecond ) <= 1e-9, quiet, "detector: two hits half a second apart: kicks %.3f and %.4f (stated %.4f: the rise above the released envelope)",
		                    k[ 10 ], k[ 40 ], expectSecond );
	}
	{
		OnsetDetector d;
		double kicks = 0.0;
		for( int f = 0; f < 60; ++f )
			kicks += d.Update( loud.data(), 64, f < 30 ? 50.0 + f / 60.0 : 2.0 + f / 60.0, f == 30, perturb );
		failures += report( kicks == 0.0, quiet, "detector: loud across a jump (re-primed): kicks %.3g", kicks );
	}
	{
		//A dt of zero is a frame with no release, not a snap.
		OnsetDetector d;
		for( int f = 0; f < 31; ++f )
			d.Update( f < 30 ? silent.data() : loud.data(), 64, f / 60.0, false, perturb );
		const double e = d.Envelope();
		double kicks   = 0.0;
		for( int i = 0; i < 5; ++i )
			kicks += d.Update( loud.data(), 64, 30.0 / 60.0, false, perturb );
		failures += report( d.Envelope() == e && kicks == 0.0, quiet, "detector: five frames at the same instant after a hit: envelope %.4f unmoved, no kick", e );
	}
	return failures;
}

/// The CPU pendulum's trace: OffsetX in "pixels" at a chosen scale.
Trace traceModel( double L, double zeta, double rate, int perturb, int frames, double pxPerM )
{
	repousse::Pendulum p;
	repousse::PendulumSettings ps;
	ps.cordMetres   = L;
	ps.dampingRatio = zeta;
	p.Configure( ps );
	p.Kick( rate, 0.0 );
	Trace tr;
	for( int f = 0; f < frames; ++f )
	{
		if( f > 0 )
			p.Update( 1.0 / 60.0, perturb );
		tr.t.push_back( f / 60.0 );
		tr.x.push_back( p.OffsetX() * pxPerM );
		tr.y.push_back( p.OffsetY() * pxPerM );
	}
	return tr;
}

int runModel( int perturb = 0, bool quiet = false )
{
	int failures = 0;
	//The model's own trace, no picture in the way: the tolerances are the
	//frame sampling (1e-5 s crossings, peaks low by ( w dt )^2 / 8) and the
	//series, so 1e-4 on the period and 1.5e-3 on the decrement.
	int cycles = 0;
	failures += judgeSwing( traceModel( 0.5, 0.01, 3.0, perturb, 360, 1000.0 ), 0.0, 0.5, 0.01, 1000.0, 1e-4, 0.0, quiet, "model (period)", cycles );
	failures += judgeSwing( traceModel( 0.5, 0.05, 0.6, perturb, 360, 1000.0 ), 0.0, 0.5, 0.05, 1000.0, 0.0, 1.5e-3, quiet, "model (decay) ", cycles );
	//A second cord length, so L is not a constant that happens to fit.
	failures += judgeSwing( traceModel( 1.0, 0.01, 2.0, perturb, 480, 1000.0 ), 0.0, 1.0, 0.01, 1000.0, 1e-4, 0.0, quiet, "model (1 m)   ", cycles );
	//A one-second frame does not blow up: the elapsed time is clamped.
	{
		repousse::Pendulum p;
		p.Configure( { 0.1, 0.005 } );
		p.Kick( 3.0, 0.0 );
		double worst = 0.0;
		for( int f = 0; f < 200; ++f )
		{
			p.Update( 1.0, perturb );
			worst = std::max( worst, std::fabs( p.AngleX() ) );
		}
		failures += report( std::isfinite( worst ) && worst < 3.2, quiet, "model: 200 one-second frames on a 0.1 m cord kicked hard: |theta| stays finite and under pi (%.3f)", worst );
	}
	return failures;
}

int runClock( int perturb = 0, bool quiet = false )
{
	using repousse::Clock;
	using repousse::OnsetDetector;
	using repousse::Pendulum;
	int failures = 0;
	Clock seconds, millis;
	seconds.SetScaleForTest( 1.0 );
	millis.SetScaleForTest( 0.001 );
	millis.SetFloatForTest( ( perturb & pb::kClockFloat ) != 0 );
	OnsetDetector da, db;
	Pendulum pa, pb_;
	pa.Configure( { 0.5, 0.02 } );
	pb_.Configure( { 0.5, 0.02 } );
	const double origin = 499000000.0 + 6.0 * 86400.0 * 1000.0;//ms: Resolume's measured clock, plus six days
	double worst        = 0.0;
	double lastA = 0.0, lastB = 0.0;
	std::vector< float > bins( 64 );
	for( int k = 0; k < 3000; ++k )
	{
		const float v = ( k % 45 ) < 3 ? 0.7f : 0.02f;
		std::fill( bins.begin(), bins.end(), v * v );
		seconds.Update( k / 60.0 );
		millis.Update( origin + k * 1000.0 / 60.0 );
		const double ka = da.Update( bins.data(), 64, seconds.Now(), seconds.Jumped(), 0 );
		const double kb = db.Update( bins.data(), 64, millis.Now(), millis.Jumped(), 0 );
		if( ka > 0.0 )
			pa.Kick( ka, 0.0 );
		if( kb > 0.0 )
			pb_.Kick( kb, 0.0 );
		pa.Update( k == 0 ? 0.0 : seconds.Now() - lastA, 0 );
		pb_.Update( k == 0 ? 0.0 : millis.Now() - lastB, 0 );
		lastA = seconds.Now();
		lastB = millis.Now();
		worst = std::max( worst, std::fabs( pa.OffsetX() - pb_.OffsetX() ) );
	}
	//A double at 1.02e6 s resolves 1.2e-10 s: nothing. A float at that
	//origin resolves 0.0625 s, so a 1/60 s frame reads as 0 or 0.0625.
	failures += report( worst <= 1e-6, quiet, "clock: a six-day millisecond clock and a fresh seconds clock swing the lamp alike over 3000 frames: worst %.2e m (tol 1e-6)", worst );
	return failures;
}

int runNames( bool quiet = false )
{
	Repousse plugin;
	std::set< std::string > seen;
	int bad = 0;
	for( const NamedParameter& p : listParameters( plugin ) )
	{
		if( p.name.size() > 16 || !seen.insert( p.name ).second )
			++bad;
	}
	const std::string name = "SW Repousse";
	return report( bad == 0 && name.size() <= 16, quiet, "names: %zu parameters, all unique and within 16 characters; the plugin is '%s' (%zu)",
	               seen.size(), name.c_str(), name.size() );
}

//---------------------------------------------------------------------------
// --negative: every check above can fail.
//---------------------------------------------------------------------------
struct NegativeControl
{
	const char* what;
	int failuresSeen;
};

int summariseNegatives( const std::vector< NegativeControl >& controls )
{
	int failures = 0;
	for( const NegativeControl& c : controls )
	{
		const bool ok = c.failuresSeen > 0;
		++g_checks;
		std::printf( "negative %-58s %s  %s\n", c.what, ok ? "it failed" : "it PASSED", verdict( ok ) );
		if( !ok )
		{
			++failures;
			++g_failures;
		}
	}
	std::printf( "%s\n", failures == 0 ? "negative: every perturbed model is caught" : "negative: FAILURES -- a check cannot fail" );
	return failures;
}

template< typename F >
int caught( F&& check )
{
	const int checks = g_checks, failures = g_failures;
	const int seen   = check();
	g_checks         = checks;
	g_failures       = failures;
	return seen;
}

int runNegativeOffline()
{
	return summariseNegatives( {
		{ "detector: onset baseline starts from silence", caught( [] { return runDetector( pb::kUnprimed, true ); } ) },
		{ "model: sin( theta ) replaced by theta", caught( [] { return runModel( pb::kLinearPendulum, true ); } ) },
		{ "model: g 3% high", caught( [] { return runModel( pb::kGravityDetuned, true ); } ) },
		{ "model: zeta 15% high", caught( [] { return runModel( pb::kDampingDetuned, true ); } ) },
		{ "clock: kept in float", caught( [] { return runClock( pb::kClockFloat, true ); } ) },
	} );
}

int runNegative( int width, int height )
{
	return summariseNegatives( {
		{ "lambert: the 1/r^2 dropped (the spec's)", caught( [ & ] { return runLambert( width, height, pb::kNoInverseSquare, true ); } ) },
		{ "specular: the lobe on n.l instead of n.h", caught( [ & ] { return runSpecular( width, height, pb::kSpecularOnLight, true ); } ) },
		{ "fresnel: every F0 replaced by 0.5", caught( [ & ] { return runFresnel( width, height, pb::kF0Grey, true ); } ) },
		{ "shadow: no march (the spec's)", caught( [ & ] { return runShadow( width, height, pb::kNoShadowMarch, true ); } ) },
		{ "punch: a flat punch, no paraboloid", caught( [ & ] { return runPunch( width, height, pb::kFlatPunch, true ); } ) },
		{ "punch: the radius 10% wide", caught( [ & ] { return runPunch( width, height, pb::kPunchDetuned, true ); } ) },
		{ "pendulum: sin( theta ) replaced by theta", caught( [ & ] { return runPendulum( width, height, pb::kLinearPendulum, true ); } ) },
		{ "pendulum: g 3% high", caught( [ & ] { return runPendulum( width, height, pb::kGravityDetuned, true ); } ) },
		{ "pendulum: zeta 15% high", caught( [ & ] { return runPendulum( width, height, pb::kDampingDetuned, true ); } ) },
		{ "prime: onset detector unprimed", caught( [ & ] { return runPrime( width, height, pb::kUnprimed, true ); } ) },
		{ "resize: the old height buffers kept", caught( [ & ] { return runResize( width, height, pb::kResizeStale, true ); } ) },
		{ "resize: the pendulum reset", caught( [ & ] { return runResize( width, height, pb::kPendulumResetOnResize, true ); } ) },
	} );
}

//---------------------------------------------------------------------------
// The moving card, for --out, the sweep, the bench and a default --pipe: a
// soft dome drifting over a tilted plane, a ring, a hard-edged block, a
// comb of fine lines (the punch's question), a noisy patch, and a colour
// gradient so Clip mode reads differently. It carries a spectrum that kicks
// on the bass every half second, so the pendulum's controls live.
//---------------------------------------------------------------------------
uint32_t hashInt( uint32_t x )
{
	//PCG output mix, written out.
	const uint32_t state = x * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

double noiseAt( int x, int y )
{
	return hashInt( static_cast< uint32_t >( x ) * 1973u + static_cast< uint32_t >( y ) * 9277u + 26699u ) / 4294967296.0 - 0.5;
}

std::vector< unsigned char > buildCard( int width, int height, int64_t frame )
{
	std::vector< unsigned char > img( static_cast< size_t >( width ) * height * 4 );
	const double t = static_cast< double >( frame ) / 60.0;
	const double aspect = static_cast< double >( width ) / height;
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 ) / width, v = 1.0 - ( y + 0.5 ) / height;
			double e = 0.08 + 0.12 * u;
			//A dome.
			{
				const double dx = ( u - ( 0.32 + 0.04 * std::sin( 0.6 * t ) ) ) * aspect, dy = v - 0.42;
				const double r  = std::sqrt( dx * dx + dy * dy );
				e += 0.7 * std::max( 0.0, 1.0 - r * r / ( 0.26 * 0.26 ) );
			}
			//A ring.
			{
				const double dx = ( u - 0.7 ) * aspect, dy = v - ( 0.6 + 0.05 * std::cos( 0.5 * t ) );
				const double r  = std::sqrt( dx * dx + dy * dy );
				e += 0.6 * std::exp( -( r - 0.16 ) * ( r - 0.16 ) / ( 2.0 * 0.03 * 0.03 ) );
			}
			//A hard-edged block.
			if( u > 0.78 && u < 0.94 && v > 0.1 && v < 0.3 )
				e += 0.55;
			//A comb of fine lines, 2 px on 6.
			if( u > 0.05 && u < 0.28 && v > 0.72 && v < 0.94 && ( x / 2 ) % 3 == 0 )
				e += 0.5;
			//A noisy patch.
			if( u > 0.45 && u < 0.62 && v > 0.08 && v < 0.28 )
				e += 0.25 * noiseAt( x, y + static_cast< int >( frame ) );
			e = std::clamp( e, 0.0, 1.0 );
			unsigned char* px = img.data() + ( static_cast< size_t >( y ) * width + x ) * 4;
			px[ 0 ] = static_cast< unsigned char >( std::lround( 255.0 * e ) );
			px[ 1 ] = static_cast< unsigned char >( std::lround( 255.0 * std::clamp( 0.8 * e + 0.2 * v, 0.0, 1.0 ) ) );
			px[ 2 ] = static_cast< unsigned char >( std::lround( 255.0 * std::clamp( 1.0 - e * u, 0.0, 1.0 ) ) );
			px[ 3 ] = 255;
		}
	return img;
}

std::vector< float > cardSpectrum( int64_t frame )
{
	std::vector< float > bins( 64, 0.0f );
	const double phase = static_cast< double >( frame % 30 ) / 60.0;
	const float kick   = static_cast< float >( 0.7 * std::exp( -phase / 0.08 ) );
	for( int i = 0; i < 64; ++i )
		bins[ static_cast< size_t >( i ) ] = i < 8 ? kick : 0.02f + 0.01f * static_cast< float >( ( frame + i ) % 3 );
	return bins;
}

//---------------------------------------------------------------------------
// --bench
//---------------------------------------------------------------------------
double benchAt( const std::vector< std::string >& settings, int width, int height, int frames )
{
	Session session;
	session.floatOutput = false;
	for( const std::string& setting : settings )
	{
		std::string error;
		applySetting( session.plugin, setting, error );
	}
	if( !session.begin( width, height ) )
		return -1.0;

	std::vector< std::vector< unsigned char > > loop;
	for( int i = 0; i < 4; ++i )
		loop.push_back( buildCard( width, height, i * 7 ) );

	const int warmup = 20;
	for( int frame = 0; frame < warmup; ++frame )
		session.render( frame, loop[ static_cast< size_t >( frame ) % loop.size() ] );
	glFinish();

	//Best of three: the GPU is shared with other builds on this machine.
	double best   = 1e9;
	int64_t frame = warmup;
	for( int run = 0; run < 3; ++run )
	{
		const auto start = std::chrono::steady_clock::now();
		for( int i = 0; i < frames; ++i, ++frame )
			session.renderAt( frame );
		glFinish();
		const double seconds = std::chrono::duration< double >( std::chrono::steady_clock::now() - start ).count();
		best                 = std::min( best, seconds * 1000.0 / frames );
	}
	session.end();
	return best;
}

int runBench( const std::vector< std::string >& settings, int frames )
{
	struct Size
	{
		const char* name;
		int width, height;
	};
	const Size sizes[] = { { "1280x720  ", 1280, 720 }, { "1920x1080 ", 1920, 1080 }, { "3840x2160 ", 3840, 2160 } };
	std::printf( "%d frames each, best of three runs, after a 20-frame warm-up, glFinish both sides.\n\n", frames );
	std::printf( "resolution     ms/frame   %% of a 60fps frame   Punch 1 (16 px)\n" );
	std::vector< std::string > heavy = settings;
	heavy.push_back( "Punch Radius=1" );
	for( const Size& size : sizes )
	{
		const double ms = benchAt( settings, size.width, size.height, frames );
		const double hv = benchAt( heavy, size.width, size.height, frames );
		std::printf( "%s    %7.3f        %5.1f%%             %7.3f\n", size.name, ms, ms / 16.667 * 100.0, hv );
	}
	std::printf( "\nTwelve passes at the defaults (height, two blur axes of radius 3, eight morphology\n"
	             "axes of 4 taps, the shade pass with a 32-step shadow march and 32 occlusion taps).\n"
	             "Punch at its maximum makes each morphology axis 33 taps.\n" );
	return 0;
}

//---------------------------------------------------------------------------
// --dump-shaders
//---------------------------------------------------------------------------
int dumpShaders( const std::string& dir )
{
	namespace sh = repousse::shaders;
	const std::pair< const char*, const char* > files[] = {
		{ "vertex.vert", sh::kVertex },
		{ "height.frag", sh::kHeight },
		{ "blur.frag", sh::kBlur },
		{ "morph.frag", sh::kMorph },
		{ "shade.frag", sh::kShade },
	};
	for( const auto& f : files )
	{
		std::ofstream out( dir + "/" + f.first );
		if( !out )
		{
			std::fprintf( stderr, "cannot write %s/%s\n", dir.c_str(), f.first );
			return 1;
		}
		out << f.second;
	}
	std::printf( "wrote %zu shaders to %s\n", sizeof( files ) / sizeof( files[ 0 ] ), dir.c_str() );
	return 0;
}

//---------------------------------------------------------------------------
// --pipe cue sheet: one 'frame Name Value' per line, the fleet's format.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return tracks;
	}
	std::string line;
	int lineNumber = 0;
	while( std::getline( file, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream in( line );
		int frame = 0;
		if( !( in >> frame ) )
			continue;
		std::vector< std::string > words;
		std::string word;
		while( in >> word )
			words.push_back( word );
		if( words.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		const float value = std::strtof( words.back().c_str(), nullptr );
		words.pop_back();
		std::string name = words.front();
		for( size_t i = 1; i < words.size(); ++i )
			name += " " + words[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::sort( entry.second.begin(), entry.second.end() );
	return tracks;
}

float valueAt( const Track& track, int frame )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 1; i < track.size(); ++i )
		if( frame <= track[ i ].first )
		{
			const auto& a    = track[ i - 1 ];
			const auto& b    = track[ i ];
			const float span = static_cast< float >( b.first - a.first );
			const float t    = span > 0.0f ? static_cast< float >( frame - a.first ) / span : 1.0f;
			return a.second + ( b.second - a.second ) * t;
		}
	return track.back().second;
}

//---------------------------------------------------------------------------
void usage()
{
	std::printf(
		"rptest -- render and measure the Repousse sheet\n"
		"\n"
		"  --out PATH          render the moving relief card through the plugin (default /tmp/repousse.png)\n"
		"  --size WxH          raster (default 1280x720)\n"
		"  --frames N          frames to render before reading back (default 40)\n"
		"  --fps N             synthetic frame rate driving the clock (default 60)\n"
		"  --set \"Name=V\"      set a parameter by its display name (element index for options). Repeatable.\n"
		"  --list              every parameter, its kind, default and range\n"
		"  --no-audio          --out without the card's spectrum (the lamp hangs still)\n"
		"\n"
		"  checks that render, at --size:\n"
		"  --lambert           planes read Intensity ( Lref / r )^2 n.l kd F0 under a grid of lamps\n"
		"  --specular          the highlight on a hemisphere peaks at the half-vector point\n"
		"  --fresnel           at normal incidence each metal reflects its tabulated F0\n"
		"  --shadow            the shadow behind a step is h / tan( theta ), within a march step\n"
		"  --punch             a narrow ridge comes out min( H, d^2 / 2R ) tall, a wide one H\n"
		"  --pendulum          the lamp's period and decay, read from the picture\n"
		"  --prime             loud audio from frame 0 does not kick; a step does; a scrub re-primes\n"
		"  --resize            a resize mid-run keeps the lamp swinging and leaves no stale buffer\n"
		"  --negative          every check above can fail\n"
		"  --perturb BITS      run the checks verbosely against a perturbed model (bits in Controls.h)\n"
		"\n"
		"  checks that need no GL:\n"
		"  --controls          every control mapping and constant against the README's statement\n"
		"  --profile           the paraboloid punch against the sphere: the stated bound\n"
		"  --detector          the onset detector: primed, one kick per hit, release, jumps\n"
		"  --model             the pendulum's period and decay from the model's own position\n"
		"  --clock             a six-day millisecond clock swings the lamp as a fresh one does\n"
		"  --names             nothing the host will silently truncate\n"
		"  --offline           all of those, and their negative controls; says loudly what it skipped. For CI.\n"
		"  --allow-no-gl       with the rendering checks: SKIP loudly, not FAIL, when no GL 4.1 context exists\n"
		"\n"
		"  --bench             time ProcessOpenGL at 720p, 1080p and 4K\n"
		"  --dump-shaders DIR  write the exact GLSL the plugin compiles\n"
		"  --pipe              raw RGBA frames on stdin, raw RGBA frames on stdout\n"
		"  --script PATH       parameter cues for --pipe: 'frame Name Value'\n"
		"  --help\n" );
}
} // namespace

int main( int argc, char** argv )
{
	std::string outPath = "/tmp/repousse.png";
	std::string scriptPath;
	std::string dumpDir;
	int width      = 1280;
	int height     = 720;
	int frames     = 40;
	int failRender = -1;
	int perturb    = 0;
	double fps     = 60.0;
	bool wantList  = false;
	bool wantBench = false;
	bool wantPipe  = false;
	bool allowNoGL = false;
	bool noAudio   = false;
	std::vector< std::string > settings;
	std::vector< std::string > checks;

	const std::set< std::string > rendered = { "--lambert", "--specular", "--fresnel", "--shadow", "--punch", "--pendulum", "--prime", "--resize", "--negative" };
	const std::set< std::string > offline  = { "--controls", "--profile", "--detector", "--model", "--clock", "--names", "--negative-offline" };

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			usage();
			return 0;
		}
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--dump-shaders" && hasNext )
			dumpDir = argv[ ++i ];
		else if( argument == "--size" && hasNext )
		{
			const std::string size = argv[ ++i ];
			const size_t x         = size.find( 'x' );
			if( x == std::string::npos )
			{
				std::fprintf( stderr, "--size wants WxH\n" );
				return 2;
			}
			width  = std::atoi( size.substr( 0, x ).c_str() );
			height = std::atoi( size.substr( x + 1 ).c_str() );
		}
		else if( argument == "--frames" && hasNext )
			frames = std::atoi( argv[ ++i ] );
		else if( argument == "--fps" && hasNext )
			fps = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--perturb" && hasNext )
			perturb = std::atoi( argv[ ++i ] );
		else if( argument == "--fail-render-at" && hasNext )
			failRender = std::atoi( argv[ ++i ] );//test hook: verify.sh proves --pipe exits 1 on a failed render
		else if( argument == "--list" )
			wantList = true;
		else if( argument == "--bench" )
			wantBench = true;
		else if( argument == "--pipe" )
			wantPipe = true;
		else if( argument == "--no-audio" )
			noAudio = true;
		else if( argument == "--allow-no-gl" )
			allowNoGL = true;
		else if( argument == "--offline" )
			for( const char* m : { "--controls", "--profile", "--detector", "--model", "--clock", "--names", "--negative-offline" } )
				checks.push_back( m );
		else if( rendered.count( argument ) || offline.count( argument ) )
			checks.push_back( argument );
		else
		{
			std::fprintf( stderr, "unknown argument: %s\n", argument.c_str() );
			usage();
			return 2;
		}
	}

	if( width < 3 || height < 3 || frames <= 0 || fps <= 0.0 )
	{
		std::fprintf( stderr, "width and height must be at least 3, frames and fps positive\n" );
		return 2;
	}

	if( !dumpDir.empty() )
		return dumpShaders( dumpDir );

	if( wantList )
	{
		//No GL needed: answered before a context is made, so it works in CI.
		Repousse plugin;
		std::printf( "%3s  %-16s  %-9s  %-8s  %s\n", "id", "name", "kind", "default", "range" );
		for( const NamedParameter& p : listParameters( plugin ) )
			std::printf( "%3u  %-16s  %-9s  %.4f    [%g..%g]\n", p.index, p.name.c_str(), kindName( p ), p.value, p.low, p.high );
		return 0;
	}

	if( !checks.empty() )
	{
		bool needGL     = false;
		bool offlineRan = false;
		for( const std::string& check : checks )
		{
			if( check == "--controls" )
				runControls();
			else if( check == "--profile" )
				runProfile();
			else if( check == "--detector" )
				runDetector( perturb );
			else if( check == "--model" )
				runModel( perturb );
			else if( check == "--clock" )
				runClock( perturb );
			else if( check == "--names" )
				runNames();
			else if( check == "--negative-offline" )
			{
				runNegativeOffline();
				offlineRan = true;
			}
			else
			{
				needGL = true;
				continue;
			}
			std::printf( "\n" );
		}
		if( offlineRan )
			std::printf( "   OFFLINE: --lambert, --specular, --fresnel, --shadow, --punch, --pendulum, --prime,\n"
			             "   --resize and their negative controls were NOT run. Nothing here drew a pixel\n"
			             "   through a GL driver; the shaders were not exercised, only (in CI) compiled by glslc.\n\n" );

		if( needGL )
		{
			CGLContextObj context = createContext();
			if( context == nullptr && allowNoGL )
				std::printf( "   SKIP  could not create an OpenGL 4.1 core context, accelerated or software.\n"
				             "         The rendering checks and their negative controls were NOT run.\n" );
			else if( context == nullptr )
			{
				std::printf( "   FAIL  could not create an OpenGL 4.1 core context\n" );
				++g_failures;
			}
			else
			{
				for( const std::string& check : checks )
				{
					if( check == "--lambert" )
						runLambert( width, height, perturb );
					else if( check == "--specular" )
						runSpecular( width, height, perturb );
					else if( check == "--fresnel" )
						runFresnel( width, height, perturb );
					else if( check == "--shadow" )
						runShadow( width, height, perturb );
					else if( check == "--punch" )
						runPunch( width, height, perturb );
					else if( check == "--pendulum" )
						runPendulum( width, height, perturb );
					else if( check == "--prime" )
						runPrime( width, height, perturb );
					else if( check == "--resize" )
						runResize( width, height, perturb );
					else if( check == "--negative" )
						runNegative( width, height );
					else
						continue;
					std::printf( "\n" );
				}
				CGLSetCurrentContext( nullptr );
				CGLDestroyContext( context );
			}
		}
		std::printf( "%d checks, %d failed\n", g_checks, g_failures );
		return g_failures == 0 ? 0 : 1;
	}

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL context\n" );
		return 1;
	}
	auto finish = [ & ]( int result ) {
		CGLSetCurrentContext( nullptr );
		CGLDestroyContext( context );
		return result;
	};

	if( wantBench )
		return finish( runBench( settings, frames < 40 ? 60 : frames ) );

	Session session;
	session.floatOutput = false;
	session.fps         = fps;
	for( const std::string& setting : settings )
	{
		std::string error;
		if( applySetting( session.plugin, setting, error ) )
			continue;
		std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
		return finish( 2 );
	}

	if( wantPipe )
	{
		//Everything but the video goes to stderr: one stray byte in stdout is
		//a torn frame for the rest of the reel.
		std::map< unsigned int, Track > automation;
		if( !scriptPath.empty() )
		{
			std::string error;
			const std::map< std::string, Track > tracks = loadScript( scriptPath, error );
			if( !error.empty() )
			{
				std::fprintf( stderr, "%s\n", error.c_str() );
				return finish( 2 );
			}
			for( const auto& entry : tracks )
			{
				const int index = indexOfParameter( session.plugin, entry.first );
				if( index < 0 )
				{
					std::fprintf( stderr, "script names '%s', which is not a parameter (try --list)\n", entry.first.c_str() );
					return finish( 2 );
				}
				automation[ static_cast< unsigned int >( index ) ] = entry.second;
			}
		}

		//A closed stdout must be a failed write we can see, not a SIGPIPE
		//that kills the process with 141 before it can say so.
		std::signal( SIGPIPE, SIG_IGN );

		if( !session.begin( width, height ) )
			return finish( 1 );

		std::vector< unsigned char > frame( static_cast< size_t >( width ) * height * 4 );
		int status = 0;
		for( int index = 0;; ++index )
		{
			size_t got = 0;
			while( got < frame.size() )
			{
				const ssize_t n = read( STDIN_FILENO, frame.data() + got, frame.size() - got );
				if( n <= 0 )
					break;
				got += static_cast< size_t >( n );
			}
			//A partial frame is the end of the stream, never a frame.
			if( got < frame.size() )
			{
				if( got > 0 )
					std::fprintf( stderr, "partial frame at the end (%zu of %zu bytes, %dx%d): dropped\n", got, frame.size(), width, height );
				break;
			}

			//Through the plugin's own setter, so a cue moves what a slider would.
			for( const auto& track : automation )
				session.plugin.SetFloatParameter( track.first, valueAt( track.second, index ) );

			//Frame n is clocked at n / fps, never the wall clock.
			const bool rendered = index != failRender && session.render( index, frame );
			if( !rendered )
			{
				std::fprintf( stderr, "render failed at frame %d\n", index );
				status = 1;
				break;
			}

			const std::vector< unsigned char > out = session.readBack();
			size_t written                         = 0;
			while( written < out.size() )
			{
				const ssize_t put = write( STDOUT_FILENO, out.data() + written, out.size() - written );
				if( put <= 0 )
					break;
				written += static_cast< size_t >( put );
			}
			//The reader has gone: rendering on into a closed pipe is work
			//nobody will see, and a short frame is worse than none.
			if( written < out.size() )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				status = 1;
				break;
			}
		}
		session.end();
		return finish( status );
	}

	if( !session.begin( width, height ) )
		return finish( 1 );
	for( int frame = 0; frame < frames; ++frame )
	{
		if( !noAudio )
			session.spectrum( cardSpectrum( frame ) );
		if( !session.render( frame, buildCard( width, height, frame ) ) )
			return finish( 1 );
	}
	const std::vector< unsigned char > pixels = session.readBack();
	session.end();

	if( !writePng( outPath, width, height, pixels ) )
	{
		std::fprintf( stderr, "could not write %s\n", outPath.c_str() );
		return finish( 1 );
	}
	std::printf( "wrote %s (%dx%d, frame %d)\n", outPath.c_str(), width, height, frames - 1 );
	return finish( 0 );
}
