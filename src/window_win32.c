

#include <windows.h>
#include <windowsx.h>

#include "window.h"
#include "filesys.h"


#define WINDOW_CLASS "nailerWindow"


int g_closeRequested = FALSE;
int g_mouseX = 0;
int g_mouseY = 0;
HWND g_wnd = NULL;
HFONT g_font = NULL;
HBRUSH g_bgrBrush = NULL;
int g_bgrBitmapID = -1;
HPEN g_nullPen = NULL;
HDC g_bmpDC;

HBITMAP g_bitmaps[ MAX_BITMAPS ];

HANDLE g_subProcHandle = NULL;


typedef
struct _wintcontrol
{
	int x1, y1, x2, y2;
	HFONT font;
	int bgImage;
	WCHAR text[ MAX_CAPTION_SIZE_UTF16 ];
}
wintcontrol;

wcontrol g_controls[ MAX_CONTROLS ];
wintcontrol g_intControls[ MAX_CONTROLS ];
int g_numControls = 0;
wctl_action_callback g_winActCb = NULL;

wcontrol* g_curHover = NULL;
wcontrol* g_curClickedL = NULL;
wcontrol* g_curClickedR = NULL;
wcontrol* g_curClickedM = NULL;


#define UTF8_TO_WIDE( buf, bufsize, from ) \
	buf[ MultiByteToWideChar( CP_UTF8, 0, from, strlen( from ), buf, 1024 ) ] = 0;

static void win_change_brush( HBRUSH* pB, int r, int g, int b )
{
	if( *pB )
		DeleteObject( *pB );
	*pB = CreateSolidBrush( RGB( r, g, b ) );
}

static void win_draw_bitmap( int which, HDC hdcTo, int dstX, int dstY, int dstW, int dstH, int srcX, int srcY, int srcW, int srcH )
{
	if( which < 0 || which >= g_imageCount )
		return;
	
	SelectObject( g_bmpDC, g_bitmaps[ which ] );
	BITMAP bm;
	GetObject( g_bitmaps[ which ], sizeof(bm), &bm );
	if( dstW < 0 ) dstW = bm.bmWidth;
	if( dstH < 0 ) dstH = bm.bmHeight;
	if( srcW < 0 ) srcW = bm.bmWidth;
	if( srcH < 0 ) srcH = bm.bmHeight;
	if( !*g_imageData[ which ] ) /* if no alpha */
		StretchBlt( hdcTo, dstX, dstY, dstW, dstH, g_bmpDC, srcX, srcY, srcW, srcH, SRCCOPY );
	else
	{
		BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
		AlphaBlend( hdcTo, dstX, dstY, dstW, dstH, g_bmpDC, srcX, srcY, srcW, srcH, bf );
	}
}


static LRESULT CALLBACK windowproc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );

void win_initialize( int argc, char* argv[] )
{
	HINSTANCE hInstance;
	WNDCLASSEXA wc;
	
	// set up core
	g_bgrBrush = GetStockObject( BLACK_BRUSH );
	
	// make class
	hInstance = GetModuleHandle(NULL);
	memset( &wc, 0, sizeof(wc) );
	
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = windowproc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor( NULL, IDC_ARROW );
	wc.hbrBackground = NULL;
	wc.lpszClassName = WINDOW_CLASS;
	
	RegisterClassExA( &wc );
	X_TRACE( "window class registered" );
	
	// make window
	RECT wrect = { 0, 0, 400, 300 };
	AdjustWindowRect( &wrect, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 0 );
	g_wnd = CreateWindowExA
	(
		0, WINDOW_CLASS, WINDOW_CLASS,
		WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		300, 300, wrect.right - wrect.left, wrect.bottom - wrect.top,
		NULL, NULL, hInstance, NULL
	);
	X_TRACE( "window created" );
	
	HDC hdc = GetDC( g_wnd );
	
	// initialize font
	LOGFONT lf;
	memset( &lf, 0, sizeof(lf) );
	lf.lfQuality = CLEARTYPE_QUALITY; // ANTIALIASED_QUALITY; // CLEARTYPE_QUALITY;
	lf.lfHeight = 24;
	lf.lfOutPrecision = OUT_TT_PRECIS;
	strcpy( lf.lfFaceName, "Tahoma" );
	g_font = CreateFontIndirect( &lf );
	
	// initialize bitmaps
	int i;
	for( i = 0; i < g_imageCount; ++i )
	{
		byte* buf = g_imageData[i];
		int w = g_imageWidths[i], h = g_imageHeights[i];
		int alpha = *buf++;
		if( !alpha )
		{
			BITMAPINFOHEADER bih;
			memset( &bih, 0, sizeof(bih) );
			bih.biSize = sizeof(bih);
			bih.biWidth = w;
			bih.biHeight = h;
			bih.biPlanes = 1;
			bih.biBitCount = 24;
			
			BITMAPINFO bi;
			memset( &bi, 0, sizeof(bi) );
			bi.bmiHeader = bih;
			
			g_bitmaps[i] = CreateDIBitmap( hdc, &bih, CBM_INIT, buf, &bi, DIB_RGB_COLORS );
		}
		else
		{
			BITMAPINFO bmi;
			ZeroMemory(&bmi, sizeof(BITMAPINFO));
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = w;
			bmi.bmiHeader.biHeight = h;
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			bmi.bmiHeader.biCompression = BI_RGB;
			bmi.bmiHeader.biSizeImage = bmi.bmiHeader.biWidth * bmi.bmiHeader.biHeight * 4;
			
			byte* bmpdata = NULL;
			g_bitmaps[i] = CreateDIBSection( hdc, &bmi, DIB_RGB_COLORS, (void**) &bmpdata, NULL, 0 );
			int x, y;
			for( y = 0; y < h; ++y )
			{
				for( x = 0; x < w; ++x )
				{
					int pxo = ( x + y * w ) * 4;
					bmpdata[ pxo+0 ] = buf[ pxo+3 ] * buf[ pxo+0 ] / 255;
					bmpdata[ pxo+1 ] = buf[ pxo+2 ] * buf[ pxo+0 ] / 255;
					bmpdata[ pxo+2 ] = buf[ pxo+1 ] * buf[ pxo+0 ] / 255;
					bmpdata[ pxo+3 ] = buf[ pxo+0 ];
				}
			}
		}
	}
	
	// initialize other resources
	g_bmpDC = CreateCompatibleDC( hdc );
	g_nullPen = CreatePen( PS_NULL, 0, 0 );
	
	ReleaseDC( g_wnd, hdc );
	
	// display the window on the screen
	ShowWindow( g_wnd, SW_SHOW );
    UpdateWindow( g_wnd );
}

void win_destroy()
{
	int i;
	for( i = 0; i < g_imageCount; ++i )
	{
		DeleteObject( g_bitmaps[i] );
	}
	if( g_bgrBrush )
		DeleteObject( g_bgrBrush );
	X_TRACE( "window destroyed" );
	DestroyWindow( g_wnd );
}

int win_process( int peek )
{
	if( g_subProcHandle )
	{
		if( WaitForSingleObject( g_subProcHandle, 0 ) != WAIT_TIMEOUT )
		{
			CloseHandle( g_subProcHandle );
			g_subProcHandle = NULL;
			if( g_winActCb )
				g_winActCb( NULL, WA_PROC_EXIT, NULL );
		}
	}
	
	MSG msg;
	if( peek )
	{
		if( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) )
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );
			return 1;
		}
		return 0;
	}
	else
	{
		if( GetMessage( &msg, NULL, 0, 0 ) )
		{
			TranslateMessage( &msg );
			DispatchMessage( &msg );
		}
		return !g_closeRequested;
	}
}

void win_quit()
{
	SendMessage( g_wnd, WM_DESTROY, 0, 0 );
}


void win_set_title( const char* title )
{
	WCHAR wc_title[ 1024 ] = {0};
	UTF8_TO_WIDE( wc_title, 1024, title );
	SetWindowTextW( g_wnd, wc_title );
}

void win_set_size( int w, int h )
{
	RECT wrect = { 0, 0, w, h };
	AdjustWindowRect( &wrect, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 0 );
	SetWindowPos( g_wnd, NULL, 0, 0, wrect.right - wrect.left, wrect.bottom - wrect.top, SWP_NOMOVE | SWP_NOZORDER );
}

void win_set_background_image( int which )
{
	g_bgrBitmapID = which;
}

void win_set_background_color( int r, int g, int b )
{
	win_change_brush( &g_bgrBrush, r, g, b );
	RedrawWindow( g_wnd, NULL, NULL, RDW_ERASE | RDW_INVALIDATE );
}


static void _winctl_release( wcontrol* wc, wintcontrol* ic )
{
	if( g_curHover == wc ) g_curHover = NULL;
	if( g_curClickedL == wc ) g_curClickedL = NULL;
	if( g_curClickedR == wc ) g_curClickedR = NULL;
	if( g_curClickedM == wc ) g_curClickedM = NULL;
	if( ic->font ) DeleteObject( ic->font );
}

static void _winctl_init( wcontrol* wc, wintcontrol* ic )
{
	memset( wc, 0, sizeof(*wc) );
	memset( ic, 0, sizeof(*ic) );
	memset( wc->fgColor, 0xff, 3 );
	wc->bgImage = -1;
	wc->bgImageMode = 1;
}

static void _winctl_updatestate( wcontrol* wc )
{
	wc->state = wc->clickedL ? WST_CLICKED : ( wc->mouse_on ? WST_HOVER : WST_NORMAL );
}

void win_ctl_resize( int count )
{
	X_ASSERT( count >= 0 && count < MAX_CONTROLS );
	while( count < g_numControls )
	{
		g_numControls--;
		_winctl_release( &g_controls[ g_numControls ], &g_intControls[ g_numControls ] );
	}
	while( count > g_numControls )
	{
		_winctl_init( &g_controls[ g_numControls ], &g_intControls[ g_numControls ] );
		g_numControls++;
	}
}

void win_ctl_updated( int i, int what )
{
	wcontrol* wc = &g_controls[i];
	wintcontrol* ic = &g_intControls[i];
	
	if( what & WCU_RECT )
	{
		RECT rcp = { ic->x1, ic->y1, ic->x2, ic->y2 };
		InvalidateRect( g_wnd, &rcp, 0 );
		ic->x1 = wc->x1;
		ic->x2 = wc->x2;
		ic->y1 = wc->y1;
		ic->y2 = wc->y2;
	}
	if( what & WCU_TEXT )
	{
		wc->text[ MAX_CAPTION_SIZE_UTF8 - 1 ] = 0;
		UTF8_TO_WIDE( ic->text, MAX_CAPTION_SIZE_UTF16, wc->text );
	}
	
	RECT rcc = { wc->x1, wc->y1, wc->x2, wc->y2 };
	InvalidateRect( g_wnd, &rcc, 0 );
}

wcontrol* win_ctl_find( int x, int y )
{
	int i;
	for( i = g_numControls - 1; i >= 0; --i )
	{
		wcontrol* wc = &g_controls[i];
		if( !wc->type )
			continue;
		if( x >= wc->x1 && x < wc->x2 && y >= wc->y1 && y < wc->y2 )
			return wc;
	}
	return NULL;
}



static LRESULT CALLBACK windowproc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam )
{
	if( message == WM_DESTROY )
	{
		X_TRACE( "window destruction requested" );
		g_closeRequested = TRUE;
		return 0;
	}
	else if( message == WM_PAINT )
	{
		BITMAP bm;
		PAINTSTRUCT ps;
		HDC hdc_window = BeginPaint( g_wnd, &ps );
	//	X_DBG( printf( "WM_PAINT: %d;%d - %d;%d\n", (int) ps.rcPaint.left, (int) ps.rcPaint.top, (int) ps.rcPaint.right, (int) ps.rcPaint.bottom ) );
		
		RECT win_rect;
		GetWindowRect( g_wnd, &win_rect );
		HDC hdc = CreateCompatibleDC( hdc_window );
		HBITMAP canvas_bitmap = CreateCompatibleBitmap( hdc_window, win_rect.right, win_rect.bottom );
		SelectObject( hdc, canvas_bitmap );
		
		SelectObject( hdc, g_font );
		SelectObject( hdc, g_nullPen );
		SelectObject( hdc, g_bgrBrush );
		SetBkMode( hdc, TRANSPARENT );
		SetBkColor( hdc, RGB(0,0,0) );
		SetTextColor( hdc, RGB(0,0,0) );
		
		Rectangle( hdc, 0, 0, win_rect.right, win_rect.bottom );
		
		win_draw_bitmap( g_bgrBitmapID, hdc, 0, 0, -1, -1, 0, 0, -1, -1 );
//		win_draw_bitmap( 1, hdc, 370, 120, -1, -1, 0, 0, -1, -1 );
	//	SelectObject( g_bmpDC, g_bgrBitmap );
	//	GetObject( g_bgrBitmap, sizeof(bm), &bm );
	//	BitBlt( hdc, 0, 0, bm.bmWidth, bm.bmHeight, g_bmpDC, 0, 0, SRCCOPY );
	//	BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	//	printf( "%d-------\n",AlphaBlend( hdc, 0, 0, bm.bmWidth, bm.bmHeight, g_bmpDC, 0, 0, bm.bmWidth, bm.bmHeight, bf ));
		
	//	TextOutW( hdc, 0, 0, L"Hello, Windōws!!!", sizeof(L"Hello, Windōws!")/sizeof(WCHAR)-1 );
//		RECT rc = { 0, 0, 200, 100 };
//		DrawTextW( hdc, L"Hello, Windōws!", -1, &rc, DT_LEFT | DT_TOP );
//		RECT rc2 = { 4, 4, 200, 100 };
//		DrawTextW( hdc, L"Hello, Windōws!", -1, &rc2, DT_LEFT | DT_TOP );
		
		int i;
		for( i = 0; i < g_numControls; ++i )
		{
			wcontrol* wc = &g_controls[i];
			if( !wc->type )
				continue;
			wintcontrol* ic = &g_intControls[i];
			RECT wrc = { wc->x1, wc->y1, wc->x2, wc->y2 };
			
			// background color
			if( wc->bgColor[3] )
			{
				HBRUSH bgbrush = CreateSolidBrush( RGB( wc->bgColor[0], wc->bgColor[1], wc->bgColor[2] ) );
				SelectObject( hdc, bgbrush );
				Rectangle( hdc, wc->x1, wc->y1, wc->x2, wc->y2 );
				DeleteObject( bgbrush );
			}
			
			// background image
			if( wc->bgImage >= 0 && wc->bgImage < g_imageCount )
			{
				int img = wc->bgImage;
				if( WIM_IS_BUTTON_MODE( wc->bgImageMode ) )
				{
					int boff = WIM_GET_BUTTON_OFF( wc->bgImageMode ), dw = wc->x2 - wc->x1, dh = wc->y2 - wc->y1,
						iw = g_imageWidths[ img ], ih = g_imageHeights[ img ];
					
					// LT
					win_draw_bitmap( wc->bgImage, hdc, wc->x1, wc->y1, boff, boff, 0, 0, boff, boff );
					// CT
					win_draw_bitmap( wc->bgImage, hdc, wc->x1 + boff, wc->y1, dw - boff * 2, boff, boff, 0, iw - boff * 2, boff );
					// RT
					win_draw_bitmap( wc->bgImage, hdc, wc->x2 - boff, wc->y1, boff, boff, iw - boff, 0, boff, boff );
					
					// LC
					win_draw_bitmap( wc->bgImage, hdc, wc->x1, wc->y1 + boff, boff, dh - boff * 2, 0, boff, boff, ih - boff * 2 );
					// CC
					win_draw_bitmap( wc->bgImage, hdc, wc->x1 + boff, wc->y1 + boff, dw - boff * 2, dh - boff * 2, boff, boff, iw - boff * 2, ih - boff * 2 );
					// RC
					win_draw_bitmap( wc->bgImage, hdc, wc->x2 - boff, wc->y1 + boff, boff, dh - boff * 2, iw - boff, boff, boff, ih - boff * 2 );
					
					// LB
					win_draw_bitmap( wc->bgImage, hdc, wc->x1, wc->y2 - boff, boff, boff, 0, ih - boff, boff, boff );
					// CB
					win_draw_bitmap( wc->bgImage, hdc, wc->x1 + boff, wc->y2 - boff, dw - boff * 2, boff, boff, ih - boff, iw - boff * 2, boff );
					// RB
					win_draw_bitmap( wc->bgImage, hdc, wc->x2 - boff, wc->y2 - boff, boff, boff, iw - boff, ih - boff, boff, boff );
				}
				else
				{
					int dx = wc->x1, dy = wc->y1,
						dw = -1, dh = -1, sx = 0, sy = 0, sw = -1, sh = -1,
						iw = g_imageWidths[ img ], ih = g_imageHeights[ img ];
					switch( wc->bgImageMode )
					{
					case WIM_STRETCH:
						dw = wc->x2 - wc->x1;
						dh = wc->y2 - wc->y1;
						break;
					case WIM_TOPLEFT:
					case WIM_TOPRIGHT:
					case WIM_BOTTOMLEFT:
					case WIM_BOTTOMRIGHT:
						{
							dw = wc->x2 - wc->x1;
							dh = wc->y2 - wc->y1;
							int dx1 = ( wc->bgImageMode == WIM_TOPLEFT || wc->bgImageMode == WIM_BOTTOMLEFT ) ? dx : dx + dw - iw;
							int dx2 = ( wc->bgImageMode == WIM_TOPLEFT || wc->bgImageMode == WIM_BOTTOMLEFT ) ? dx + iw : dx + dw;
							int dy1 = ( wc->bgImageMode == WIM_TOPLEFT || wc->bgImageMode == WIM_TOPRIGHT ) ? dy : dy + dh - ih;
							int dy2 = ( wc->bgImageMode == WIM_TOPLEFT || wc->bgImageMode == WIM_TOPRIGHT ) ? dy + ih : dy + dh;
							if( dx1 < wc->x1 ) dx1 = wc->x1;
							if( dx2 > wc->x2 ) dx2 = wc->x2;
							if( dy1 < wc->y1 ) dy1 = wc->y1;
							if( dy2 > wc->y2 ) dy2 = wc->y2;
							dw = sw = dx2 - dx1;
							dh = sh = dy2 - dy1;
							dx = dx1; dy = dy1;
							if( wc->bgImageMode == WIM_TOPRIGHT || wc->bgImageMode == WIM_BOTTOMRIGHT ) sx = iw - sw;
							if( wc->bgImageMode == WIM_BOTTOMLEFT || wc->bgImageMode == WIM_BOTTOMRIGHT ) sy = ih - sh;
						}
						break;
					}
					win_draw_bitmap( wc->bgImage, hdc, dx, dy, dw, dh, sx, sy, sw, sh );
				}
			}
			
			// text
			if( wc->fgColor[3] )
			{
				SetTextColor( hdc, RGB( wc->fgColor[0], wc->fgColor[1], wc->fgColor[2] ) );
				DrawTextW( hdc, ic->text, -1, &wrc, DT_CENTER | DT_VCENTER | DT_SINGLELINE );
			}
		}
		
		GetObject( canvas_bitmap, sizeof(bm), &bm );
		BitBlt( hdc_window, 0, 0, bm.bmWidth, bm.bmHeight, hdc, 0, 0, SRCCOPY );
		DeleteObject( canvas_bitmap );
		DeleteDC( hdc );
		
		EndPaint( g_wnd, &ps );
		return 1;
	}
	else if( message == WM_MOUSEMOVE )
	{
		g_mouseX = GET_X_LPARAM( lParam );
		g_mouseY = GET_Y_LPARAM( lParam );
		
		if( !g_curClickedL && !g_curClickedR && !g_curClickedM )
		{
			wcontrol* wc = win_ctl_find( g_mouseX, g_mouseY );
			if( ( !wc || wc->type ) && wc != g_curHover )
			{
				if( g_curHover )
				{
					g_curHover->mouse_on = 0;
					_winctl_updatestate( g_curHover );
					if( g_winActCb )
						g_winActCb( g_curHover, WA_MOUSE_LEAVE, NULL );
				}
				if( wc )
				{
					wc->mouse_on = 1;
					_winctl_updatestate( wc );
					if( g_winActCb )
						g_winActCb( wc, WA_MOUSE_ENTER, NULL );
				}
				g_curHover = wc;
			}
		}
	//	RECT rr = { g_mouseX - 5, g_mouseY - 5, g_mouseX + 5, g_mouseY + 5 };
	//	InvalidateRect( g_wnd, &rr, 1 );
	}
	else if(
		message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
		message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ||
		message == WM_MBUTTONDOWN || message == WM_MBUTTONUP )
	{
		int isL = message == WM_LBUTTONDOWN || message == WM_LBUTTONUP;
		int isR = message == WM_RBUTTONDOWN || message == WM_RBUTTONUP;
		
		wcontrol** curclick = isL ? &g_curClickedL : ( isR ? &g_curClickedR : &g_curClickedM );
		int down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN;
		
		if( (*curclick) && !down )
		{
			if( isL ) (*curclick)->clickedL = 0;
			else if( isR ) (*curclick)->clickedR = 0;
			else (*curclick)->clickedM = 0;
			
			_winctl_updatestate( (*curclick) );
			if( g_winActCb )
				g_winActCb( *curclick, WA_MOUSE_BTNUP, NULL );
			if( isL && (*curclick) == win_ctl_find( g_mouseX, g_mouseY ) )
			{
				if( g_winActCb )
					g_winActCb( *curclick, WA_MOUSE_CLICK, NULL );
			}
			(*curclick) = NULL;
			SendMessage( hWnd, WM_MOUSEMOVE, 0, MAKELPARAM( g_mouseX, g_mouseY ) );
		}
		if( g_curHover && down )
		{
			(*curclick) = g_curHover;
			
			if( isL ) (*curclick)->clickedL = 1;
			else if( isR ) (*curclick)->clickedR = 1;
			else (*curclick)->clickedM = 1;
			
			_winctl_updatestate( g_curHover );
			if( g_winActCb )
				g_winActCb( *curclick, WA_MOUSE_BTNDN, NULL );
		}
	}
	return DefWindowProc( hWnd, message, wParam, lParam );
}


FILE* platfs_exereadfile()
{
	WCHAR buf16[ 32768 ];
	DWORD buf16_size;
	
	buf16_size = GetModuleFileNameW( NULL, buf16, 32768 );
	if( buf16_size == 0 || buf16_size >= 32768 )
	{
		X_TRACE( "CANNOT GET MODULE NAME" );
		return NULL;
	}
	buf16[ buf16_size ] = 0;
	
	return _wfopen( buf16, L"rb" );
}

int platfs_curdir( const char* path_utf8 )
{
	WCHAR pbuf[ 4096 ];
	UTF8_TO_WIDE( pbuf, 4096, path_utf8 );
	SetCurrentDirectoryW( pbuf );
	return 0;
}

int platfs_tmpdir()
{
	WCHAR tmproot[ MAX_PATH + 1 ] = {0}, tmpext[ MAX_PATH + 1 ] = {0};
	if( !GetTempPathW( MAX_PATH, tmproot ) )
		return 1;
	if( !GetTempFileNameW( tmproot, L"nsr", 0, tmpext ) )
		return 1;
	if( !DeleteFileW( tmpext ) )
		return 1;
	if( !CreateDirectoryW( tmpext, NULL ) )
		return 1;
	return !SetCurrentDirectoryW( tmpext );
}


static void _remdir()
{
	WIN32_FIND_DATAW wfd;
	HANDLE h = FindFirstFileW( L".\\*", &wfd );
	if( h != INVALID_HANDLE_VALUE )
	{
		do
		{
			if( *wfd.cFileName == L'.' && ( !wfd.cFileName[1] || ( wfd.cFileName[1] == L'.' && !wfd.cFileName[2] ) ) )
				continue;
			X_DBG( _putws( wfd.cFileName ) );
			if( wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			{
				if( SetCurrentDirectoryW( wfd.cFileName ) )
				{
					_remdir();
					SetCurrentDirectoryW( L".." );
					RemoveDirectoryW( wfd.cFileName );
				}
			}
			else
				DeleteFileW( wfd.cFileName );
		}
		while( FindNextFileW( h, &wfd ) != 0 );
		FindClose( h );
	}
}

int platfs_nukedir()
{
	WCHAR cwd[ MAX_PATH ];
	_remdir();
	GetCurrentDirectoryW( MAX_PATH, cwd );
	cwd[ MAX_PATH - 1 ] = 0;
	SetCurrentDirectoryW( L".." );
	RemoveDirectoryW( cwd );
	return 0;
}


int platfs_createdir_utf16( const unsigned short* path_utf16 )
{
	return CreateDirectoryW( path_utf16, NULL ) ? 0 : GetLastError();
}

FILE* g_wfile = NULL;

int platfs_openwritefile_utf16( const unsigned short* path_utf16 )
{
	if( g_wfile )
		return 1;
	g_wfile = _wfopen( path_utf16, L"wb" );
	if( !g_wfile )
		return 1;
	return 0;
}

int platfs_writefile( byte* buf, size_t num )
{
	return g_wfile && fwrite( buf, num, 1, g_wfile ) ? 0 : 1;
}

int platfs_closewritefile()
{
	if( !g_wfile )
		return 1;
	fclose( g_wfile );
	g_wfile = NULL;
	return 0;
}

int platfs_run( const char* cmd )
{
	if( g_subProcHandle )
		return 1;
	
	X_DBG( const char* cmde = cmd + strlen( cmd ) );
	X_TRACE_SB( "running a new process", cmd, cmde );
	
	WCHAR cmdw[ 4096 ];
	UTF8_TO_WIDE( cmdw, 4096, cmd );
	
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	
	memset( &si, 0, sizeof(si) );
	memset( &pi, 0, sizeof(pi) );
	si.cb = sizeof(si);
	
	if( !CreateProcessW( NULL, cmdw, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ) )
		return 1;
	CloseHandle( pi.hThread );
	g_subProcHandle = pi.hProcess;
	if( g_winActCb )
		g_winActCb( NULL, WA_PROC_LAUNCH, NULL );
	return 0;
}


