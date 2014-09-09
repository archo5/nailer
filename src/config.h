
#pragma once


#define MAX_BITMAPS 128
#define MAX_BITMAP_SIDE_SIZE 4096

#define MAX_CAPTION_SIZE 128
#define MAX_FONT_NAME_SIZE 128
#define MAX_CONTROLS 128

#define MAX_CAPTION_SIZE_UTF8 (MAX_CAPTION_SIZE*4)
#define MAX_CAPTION_SIZE_UTF16 (MAX_CAPTION_SIZE*2)

#define MAX_CHANGE_STACK 32


#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define X_MALLOC( n ) malloc( n )
#define X_ALLOC_N( ty, n ) (ty*) X_MALLOC( sizeof(ty) * n )
#define X_FREE( ptr ) free( ptr )
#define strlenfunc( str ) strlen( str )

#define strlitlen( str ) (str), (sizeof(str)-1)

#define MIN( a, b ) ((a)<(b)?(a):(b))


#define DEBUG_LEVEL 0

#if DEBUG_LEVEL > 0
# define X_DBG( x ) x
# define X_TRACE( str ) printf( "TRACE L%04d: %s\n", __LINE__, str )
# define X_TRACE_S( str, s ) printf( "TRACE L%04d: %s \"%.32s\"\n", __LINE__, str, s )
# define X_TRACE_SB( str, p, e ) printf( "TRACE L%04d: %s \"%.*s\"\n", __LINE__, str, MIN(32,(int)(e-p)), p )
# define X_ASSERT( test ) if(!(test)){ printf( "\n%s:%d: ASSERTION FAILED: " #test "\n\n", __FILE__, __LINE__ ); *(int*)0=0; }
#else
# define X_DBG( x )
# define X_TRACE( str )
# define X_TRACE_S( str, s )
# define X_TRACE_SB( str, p, e )
# define X_ASSERT( test )
#endif


typedef unsigned char byte;
typedef int i32;

/* --- key-value map (variable list) --- */

#define MAX_KEY_LENGTH 128

typedef struct _variable variable;
typedef struct _variable_list variable_list;

struct _variable
{
	char* key;
	size_t keysz;
	char* val;
	size_t valsz;
};

struct _variable_list
{
	variable* vars;
	size_t size, mem;
};


variable_list vl_create();
void vl_destroy( variable_list* VL );
void vl_reserve( variable_list* VL, size_t size );

int vl_set( variable_list* VL, const char* key, size_t key_len, const char* val, size_t val_len );
variable* vl_find( variable_list* VL, const char* key, size_t key_len );
#define vl_findcs( VL, key ) vl_find( VL, key, strlenfunc( key ) )
int vl_unset( variable_list* VL, const char* key, size_t key_len );
#define vl_unsetcs( VL, key ) vl_unset( VL, key, strlenfunc( key ) )


/* --- variable resolver --- */

#define CMPBUF_SIZE 1024

#define RSLE_NONE 0
#define RSLE_UNEXP -1
#define RSLE_UNBASED -2
#define RSLE_NOEXP -3
#define RSLE_INCOMP -4
#define RSLE_BRACES -5
#define RSLE_LIMIT -6

#define RSLT_NONE 0
#define RSLT_SET 1
#define RSLT_IF 2
#define RSLT_ELSE 3
#define RSLT_ELSEIF 4
#define RSLT_ENDIF 5
#define RSLT_AND 6
#define RSLT_OR 7
#define RSLT_EQUAL 8
#define RSLT_NOTEQUAL 9
#define RSLT_LESS 10
#define RSLT_LESSEQUAL 11
#define RSLT_GRTR 12
#define RSLT_GRTREQUAL 13
#define RSLT_NONZERO 14


typedef struct _rsl_node rsl_node;
typedef struct _rsl_deplist rsl_deplist;
typedef struct _resolver resolver;

typedef void (*rsl_change_cb) ( resolver*, const char*, size_t, const char*, size_t );

struct _rsl_node
{
	rsl_node* prev;
	rsl_node* next;
	rsl_node* parent;
	rsl_node* sub;
	rsl_node* cond;
	int type;
	int linenum;
	char* v1;
	size_t v1sz;
	char* v2;
	size_t v2sz;
};

struct _rsl_deplist
{
	char* key;
	size_t keysz;
	rsl_node** deps;
	size_t count;
};

struct _resolver
{
	rsl_node* root;
	rsl_deplist* depmap;
	size_t depmapsz;
	variable_list varlist;
	
	/* invalidated entry node list */
	rsl_node** invnodes;
	size_t invnodecount, invnodemem;
	
	/* whether was modified externally and needs to be reevaluated */
	int changed;
	
	/* comparison buffers */
	char cmpbuf0[ CMPBUF_SIZE ];
	char cmpbuf1[ CMPBUF_SIZE ];
	size_t cb0sz, cb1sz;
	
	/* change callback */
	rsl_change_cb change_cb;
	void* change_ud;
};

resolver rsl_create();
void rsl_destroy( resolver* RS );
void rsl_dump( resolver* RS );

rsl_node* rsl_make_node( int type, int linenum, const char* v1, size_t v1_len, const char* v2, size_t v2_len );
void rsl_destroy_node( rsl_node* n );

int rsl_compile( resolver* RS, const char* str, int* line );
int rsl_run( resolver* RS );
void rsl_resolve( resolver* RS );
void rsl_set( resolver* RS, const char* key, size_t key_len, const char* val, size_t val_len );

