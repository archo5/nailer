

#pragma once

#include "config.h"

/* MAX_* - config.h */

/* --- control types --- */
#define WCTL_NONE 0
#define WCTL_TEXT 1
#define WCTL_BUTTON 2

/* --- control states --- */
#define WST_NORMAL 0
#define WST_HOVER 1
#define WST_CLICKED 2

/* --- actions --- */
#define WA_MOUSE_CLICK 1
#define WA_MOUSE_ENTER 2
#define WA_MOUSE_LEAVE 3
#define WA_MOUSE_BTNDN 4
#define WA_MOUSE_BTNUP 5
#define WA_CTRL_CREATED 100 /* custom */
#define WA_PROC_LAUNCH 200
#define WA_PROC_EXIT 201

/* --- image modes --- */
#define WIM_STRETCH 0
#define WIM_TOPLEFT 1
#define WIM_TOPRIGHT 2
#define WIM_BOTTOMLEFT 3
#define WIM_BOTTOMRIGHT 4
#define WIM_IS_BUTTON_MODE(x) ((x)<0)
#define WIM_GET_BUTTON_OFF(x) (-(x))

/* --- update categories --- */
#define WCU_RECT        0x0001
#define WCU_FG_COLOR    0x0002
#define WCU_BG_COLOR    0x0004
#define WCU_BG_IMAGE    0x0008
#define WCU_BG_IMGMODE  0x0010
#define WCU_FONT        0x0020
#define WCU_TEXT        0x0040
#define WCU_TYPE        0x0080
#define WCU_EVERYTHING  0xffff


typedef struct _wcontrol wcontrol;
typedef void (*wctl_action_callback) ( wcontrol*, int /* action */, int* /* data */ );

struct _wcontrol
{
	/* control basic description */
	int type;
	int x1, y1, x2, y2;
	
	/* state */
	int state : 3;
	int mouse_on : 1;
	int clickedL : 1;
	int clickedR : 1;
	int clickedM : 1;
	
	/* visual overrides */
	byte fgColor[4];
	byte bgColor[4];
	int bgImage;
	int bgImageMode;
	char text[ MAX_CAPTION_SIZE_UTF8 ];
	char fontName[ MAX_FONT_NAME_SIZE ];
};


extern wcontrol g_controls[ MAX_CONTROLS ];
extern int g_numControls;
extern wctl_action_callback g_winActCb;


void win_initialize( int argc, char* argv[] );
void win_destroy();
int win_process( int peek );
void win_quit();

void win_set_title( const char* title );
void win_set_size( int w, int h );
void win_set_background_image( int which );
void win_set_background_color( int r, int g, int b );

void win_ctl_resize( int count );
void win_ctl_updated( int i, int what );


/* platform file system */
FILE* platfs_exereadfile();
int platfs_curdir( const char* path_utf8 ); // sets current directory to the specified (absolute) directory
int platfs_tmpdir(); // sets current directory to a temporary directory
int platfs_nukedir(); // changes current directory back to executable, removes contents of previous temporary directory and the directory itself

int platfs_createdir_utf16( const unsigned short* path_utf16 );
int platfs_openwritefile_utf16( const unsigned short* path_utf16 ); // opens internal file slot for writing
int platfs_writefile( byte* buf, size_t num ); // writes to opened file
int platfs_closewritefile(); // closes opened file

int platfs_run( const char* cmd );

