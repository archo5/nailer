

#pragma once

#include "config.h"


/* MAX_BITMAPS @ config.h */

extern char* g_scriptData;
extern int g_imageCount;
extern int g_imageWidths[ MAX_BITMAPS ];
extern int g_imageHeights[ MAX_BITMAPS ];
extern byte* g_imageData[ MAX_BITMAPS ];

typedef int (*fs_progress_callback) ( int /* numerator */, int /* denominator */, const char* /* utf8 file name */ );

int fs_init();
void fs_free();

/* common functions */
int fsc_open_archive( FILE* fh );
void fsc_close_archive();
int fsc_extract_files( int test, fs_progress_callback pcb ); /* extracts files to current directory / tests integrity of files */

