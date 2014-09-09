

#include "filesys.h"
#include "window.h"


#pragma pack(push, 1)

#define DWORD unsigned int
#define LONG int
#define WORD short

typedef struct tagBITMAPFILEHEADER
{
	WORD  bfType;
	DWORD bfSize;
	WORD  bfReserved1;
	WORD  bfReserved2;
	DWORD bfOffBits;
}
BITMAPFILEHEADER;

typedef struct _BITMAPINFOHEADER
{
	DWORD biSize;
	LONG  biWidth;
	LONG  biHeight;
	WORD  biPlanes;
	WORD  biBitCount;
	DWORD biCompression;
	DWORD biSizeImage;
	LONG  biXPelsPerMeter;
	LONG  biYPelsPerMeter;
	DWORD biClrUsed;
	DWORD biClrImportant;
}
BITMAPINFOHEADER;

#pragma pack(pop)


char* g_scriptData = NULL;
int g_imageCount = 0;
int g_imageWidths[ MAX_BITMAPS ] = {0};
int g_imageHeights[ MAX_BITMAPS ] = {0};
byte* g_imageData[ MAX_BITMAPS ] = {0};


static char* loadfilestr( const char* name )
{
	FILE* f = fopen( name, "rb" );
	X_TRACE_S( "filesys - loading file", name );
	if( !f )
	{
		X_TRACE( "file NOT FOUND" );
		return NULL;
	}
	fseek( f, 0, SEEK_END );
	size_t sz = ftell( f );
	X_DBG( printf( "file size: %d\n", (int) sz ) );
	fseek( f, 0, SEEK_SET );
	char* data = X_ALLOC_N( char, sz + 1 );
	if( !fread( data, sz, 1, f ) )
	{
		X_TRACE( "could not read file" );
		X_FREE( data );
		fclose( f );
		return 0;
	}
	data[ sz ] = 0;
	return data;
}

static int loadimagebmp( int i )
{
	char path[ 32 ] = {0};
	sprintf( path, "testdata/%d.bmp", i );
	
	char* data = loadfilestr( path );
	if( data )
	{
		BITMAPFILEHEADER* bfh = (BITMAPFILEHEADER*) data;
		BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*) ( data + sizeof(BITMAPFILEHEADER) );
		
		if( bfh->bfType != 0x4D42 )
		{
			X_TRACE( "NOT A BITMAP" );
			X_FREE( data );
			return 0;
		}
		
		X_DBG( printf( "is a bitmap, width = %d, height = %d"
			", bit count = %d, compression = %d\n",
			(int) bih->biWidth, (int) bih->biHeight,
			(int) bih->biBitCount, (int) bih->biCompression ) );
		
		if( ( bih->biCompression != 0 && bih->biCompression != 3 ) || ( bih->biBitCount != 24 && bih->biBitCount != 32 ) )
		{
			X_TRACE( "COMPRESSED/INDEXED BITMAP, NOT SUPPORTED" );
			X_FREE( data );
			return 0;
		}
		
		int rowlen = bih->biBitCount == 32 ? bih->biWidth * 4 : ( ( bih->biWidth * 3 + 3 ) / 4 ) * 4;
		byte* bmdata = X_ALLOC_N( byte, 1 + rowlen * bih->biHeight );
		bmdata[ 0 ] = bih->biBitCount == 32;
		memcpy( bmdata + 1, data + sizeof(BITMAPFILEHEADER) + bih->biSize, rowlen * bih->biHeight );
		
		g_imageData[ g_imageCount ] = bmdata;
		g_imageWidths[ g_imageCount ] = bih->biWidth;
		g_imageHeights[ g_imageCount ] = bih->biHeight;
		g_imageCount++;
		X_DBG( printf( "new image count: %d\n", (int) g_imageCount ) );
		
		X_FREE( data );
		return 1;
	}
	return 0;
}


int fs_init()
{
	g_scriptData = loadfilestr( "testdata/script.txt" );
	
	int i = 0;
	while( i < MAX_BITMAPS && loadimagebmp( i++ ) );
	
	return fsc_open_archive( fopen( "testdata/files.7z", "rb" ) );
}

void fs_free()
{
	fsc_close_archive();
	X_FREE( g_scriptData );
}

