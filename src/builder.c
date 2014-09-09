
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define DS '\\'
#define _BINEXT ".exe"
#else
#define DS '/'
#define _BINEXT
#endif
#include <dirent.h>
#ifndef _WIN32
# include <dlfcn.h>
# include <sys/stat.h>
# include <unistd.h>
#endif

#include "../image-sdks/libpng/png.h"
#include "../image-sdks/libjpg/jpeglib.h"

#define RX_NEED_DEFAULT_MEMFUNC
#include "regex.h"

#include "config.h"


//
// FILE
//
// SYSTEM
//

#if 0&& _WIN32

#define MAX_PATH 260
typedef struct _path_s
{
	WCHAR path[ MAX_PATH ];
}
path_t;

int fs_getcwd( path_t* p )
{
}

#else
/* Linux / Mac / ... */

#ifndef MAX_PATH
# define MAX_PATH 4096
#endif
typedef struct _path_s
{
	char path[ MAX_PATH ];
}
path_t;

static int fs_setpath( path_t* p, const char* src )
{
	strncpy( p->path, src, MAX_PATH );
	p->path[ MAX_PATH - 1 ] = 0;
	return 0;
}

static int fs_getcwd( path_t* p )
{
	if( getcwd( p->path, MAX_PATH ) )
	{
		p->path[ MAX_PATH - 1 ] = 0;
		return 0;
	}
	return -1;
}

static int fs_setcwd( path_t* p )
{
	return chdir( p->path );
}

static int fs_dirname( path_t* p )
{
	char* pos = strchr( p->path, DS );
	if( !pos )
		return -1;
	while( pos )
	{
		char* npos = strchr( pos + 1, DS );
		if( npos )
			pos = npos;
		else
			break;
	}
	pos[0] = 0;
	return 0;
}

static int fs_getexecpath( path_t* p )
{
#if defined(_WIN32)
	int res = GetModuleFileNameA( NULL, p->path, MAX_PATH );
	if( res > 0 && res < MAX_PATH )
	{
		p->path[ res ] = 0;
		return 0;
	}
	return -1;
#elif defined(__APPLE__) && (__MACH__)
	if( _NSGetExecutablePath( p->path, MAX_PATH ) == 0 )
	{
		p->path[ MAX_PATH - 1 ] = 0;
		return 0;
	}
	return -1;
#else
	int res = readlink( "/proc/self/exe", p->path, MAX_PATH );
	if( res > 0 && res < MAX_PATH )
	{
		p->path[ res ] = 0;
		return 0;
	}
	return -1;
#endif
}

static char* fs_readfile( const char* name )
{
	FILE* f = fopen( name, "rb" );
	if( !f )
		return NULL;
	fseek( f, 0, SEEK_END );
	size_t sz = ftell( f );
	fseek( f, 0, SEEK_SET );
	char* data = (char*) malloc( sz + 1 );
	if( !fread( data, sz, 1, f ) )
	{
		free( data );
		fclose( f );
		return 0;
	}
	data[ sz ] = 0;
	return data;
}

static const char* fs_readbinfile( const char* name, byte** outData, i32* outSize )
{
	FILE* f = fopen( name, "rb" );
	if( !f )
		return strerror( errno );
	fseek( f, 0, SEEK_END );
	long sz = ftell( f );
	fseek( f, 0, SEEK_SET );
	byte* data = (byte*) malloc( sz );
	if( !fread( data, sz, 1, f ) )
	{
		free( data );
		fclose( f );
		return 0;
	}
	*outData = data;
	*outSize = sz;
	return NULL;
}

static int fs_copy( FILE* dst, FILE* src )
{
	char bfr[ 1024 ];
	size_t sz;
	while( ( sz = fread( bfr, 1, 1024, src ) ) != 0 )
		if( fwrite( bfr, 1, sz, dst ) < sz )
			return 1;
	return 0;
}

#endif


//
// IMAGES
//

//
// PNG SUPPORT

typedef struct _png_read_data
{
	byte *data, *at;
	size_t size;
}
png_read_data;

static void _png_memread( png_structp png_ptr, png_bytep data, png_size_t size )
{
	png_read_data* pd = (png_read_data*) png_get_io_ptr( png_ptr );
	if( pd->at + size > pd->data + pd->size )
	{
		fprintf( stderr, "INTERNAL ERROR: overrun at %s:%d\n", __FILE__, __LINE__ - 2 );
		exit(1);
	}
	memcpy( data, pd->at, size );
	pd->at += size;
}

static const char* load_image_png( byte* data, i32 size, byte** outData, i32* outWidth, i32* outHeight )
{
	byte* imgData = NULL;
	png_structp png_ptr = png_create_read_struct( PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
	
	if( !png_ptr )
		return "failed to initialize PNG";
	
	if( setjmp( png_jmpbuf( png_ptr ) ) )
	{
		if( imgData )
			free( imgData );
		png_destroy_read_struct( &png_ptr, NULL, NULL );
		return NULL;
	}
	
	png_infop info_ptr = png_create_info_struct( png_ptr );
	
	if( !info_ptr )
	{
		png_destroy_read_struct( &png_ptr, NULL, NULL );
		return "failed to set up PNG reading";
	}
	
	// Load..
	png_read_data prd = { data, data, size };
	png_set_read_fn( png_ptr, &prd, (png_rw_ptr) &_png_memread );
	png_set_user_limits( png_ptr, MAX_BITMAP_SIDE_SIZE, MAX_BITMAP_SIDE_SIZE );
	
	png_read_info( png_ptr, info_ptr );
	png_set_strip_16( png_ptr );
	png_set_packing( png_ptr );
	png_set_gray_to_rgb( png_ptr );
//	png_set_bgr( png_ptr );
//	png_set_add_alpha( png_ptr, 0xffffffff, PNG_FILLER_AFTER );
	
	// send info..
	i32 width = png_get_image_width( png_ptr, info_ptr );
	i32 height = png_get_image_height( png_ptr, info_ptr );
	i32 x, y, rowlen;
	if( png_get_channels( png_ptr, info_ptr ) == PNG_COLOR_TYPE_RGB )
		rowlen = ( ( width * 3 + 3 ) / 4 ) * 4;
	else
		rowlen = width * 4;
	
	imgData = calloc( rowlen * height + 1, 1 );
	imgData[0] = png_get_channels( png_ptr, info_ptr ) != PNG_COLOR_TYPE_RGB;
	byte* pout = imgData + 1;
	
	i32 offsets[ MAX_BITMAP_SIDE_SIZE ] = {0};
	int pass, number_passes = png_set_interlace_handling( png_ptr );
	for( pass = 0; pass < number_passes; ++pass )
	{
		for( y = 0; y < height; ++y )
		{
			png_bytep rowp = (png_bytep) pout + ( height - y - 1 ) * rowlen;
			png_bytep crp = rowp + offsets[ y ];
			png_read_rows( png_ptr, &crp, NULL, 1 );
			offsets[ y ] = crp - rowp;
		}
	}
	for( y = 0; y < height; ++y )
	{
		byte* rp = pout + y * rowlen;
		if( rowlen == width * 4 )
		{
			for( x = 0; x < width; ++x )
			{
				int tmp = rp[ x * 4 ];
				rp[ x * 4 ] = rp[ x * 4 + 3 ];
				rp[ x * 4 + 3 ] = tmp;
				tmp = rp[ x * 4 + 1 ];
				rp[ x * 4 + 1 ] = rp[ x * 4 + 2 ];
				rp[ x * 4 + 2 ] = tmp;
			}
		}
		else
		{
			for( x = 0; x < width; ++x )
			{
				int tmp = rp[ x * 3 ];
				rp[ x * 3 ] = rp[ x * 3 + 2 ];
				rp[ x * 3 + 2 ] = tmp;
			}
		}
	}
	
	png_read_end( png_ptr, info_ptr );
	png_destroy_read_struct( &png_ptr, &info_ptr, NULL );
	
	*outData = imgData;
	*outWidth = width;
	*outHeight = height;
	return NULL;
}


//
// JPG SUPPORT

typedef struct _jpg_error_mgr
{
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
}
jpg_error_mgr;

static void _jpg_error_exit( j_common_ptr cinfo )
{
	jpg_error_mgr* myerr = (jpg_error_mgr*) cinfo->err;
	// (*cinfo->err->output_message) (cinfo);
	longjmp(myerr->setjmp_buffer, 1);
}

static const char* load_image_jpg( byte* data, i32 size, byte** outData, i32* outWidth, i32* outHeight )
{
	byte* imgData = NULL;
	struct jpeg_decompress_struct cinfo;
	jpg_error_mgr jerr;
	
	JSAMPARRAY buffer;
	int row_stride;
	
	cinfo.err = jpeg_std_error( &jerr.pub );
	jerr.pub.error_exit = _jpg_error_exit;
	if( setjmp( jerr.setjmp_buffer ) )
	{
		jpeg_destroy_decompress( &cinfo );
		if( imgData )
			free( imgData );
		return NULL;
	}
	
	jpeg_create_decompress( &cinfo );
	jpeg_mem_src( &cinfo, data, size );
	jpeg_read_header( &cinfo, 1 );
	jpeg_start_decompress( &cinfo );
	
	i32 x, width = cinfo.output_width, height = cinfo.output_height;
	i32 rowlen = ( ( width * 3 + 3 ) / 4 ) * 4;
	imgData = calloc( rowlen * height + 1, 1 );
	imgData[0] = 0;
	byte* pout = imgData + 1 + rowlen * height;
	
	row_stride = width * cinfo.output_components;
	buffer = (*cinfo.mem->alloc_sarray)( (j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1 );
	
	while( cinfo.output_scanline < height )
	{
		jpeg_read_scanlines( &cinfo, buffer, 1 );
		pout -= rowlen;
		memcpy( pout, buffer[0], width * cinfo.output_components );
		for( x = 0; x < width; ++x )
		{
			int tmp = pout[ x * 3 ];
			pout[ x * 3 ] = pout[ x * 3 + 2 ];
			pout[ x * 3 + 2 ] = tmp;
		}
	}
	
	jpeg_finish_decompress( &cinfo );
	jpeg_destroy_decompress( &cinfo );
	
	*outData = imgData;
	*outWidth = width;
	*outHeight = height;
	return NULL;
}

//
int img_load( const char* image, byte** outData, i32* outW, i32* outH )
{
	byte* data = NULL;
	i32 size = 0;
	const char* errstr = fs_readbinfile( image, &data, &size );
	if( errstr )
	{
		fprintf( stderr, "ERROR: failed to load image file '%s': %s\n", image, errstr );
		goto fail;
	}
	
	errstr = load_image_png( data, size, outData, outW, outH );
	if( errstr )
	{
		fprintf( stderr, "ERROR: failed to load PNG image '%s': %s\n", image, errstr );
		goto fail;
	}
	if( !errstr && *outData )
		return 0;
	
	errstr = load_image_jpg( data, size, outData, outW, outH );
	if( errstr )
	{
		fprintf( stderr, "ERROR: failed to load JPEG image '%s': %s\n", image, errstr );
		goto fail;
	}
	if( !errstr && *outData )
		return 0;
	
	fprintf( stderr, "ERROR: unrecognized image format: '%s'\n", image );

fail:
	if( data ) free( data );
	return 1;
}



//
// BUILDER
//


#define INDENT "\t"

char* g_fileDir = "files";
char* g_imageDir = "images";
char* g_scriptFile = "script.txt";
char* g_outputFile = NULL;
int g_dumpScriptPreproc = 0;
int g_dumpScriptCompiled = 0;

FILE* g_outFileHandle = NULL;
FILE* g_runnerFileHandle = NULL;
FILE* g_archFileHandle = NULL;

path_t g_pCurDir;
path_t g_pExeDir;
path_t g_pImgDir;
path_t g_pFileDir;

char* g_scriptData = NULL;
size_t g_scriptLength = 0;
char* g_scriptData2 = NULL;
size_t g_scriptLength2 = 0;
char* g_scriptData3 = NULL;
size_t g_scriptLength3 = 0;

const char* g_imageNames[ MAX_BITMAPS ];
int g_imageNameLengths[ MAX_BITMAPS ];
byte* g_imageData[ MAX_BITMAPS ];
i32 g_imageWidths[ MAX_BITMAPS ];
i32 g_imageHeights[ MAX_BITMAPS ];
int g_numImages = 0;
const char* g_ctrlNames[ MAX_CONTROLS ];
int g_ctrlNameLengths[ MAX_CONTROLS ];
int g_numControls = 0;


static void cleanup()
{
	if( g_outFileHandle ) fclose( g_outFileHandle );
	if( g_runnerFileHandle ) fclose( g_runnerFileHandle );
	if( g_scriptData ) free( g_scriptData );
	if( g_scriptData2 ) free( g_scriptData2 );
	if( g_scriptData3 ) free( g_scriptData3 );
	int i;
	for( i = 0; i < g_numImages; ++i )
		if( g_imageData[ i ] ) free( g_imageData[ i ] );
}


static int handle_script_error( int res, int lineno )
{
	if( res != RSLE_NONE )
	{
		const char* errstr = "unknown error";
		switch( res )
		{
		case RSLE_UNEXP: errstr = "unexpected item"; break;
		case RSLE_UNBASED: errstr = "unexpected follow-up (else/elseif/endif)"; break;
		case RSLE_NOEXP: errstr = "missing item"; break;
		case RSLE_INCOMP: errstr = "unexpected end of item (conditional/inclusion not closed)"; break;
		case RSLE_BRACES: errstr = "brace mismatch"; break;
		case RSLE_LIMIT: errstr = "exceeded a limit"; break;
		}
		fprintf( stderr, "ERROR: failed to compile script - %s at line %d\nscript: %s\n", errstr, lineno, g_scriptFile );
		return 1;
	}
	return 0;
}


extern int ar7z_compress_dir();


static int print_help();
static int eputs( const char* str ){ fprintf( stderr, "%s\n", str ); return 1; }

int main( int argc, char* argv[] )
{
	resolver RS;
	atexit( cleanup );
	
	puts( "" );
	puts( "NAILER Builder 0.4  Copyright (c) 2014 Arvids Kokins" );
	puts( "" );
	
	if( fs_getcwd( &g_pCurDir ) )
		return eputs( "ERROR: failed to retrieve current directory" );
	
	if( fs_getexecpath( &g_pExeDir ) || fs_dirname( &g_pExeDir ) )
		return eputs( "ERROR: failed to retrieve executable directory" );
	
	if( fs_setcwd( &g_pExeDir ) )
		return eputs( "ERROR: failed to move to executable directory" );
	
	g_runnerFileHandle = fopen( "nailer-runner" _BINEXT, "rb" );
	if( !g_runnerFileHandle )
		return eputs( "ERROR: runner file 'nailer-runner" _BINEXT "' was not found in executable directory" );
	
	int i, res, lineno;
	for( i = 1; i < argc; ++i )
	{
		const char* v = argv[i];
		if( i < argc-1 && ( !strcmp( v, "-o" ) || !strcmp( v, "--output" ) ) )
			g_outputFile = argv[ ++i ];
		else if( i < argc-1 && ( !strcmp( v, "-i" ) || !strcmp( v, "--images" ) ) )
			g_imageDir = argv[ ++i ];
		else if( i < argc-1 && ( !strcmp( v, "-f" ) || !strcmp( v, "--files" ) ) )
			g_fileDir = argv[ ++i ];
		else if( i < argc-1 && ( !strcmp( v, "-s" ) || !strcmp( v, "--script" ) ) )
			g_scriptFile = argv[ ++i ];
		else if( !strcmp( v, "-d" ) || !strcmp( v, "--dump" ) )
			{ g_dumpScriptPreproc = 1; g_dumpScriptCompiled = 1; }
		else if( !strcmp( v, "-?" ) || !strcmp( v, "-h" ) || !strcmp( v, "--help" ) )
			return print_help();
		else
			fprintf( stderr, "\nunrecognized option: %s\n\n", argv[i] );
	}
	
	// SCRIPT - LOAD
	if( fs_setcwd( &g_pCurDir ) )
		return eputs( "ERROR: failed to reset current directory" );
	
	if( !g_outputFile )
	{
		print_help();
		return eputs( "\nno output file specified (-o, --output), aborting\n" );
	}
	
	g_outFileHandle = fopen( g_outputFile, "wb" );
	if( !g_outFileHandle )
	{
		fprintf( stderr, "ERROR: failed to open output file '%s' for writing (%s)\n", g_outputFile, strerror( errno ) );
		return 1;
	}
	
	if( !( g_scriptData = fs_readfile( g_scriptFile ) ) )
	{
		fprintf( stderr, "ERROR: failed to open script file '%s' for writing (%s)\n", g_scriptFile, strerror( errno ) );
		return 1;
	}
	g_scriptLength = strlen( g_scriptData );
	
	// SCRIPT - COMPILE
	RS = rsl_create();
	res = rsl_compile( &RS, g_scriptData, &lineno );
	if( g_dumpScriptPreproc )
	{
		printf( "~~~~\n%s\n~~~~\n", g_scriptData );
		rsl_dump( &RS );
	}
	rsl_destroy( &RS );
	if( handle_script_error( res, lineno ) )
		return 1;
	
	//////////////////////////////////////////////////////////////////////
	// SCRIPT - IMAGES
	if( fs_setpath( &g_pImgDir, g_imageDir ) || fs_setcwd( &g_pImgDir ) )
	{
		fprintf( stderr, "ERROR: failed to open image directory '%s' (%s)\n", g_pImgDir.path, strerror( errno ) );
		return 1;
	}
	// >>>
	{
		srx_Context* rx_images = srx_Create( "@([^ \t\r\n]+)", "i" );
		if( !rx_images )
			return eputs( "INTERNAL ERROR: failed to compile rx_images" );
		
		g_scriptData2 = (char*) malloc( g_scriptLength + 1 );
		g_scriptLength2 = 0;
		size_t offset = 0;
		char* pout = g_scriptData2;
		size_t outbufsize = g_scriptLength;
		const char* pin = g_scriptData;
		
#define B_IMG_APPEND( from, to ) \
	if( outbufsize < ( (to) - (from) ) + ( pout - g_scriptData2 ) ) \
	{ \
		outbufsize *= 2; \
		size_t cwsize = pout - g_scriptData2; \
		char* nbuf = (char*) malloc( outbufsize + 1 ); \
		memcpy( nbuf, g_scriptData2, cwsize ); \
		free( g_scriptData2 ); \
		g_scriptData2 = nbuf; \
		pout = nbuf + cwsize; \
	} \
	memcpy( pout, (from), (to) - (from) ); \
	pout += (to) - (from); \
	g_scriptLength2 += (to) - (from);
		
		while( srx_MatchExt( rx_images, g_scriptData, g_scriptLength, offset ) )
		{
			const char *pbeg = NULL, *pend = NULL;
			if( !srx_GetCapturedPtrs( rx_images, 1, &pbeg, &pend ) )
				return eputs( "INTERNAL ERROR: failed to retrieve matched range" );
			
			offset = pend - g_scriptData;
			B_IMG_APPEND( pin, pbeg - 1 );
			pin = pend;
			
			// - find or add control
			int imgid = 0;
			size_t namelen = pend - pbeg;
			if( namelen > MAX_KEY_LENGTH - 1 )
				return eputs( "ERROR: max. image name length exceeded" );
			/* it can theoretically be longer but why should it be? causes problems more often than otherwise */
			
			for( ; imgid < g_numImages; ++imgid )
			{
				if( namelen == g_ctrlNameLengths[ imgid ] && !memcmp( g_ctrlNames[ imgid ], pbeg, namelen ) )
					break;
			}
			if( imgid == g_numImages ) // need to add one
			{
				if( g_numImages >= MAX_BITMAPS )
					return eputs( "ERROR: max. number of images exceeded" );
				
				char imgbuf[ MAX_KEY_LENGTH + 1 ];
				memcpy( imgbuf, pbeg, namelen );
				imgbuf[ namelen ] = 0;
				
				int ci = g_numImages;
				printf( "image> %s\n", imgbuf );
				if( img_load( imgbuf, &g_imageData[ ci ], &g_imageWidths[ ci ], &g_imageHeights[ ci ] ) )
					return 1;
				
				g_imageNames[ ci ] = pbeg;
				g_imageNameLengths[ ci ] = namelen;
				g_numImages++;
			}
			
			char bfr[ 8 ];
			sprintf( bfr, "%d", imgid );
			size_t bfr_size = strlen( bfr );
			B_IMG_APPEND( bfr, bfr + bfr_size );
		}
		
		srx_Destroy( rx_images );
		B_IMG_APPEND( g_scriptData + offset, g_scriptData + g_scriptLength );
		g_scriptData2[ g_scriptLength2 ] = 0;
	}
	//////////////////////////////////////////////////////////////////////
	// SCRIPT - CONTROLS
	{
		srx_Context* rx_controls = srx_Create( "#([-_/a-zA-Z0-9\x80-\xFF]+)", "" );
		if( !rx_controls )
			return eputs( "INTERNAL ERROR: failed to compile rx_controls" );
		
		g_scriptData3 = (char*) malloc( g_scriptLength2 + 1 );
		g_scriptLength3 = 0;
		size_t offset = 0;
		char* pout = g_scriptData3;
		size_t outbufsize = g_scriptLength2;
		const char* pin = g_scriptData2;
		
#define B_CTL_APPEND( from, to ) \
	if( outbufsize < ( (to) - (from) ) + ( pout - g_scriptData3 ) ) \
	{ \
		outbufsize *= 2; \
		size_t cwsize = pout - g_scriptData3; \
		char* nbuf = (char*) malloc( outbufsize + 1 ); \
		memcpy( nbuf, g_scriptData3, cwsize ); \
		free( g_scriptData3 ); \
		g_scriptData3 = nbuf; \
		pout = nbuf + cwsize; \
	} \
	memcpy( pout, (from), (to) - (from) ); \
	pout += (to) - (from); \
	g_scriptLength3 += (to) - (from);
		
		while( srx_MatchExt( rx_controls, g_scriptData2, g_scriptLength2, offset ) )
		{
			const char *pbeg = NULL, *pend = NULL;
			if( !srx_GetCapturedPtrs( rx_controls, 1, &pbeg, &pend ) )
				return eputs( "INTERNAL ERROR: failed to retrieve matched range" );
			
			offset = pend - g_scriptData2;
			B_CTL_APPEND( pin, pbeg );
			pin = pend;
			
			// - find or add control
			int ctlid = 0;
			size_t namelen = pend - pbeg;
			if( namelen > MAX_KEY_LENGTH - 1 )
				eputs( "ERROR: max. control name length exceeded" );
			
			for( ; ctlid < g_numControls; ++ctlid )
			{
				if( namelen == g_ctrlNameLengths[ ctlid ] && !memcmp( g_ctrlNames[ ctlid ], pbeg, namelen ) )
					break;
			}
			if( ctlid == g_numControls ) // need to add one
			{
				if( g_numControls >= MAX_CONTROLS )
					return eputs( "ERROR: max. number of controls exceeded" );
				g_ctrlNames[ g_numControls ] = pbeg;
				g_ctrlNameLengths[ g_numControls ] = namelen;
				g_numControls++;
			}
			
			char bfr[ 8 ];
			sprintf( bfr, "%d", ctlid );
			size_t bfr_size = strlen( bfr );
			B_CTL_APPEND( bfr, bfr + bfr_size );
		}
		
		srx_Destroy( rx_controls );
		B_CTL_APPEND( g_scriptData2 + offset, g_scriptData2 + g_scriptLength2 );
		g_scriptData3[ g_scriptLength3 ] = 0;
	}
	
	// SCRIPT - COMPILE AGAIN
	RS = rsl_create();
	res = rsl_compile( &RS, g_scriptData3, &lineno );
	if( g_dumpScriptCompiled )
	{
		printf( "~~~~\n%s\n~~~~\n", g_scriptData3 );
		rsl_dump( &RS );
	}
	rsl_destroy( &RS );
	if( handle_script_error( res, lineno ) )
		return 1;
	
	//////////////////////////////////////////////////////////////////////
	if( fs_setcwd( &g_pCurDir ) )
		return eputs( "ERROR: failed to reset current directory" );
	
	// - runner -> output
	if( fs_copy( g_outFileHandle, g_runnerFileHandle ) )
		return eputs( "ERROR: failed to copy (runner -> output)" );
	i32 MARK1 = ftell( g_outFileHandle );
	// - script -> output
	i32 len = g_scriptLength3;
	if( !fwrite( &len, 4, 1, g_outFileHandle ) )
		return eputs( "ERROR: failed to write (script size -> output)" );
	if( fwrite( g_scriptData3, 1, g_scriptLength3, g_outFileHandle ) != g_scriptLength3 )
		return eputs( "ERROR: failed to copy (script -> output)" );
	// - images -> output
	for( i = 0; i < g_numImages; ++i )
	{
		int alpha = g_imageData[ i ][0];
		i32 rowlen = alpha ? g_imageWidths[ i ] * 4 : ( ( g_imageWidths[ i ] * 3 + 3 ) / 4 ) * 4;
		i32 imgdatasize = rowlen * g_imageHeights[ i ] + 1;
		i32 wrbuf[3] = { imgdatasize, g_imageWidths[ i ], g_imageHeights[ i ] };
		if( !fwrite( wrbuf, 12, 1, g_outFileHandle ) )
		{
			fprintf( stderr, "ERROR: failed to write image %d (%.*s) header (12 bytes)\n", i, (int) g_imageNameLengths[i], g_imageNames[i] );
			return 1;
		}
		if( !fwrite( g_imageData[ i ], imgdatasize, 1, g_outFileHandle ) )
		{
			fprintf( stderr, "ERROR: failed to write image %d (%.*s) data (%d bytes)\n", i, (int) g_imageNameLengths[i], g_imageNames[i], imgdatasize );
			return 1;
		}
	}
	len = 0;
	if( !fwrite( &len, 4, 1, g_outFileHandle ) )
		return eputs( "ERROR: failed to write image ending marker" );
	
	// FILES
	if( fs_setpath( &g_pFileDir, g_fileDir ) || fs_setcwd( &g_pFileDir ) )
	{
		fprintf( stderr, "ERROR: failed to open file directory '%s' (%s)\n", g_fileDir, strerror( errno ) );
		return 1;
	}
	
#define TMP_ARCHIVE_NAME "__nlr_tmparch.7z"
	remove( TMP_ARCHIVE_NAME );
	printf( "\nRunning embedded archiver: " );
	if( ar7z_compress_dir() )
		return 1;
	g_archFileHandle = fopen( TMP_ARCHIVE_NAME, "rb" );
	if( !g_archFileHandle )
	{
		remove( TMP_ARCHIVE_NAME );
		return eputs( "ERROR: failed to open generated archive" );
	}
	if( fs_copy( g_outFileHandle, g_archFileHandle ) )
		return eputs( "ERROR: failed to copy archive" );
	fclose( g_archFileHandle );
	g_archFileHandle = NULL;
	remove( TMP_ARCHIVE_NAME );
	
	// ENDING
	i32 MARK2 = ftell( g_outFileHandle );
	i32 APPLEN = MARK2 - MARK1;
	if( !fwrite( &APPLEN, 4, 1, g_outFileHandle ) || !fwrite( "E0F1", 4, 1, g_outFileHandle ) )
		return eputs( "ERROR: failed to write file ending" );
	
	printf( "Output file successfully written to '%s'!\n", g_outputFile );
	return 0;
}


int print_help()
{
	puts( "syntax:" );
	puts( INDENT "nailer-builder <options>" );
	puts( "options:" );
	puts( INDENT "-o, --output - specify output file [REQUIRED]" );
	puts( INDENT "-i, --images - specify image directory [default=images]" );
	puts( INDENT "-f, --files - specify file directory [default=files]" );
	puts( INDENT "-s, --script - specify script file name [default=script.txt]" );
	puts( INDENT "-d, --dump - dump preprocessed and compiled versions of the script" );
	puts( INDENT "-?, -h, --help - show this help screen" );
	return 0;
}
