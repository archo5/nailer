

#include "filesys.h"
#include "window.h"




char* g_scriptData = NULL;
int g_imageCount = 0;
int g_imageWidths[ MAX_BITMAPS ] = {0};
int g_imageHeights[ MAX_BITMAPS ] = {0};
byte* g_imageData[ MAX_BITMAPS ] = {0};
FILE* g_exeFile = NULL;


static i32 freadint( FILE* f )
{
	i32 out = 0;
	fread( &out, 4, 1, f );
	return out;
}

static int fsiERROR( const char* txt )
{
	puts( "--------" );
	puts( "FILE SYS ERROR" );
	puts( txt );
	puts( "--------" );
	return 1;
}

int fs_init()
{
	g_exeFile = platfs_exereadfile();
	if( !g_exeFile )
		return 1;
	
	if( fseek( g_exeFile, -4, SEEK_END ) ) return fsiERROR( "seek error 1" );
	char bytes[4];
	if( !fread( bytes, 1, 4, g_exeFile ) || memcmp( bytes, "E0F1", 4 ) ) return fsiERROR( "ending mark not found" );
	if( fseek( g_exeFile, -8, SEEK_END ) ) return fsiERROR( "seek error 1.1" );
	i32 filesize = ftell( g_exeFile ) + 8;
	i32 datasize = freadint( g_exeFile );
	if( datasize <= 0 || datasize > filesize - 8 ) return fsiERROR( "invalid data size" );
	if( fseek( g_exeFile, -datasize - 8, SEEK_END ) ) return fsiERROR( "seek error 2" );
	
	// load script
	i32 scriptsize = freadint( g_exeFile );
	if( scriptsize < 0 || scriptsize > datasize - 8 ) return fsiERROR( "invalid script size" );
	g_scriptData = X_ALLOC_N( char, scriptsize + 1 );
	if( !fread( g_scriptData, scriptsize, 1, g_exeFile ) ) return fsiERROR( "failed to read script" );
	g_scriptData[ scriptsize ] = 0;
	
	// load images
	i32 lastimgsize = 0;
	while( 0 != ( lastimgsize = freadint( g_exeFile ) ) )
	{
		if( lastimgsize < 9 || lastimgsize > datasize ) return fsiERROR( "invalid image size" );
		i32 iw = freadint( g_exeFile ); if( iw < 1 || iw > 4096 ) return fsiERROR( "invalid image width" );
		i32 ih = freadint( g_exeFile ); if( iw < 1 || iw > 4096 ) return fsiERROR( "invalid image height" );
		int ch = fgetc( g_exeFile ); if( ch != 0 && ch != 1 ) return fsiERROR( "invalid image format" );
		i32 rowlen;
		if( ch == 0 )
		{
			// 24-bit RGB, pitch is the closest multiple of 4
			rowlen = ( ( iw * 3 + 3 ) / 4 ) * 4;
		}
		else // ch == 1
		{
			// 32-bit premultiplied RGBA, pitch = width
			rowlen = iw * 4;
		}
		if( lastimgsize != rowlen * ih + 1 ) return fsiERROR( "invalid image size" );
		byte* imgdata = X_ALLOC_N( byte, rowlen * ih + 1 );
		imgdata[0] = ch;
		if( fread( imgdata + 1, 1, rowlen * ih, g_exeFile ) != rowlen * ih ) return fsiERROR( "failed to read image" );
		
		int i = g_imageCount;
		g_imageData[ i ] = imgdata;
		g_imageWidths[ i ] = iw;
		g_imageHeights[ i ] = ih;
		g_imageCount++;
	}
	
	fsc_open_archive( g_exeFile );
	
	return 0;
}

void fs_free()
{
	fsc_close_archive();
	X_FREE( g_scriptData );
}

