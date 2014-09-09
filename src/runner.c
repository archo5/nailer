

#include <stdio.h>

#include "config.h"
#include "filesys.h"
#include "window.h"


#define ERROR_TMPDIR 10
#define ERROR_TMPDIR_TEXT "failed to create a temporary directory"
#define ERROR_RUN    11
#define ERROR_RUN_TEXT    "failed to start the application"


resolver g_RS;


int g_requested_changes = 0;
#define REQCHG_WINDOWSIZE  0x0001
#define REQCHG_CONTROLTYPE 0x0002

int g_window_width = 400;
int g_window_height = 300;
wcontrol* g_event_control = NULL;


static int clamp0255( int x ){ return x < 0 ? 0 : ( x > 255 ? 255 : x ); }

static int str_to_int_ext( const char* c, const char** out )
{
	while( *c == ' ' || *c == '\t' || *c == '\r' || *c == '\n' )
		c++;
	int val = 0, sign = 1;
	if( *c == '-' )
	{
		sign = -1;
		c++;
	}
	while( *c >= '0' && *c <= '9' )
	{
		val *= 10;
		val += *c++ - '0';
	}
	if( out )
		*out = c;
	return val * sign;
}
#define str_to_int( s ) str_to_int_ext( s, NULL )

static int enumstr_to_int( const char* test, const char* enumstr )
{
	int i = 0;
	while( *enumstr )
	{
		if( !strcmp( test, enumstr ) )
			return i;
		enumstr += strlen( enumstr ) + 1;
		i++;
	}
	return 0;
}


void emit_ERROR( int code, const char* text )
{
	char bfr[ 32 ] = {0};
	sprintf( bfr, "%d", code );
	vl_set( &g_RS.varlist, strlitlen( "error.code" ), bfr, strlen( bfr ) );
	vl_set( &g_RS.varlist, strlitlen( "error.text" ), text, strlen( text ) );
}


void runner_fsproc( int test );
void runner_run( const char* cmdline );

void runner_change_ctl_handler( resolver* RS, wcontrol* CTL, const char* key, size_t key_len, const char* val, size_t val_len );

int g_changeStack = 0;
int g_inTest = 0;
void runner_change_callback( resolver* RS, const char* key, size_t key_len, const char* val, size_t val_len )
{
	if( g_changeStack >= MAX_CHANGE_STACK )
	{
		fprintf( stderr, "ERROR: exceeded change stack (%d changes)\n", MAX_CHANGE_STACK );
		return;
	}
	g_changeStack++;
#define ISKEY( s ) ( key_len == sizeof(s)-1 && !memcmp( s, key, sizeof(s)-1 ) )
#define PARSEINT() str_to_int( val )
#define PARSEENUM( enumstr ) enumstr_to_int( val, enumstr )
#define NOTNULL() ( val_len && ( val[0] != '0' || val[1] != 0 ) )
	
	X_DBG( printf( "CHANGE \"%.*s\" = \"%.*s\"\n", key_len, key, val_len, val ) );
	
	if( ISKEY( "quit" ) ){ if( NOTNULL() ) win_quit(); }
	else if( ISKEY( "run" ) ){ if( NOTNULL() ) runner_run( val ); }
	else if( ISKEY( "test" ) )
	{
		if( NOTNULL() )
		{
			if( g_inTest )
			{
				fprintf( stderr, "ERROR: 'test' action recursion, cannot start testing in testing progress callback\n" );
				return;
			}
			g_inTest = 1;
			runner_fsproc( 1 );
			g_inTest = 0;
		}
	}
	else if( ISKEY( "window.width" ) ){ g_window_width = PARSEINT(); g_requested_changes |= REQCHG_WINDOWSIZE; }
	else if( ISKEY( "window.height" ) ){ g_window_height = PARSEINT(); g_requested_changes |= REQCHG_WINDOWSIZE; }
	else if( ISKEY( "window.image" ) ){ win_set_background_image( PARSEINT() ); }
	else if( ISKEY( "window.title" ) )
	{
		win_set_title( val );
	}
	else if( key_len && *key == '#' )
	{
		/* CONTROL SETTINGS */
		const char* nkey = NULL;
		int ctlid = str_to_int_ext( key + 1, &nkey );
		if( ctlid < 0 )
		{
			fprintf( stderr, "ERROR: invalid control id=%d\n", ctlid );
			goto end;
		}
		if( ctlid > MAX_CONTROLS )
		{
			fprintf( stderr, "ERROR: exceeded MAX_CONTROLS with id=%d\n", ctlid );
			goto end;
		}
		if( ctlid >= g_numControls )
			win_ctl_resize( ctlid + 1 );
		
		key_len -= nkey - key;
		key = nkey;
		runner_change_ctl_handler( RS, &g_controls[ ctlid ], key, key_len, val, val_len );
		
	}
	else if( key_len >= sizeof("event.control")-1 && !strncmp( key, "event.control", sizeof( "event.control" )-1 ) )
	{
		key += sizeof("event.control")-1;
		key_len -= sizeof("event.control")-1;
		runner_change_ctl_handler( RS, g_event_control, key, key_len, val, val_len );
	}
	
end:
	g_changeStack--;
}
void runner_change_ctl_handler( resolver* RS, wcontrol* CTL, const char* key, size_t key_len, const char* val, size_t val_len )
{
	if( !CTL )
		return;
	int ctlid = CTL - g_controls;
	
	if( ISKEY( ".type" ) )
	{
		int newtype = PARSEENUM( "none\0text\0button\0" );
		if( newtype != CTL->type )
		{
			CTL->type = newtype;
			win_ctl_updated( ctlid, WCU_TYPE );
			g_requested_changes |= REQCHG_CONTROLTYPE;
		}
	}
	else if( ISKEY( ".text" ) )
	{
		int len = MIN( MAX_CAPTION_SIZE_UTF8 - 1, val_len );
		memcpy( CTL->text, val, len );
		CTL->text[ len ] = 0;
		win_ctl_updated( ctlid, WCU_TEXT );
	}
	else if( ISKEY( ".rect" ) )
	{
		const char* pv = val;
		CTL->x1 = str_to_int_ext( pv, &pv );
		CTL->y1 = str_to_int_ext( pv, &pv );
		CTL->x2 = str_to_int_ext( pv, &pv );
		CTL->y2 = str_to_int_ext( pv, &pv );
		win_ctl_updated( ctlid, WCU_RECT );
	}
	else if( ISKEY( ".image" ) )
	{
		CTL->bgImage = PARSEINT();
		win_ctl_updated( ctlid, WCU_BG_IMAGE );
	}
	else if( ISKEY( ".imagemode" ) )
	{
		int imode = PARSEINT();
		if( !imode )
			imode = PARSEENUM( "stretch\0topleft\0topright\0bottomleft\0bottomright\0" );
		CTL->bgImageMode = imode;
		win_ctl_updated( ctlid, WCU_BG_IMGMODE );
	}
	else if( ISKEY( ".fgcolor" ) )
	{
		const char* pv = val;
		CTL->fgColor[0] = clamp0255( str_to_int_ext( pv, &pv ) );
		CTL->fgColor[1] = clamp0255( str_to_int_ext( pv, &pv ) );
		CTL->fgColor[2] = clamp0255( str_to_int_ext( pv, &pv ) );
		CTL->fgColor[3] = !!val_len;
		win_ctl_updated( CTL - g_controls, WCU_FG_COLOR );
	}
	if( ISKEY( ".bgcolor" ) )
	{
		const char* pv = val;
		CTL->bgColor[0] = clamp0255( str_to_int_ext( pv, &pv ) );
		CTL->bgColor[1] = clamp0255( str_to_int_ext( pv, &pv ) );
		CTL->bgColor[2] = clamp0255( str_to_int_ext( pv, &pv ) );
		CTL->bgColor[3] = !!val_len;
		win_ctl_updated( CTL - g_controls, WCU_BG_COLOR );
	}
	else if( ISKEY( ".image" ) )
	{
		CTL->bgImage = PARSEINT();
		win_ctl_updated( CTL - g_controls, WCU_BG_IMAGE );
	}
}


int g_applying_type_changes = 0;
void runner_apply_changes()
{
	if( g_requested_changes & REQCHG_WINDOWSIZE )
	{
		win_set_size( g_window_width, g_window_height );
		g_requested_changes &= ~REQCHG_WINDOWSIZE;
	}
	if( g_requested_changes & REQCHG_CONTROLTYPE && !g_applying_type_changes )
	{
		g_applying_type_changes = 1;
		int i;
		for( i = 0; i < g_numControls; ++i )
			g_winActCb( &g_controls[i], WA_CTRL_CREATED, NULL );
		g_requested_changes &= ~REQCHG_CONTROLTYPE;
		g_applying_type_changes = 0;
	}
}

int g_have_tmpfiles = 0;
void runner_action_callback( wcontrol* CTL, int action, int* data )
{
	char bfr[ 32 ] = {0};
	
	if( action == WA_PROC_LAUNCH || action == WA_PROC_EXIT )
	{
		if( action == WA_PROC_EXIT && g_have_tmpfiles )
		{
			if( platfs_nukedir() )
				; // ERROR
			else
				g_have_tmpfiles = 0;
		}
		const char* actstr = action == WA_PROC_LAUNCH ? "launch" : "exit";
		const char* statestr = action == WA_PROC_LAUNCH ? "1" : "";
		vl_set( &g_RS.varlist, strlitlen( "_action" ), actstr, strlen( actstr ) );
		vl_set( &g_RS.varlist, strlitlen( "_running" ), statestr, strlen( statestr ) );
		rsl_run( &g_RS );
		runner_apply_changes();
		return;
	}
	
	g_event_control = CTL;
	int ctlid = CTL - g_controls;
	
	const char* actstr = "<unknown>";
	switch( action )
	{
	case WA_MOUSE_CLICK: actstr = "clicked"; break;
	case WA_MOUSE_ENTER: actstr = "mouseenter"; break;
	case WA_MOUSE_LEAVE: actstr = "mouseleave"; break;
	case WA_MOUSE_BTNDN: actstr = "buttondown"; break;
	case WA_MOUSE_BTNUP: actstr = "buttonup"; break;
	case WA_CTRL_CREATED: actstr = "created"; break;
	}
	sprintf( bfr, "#%d.%s", ctlid, actstr );
	vl_set( &g_RS.varlist, strlitlen( "_action" ), bfr, strlen( bfr ) );
	
	sprintf( bfr, "#%d", ctlid );
	vl_set( &g_RS.varlist, strlitlen( "event.control" ), bfr, strlen( bfr ) );
	
	const char* typestr = "none";
	switch( CTL->type )
	{
	case WCTL_TEXT: typestr = "text"; break;
	case WCTL_BUTTON: typestr = "button"; break;
	}
	vl_set( &g_RS.varlist, strlitlen( "event.control.type" ), typestr, strlen( typestr ) );
	
	sprintf( bfr, "%d", CTL->state );
	vl_set( &g_RS.varlist, strlitlen( "event.control.state" ), bfr, strlen( bfr ) );
	
	rsl_run( &g_RS );
	
	vl_set( &g_RS.varlist, strlitlen( "_action" ), strlitlen( "" ) );
	vl_set( &g_RS.varlist, strlitlen( "event.control" ), strlitlen( "" ) );
	vl_set( &g_RS.varlist, strlitlen( "event.control.type" ), strlitlen( "" ) );
	vl_set( &g_RS.varlist, strlitlen( "event.control.state" ), strlitlen( "" ) );
	
	g_event_control = NULL;
	
	runner_apply_changes();
}


int g_isTesting = 0;
int runner_fsprog_callback( int numerator, int denominator, const char* utf8_filename )
{
	X_DBG( printf( "--------- %03d%% --- %s ---\n", 100 * numerator / denominator, utf8_filename ) );
	
	char bfr[ 32 ] = {0};
	
	if( g_isTesting )
		vl_set( &g_RS.varlist, strlitlen( "_action" ), strlitlen( "archive.test" ) );
	else
		vl_set( &g_RS.varlist, strlitlen( "_action" ), strlitlen( "archive.extract" ) );
	
	vl_set( &g_RS.varlist, strlitlen( "archive.cur_entry" ), utf8_filename, strlen( utf8_filename ) );
	
	sprintf( bfr, "%d", numerator );
	vl_set( &g_RS.varlist, strlitlen( "archive.num_processed" ), bfr, strlen( bfr ) );
	
	sprintf( bfr, "%d", denominator );
	vl_set( &g_RS.varlist, strlitlen( "archive.num_total" ), bfr, strlen( bfr ) );
	
	sprintf( bfr, "%d", 100 * numerator / denominator );
	vl_set( &g_RS.varlist, strlitlen( "archive.percent" ), bfr, strlen( bfr ) );
	
	rsl_run( &g_RS );
	runner_apply_changes();
	while( win_process( 1 ) );
	
	return 0;
}

void runner_fsproc( int test )
{
	g_isTesting = test;
	
	vl_set( &g_RS.varlist, strlitlen( "_action.done" ), strlitlen( "" ) );
	
	fsc_extract_files( test, runner_fsprog_callback );
	
	vl_set( &g_RS.varlist, strlitlen( "_action.done" ), strlitlen( "1" ) );
	runner_fsprog_callback( 2, 1, "100" );
	
	vl_set( &g_RS.varlist, strlitlen( "_action" ), strlitlen( "" ) );
}

void runner_run( const char* cmdline )
{
	X_TRACE( "creating a temporary directory" );
	if( platfs_tmpdir() )
	{
		emit_ERROR( ERROR_TMPDIR, ERROR_TMPDIR_TEXT );
		return;
	}
	
	char cmdlinecopy[ 1024 ] = {0};
	strncpy( cmdlinecopy, cmdline, 1023 );
	
	X_TRACE( "unpacking files" );
	runner_fsproc( 0 );
	g_have_tmpfiles = 1;
	
	X_TRACE( "starting the application" );
	if( platfs_run( cmdlinecopy ) )
	{
		emit_ERROR( ERROR_RUN, ERROR_RUN_TEXT );
		return;
	}
}


int main( int argc, char* argv[] )
{
	if( fs_init() )
		return 1;
	win_initialize( argc, argv );
	g_RS = rsl_create();
	g_RS.change_cb = runner_change_callback;
	g_winActCb = runner_action_callback;
	
	rsl_compile( &g_RS, g_scriptData, NULL );
	X_DBG( rsl_dump( &g_RS ) );
	
#if 0
	///////////////////////////////////////////////////
//	win_set_title( "Testā rešpekt" );
//	win_set_background_color( 120, 150, 180 );
//	win_set_background_image( 0 );
	
	win_ctl_resize( 2 );
	
	g_controls[0].type = WCTL_BUTTON;
	g_controls[0].x1 = 100;
	g_controls[0].y1 = 100;
	g_controls[0].x2 = 300;
	g_controls[0].y2 = 140;
	byte colors[ 24 ] =
	{
		50, 50, 50, 1,
		50, 50, 50, 1,
		50, 50, 50, 1,
		200, 200, 200, 1,
		220, 220, 220, 1,
		180, 180, 180, 1,
	};
	memcpy( g_controls[0].fgColorN, colors, 24 );
	strcpy( g_controls[0].text, "Spēlēt" );
	win_ctl_updated( 0, WCU_EVERYTHING );
	
	g_controls[1].type = WCTL_BUTTON;
	g_controls[1].x1 = 100;
	g_controls[1].y1 = 200;
	g_controls[1].x2 = 300;
	g_controls[1].y2 = 240;
	byte colors2[ 24 ] =
	{
		50, 30, 30, 1,
		60, 40, 40, 1,
		70, 50, 50, 1,
		190, 190, 210, 1,
		210, 210, 230, 1,
		170, 170, 190, 1,
	};
	memcpy( g_controls[1].fgColorN, colors2, 24 );
	strcpy( g_controls[1].text, "Instalēt" );
	win_ctl_updated( 1, WCU_EVERYTHING );
	///////////////////////////////////////////////////
#endif
	
	vl_set( &g_RS.varlist, strlitlen( "_action" ), strlitlen( "init" ) );
	rsl_run( &g_RS );
	vl_set( &g_RS.varlist, strlitlen( "_action" ), strlitlen( "" ) );
	runner_apply_changes();
	
	while( win_process( 0 ) );
	
	rsl_destroy( &g_RS );
	fs_free();
	win_destroy();
	return 0;
}
