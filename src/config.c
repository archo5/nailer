

#include "config.h"


variable_list vl_create()
{
	variable_list VL = { NULL, 0, 0 };
	return VL;
}

void vl_destroy( variable_list* VL )
{
	if( VL->vars )
	{
		size_t i;
		for( i = 0; i < VL->size; ++i )
		{
			X_FREE( VL->vars[i].key );
			X_FREE( VL->vars[i].val );
		}
		X_FREE( VL->vars );
	}
}

void vl_reserve( variable_list* VL, size_t size )
{
	if( size <= VL->size )
		return;
	variable* nvars = X_ALLOC_N( variable, size );
	if( VL->vars )
	{
		memcpy( nvars, VL->vars, sizeof(*nvars) * VL->size );
		X_FREE( VL->vars );
	}
	VL->vars = nvars;
	VL->mem = size;
}

int vl_set( variable_list* VL, const char* key, size_t key_len, const char* val, size_t val_len )
{
	int ret = 1;
	variable* V = vl_find( VL, key, key_len );
	if( !V )
	{
		if( VL->size == VL->mem )
			vl_reserve( VL, VL->mem ? VL->mem * 2 : 16 );
		V = VL->vars + VL->size++;
		V->key = X_ALLOC_N( char, key_len + 1 );
		memcpy( V->key, key, key_len );
		V->key[ key_len ] = 0;
		V->keysz = key_len;
		ret = !!val_len;
	}
	else
	{
		if( V->valsz == val_len && !memcmp( V->val, val, val_len ) )
			return 0;
		X_FREE( V->val );
	}
	V->val = X_ALLOC_N( char, val_len + 1 );
	memcpy( V->val, val, val_len );
	V->val[ val_len ] = 0;
	V->valsz = val_len;
	return ret;
}

variable* vl_find( variable_list* VL, const char* key, size_t key_len )
{
	size_t i;
	for( i = 0; i < VL->size; ++i )
	{
		variable* V = VL->vars + i;
		if( V->keysz == key_len && !memcmp( V->key, key, key_len ) )
			return V;
	}
	return NULL;
}

int vl_unset( variable_list* VL, const char* key, size_t key_len )
{
	variable* V = vl_find( VL, key, key_len );
	if( !V )
		return 0;
	X_FREE( V->key );
	X_FREE( V->val );
	VL->size--;
	if( V < VL->vars + VL->size )
		memmove( V, V + 1, ( VL->vars + VL->size - V ) * sizeof(*V) );
	return 1;
}


resolver rsl_create()
{
	resolver RS =
	{
		rsl_make_node( RSLT_NONE, 0, NULL, 0, NULL, 0 ),
		NULL, 0,
		vl_create(),
		
		X_ALLOC_N( rsl_node*, 16 ), 0, 16,
		
		0,
		
		{0}, {0}, 0, 0,
		
		NULL, NULL,
	};
	return RS;
}

void rsl_destroy( resolver* RS )
{
	size_t i;
	rsl_destroy_node( RS->root );
	for( i = 0; i < RS->depmapsz; ++i )
	{
		rsl_deplist* DM = RS->depmap + i;
		X_FREE( DM->key );
		X_FREE( DM->deps );
	}
	if( RS->depmap )
		X_FREE( RS->depmap );
	X_FREE( RS->invnodes );
	vl_destroy( &RS->varlist );
}


static void rsl_dump_ext( int level )
{
	while( level --> 0 )
		printf( "  " );
}

static const char* rsl_nodetypestr( int type )
{
	static const char* types[] = {
		"none", "set", "if", "else", "elseif", "endif",
		"and", "or", "equal", "notequal", "less",
		"lessequal", "grtr", "grtrequal", "nonzero",
	};
	if( type < 0 || type >= sizeof(types)/sizeof(types[0]) )
		return "<unknown>";
	return types[ type ];
}

static void rsl_dump_line( rsl_node* n, int level )
{
	while( n )
	{
		rsl_dump_ext( level );
		printf( rsl_nodetypestr( n->type ) );
		if( n->v1 )
			printf( " 1:\"%.*s\"", (int) n->v1sz, n->v1 );
		if( n->v2 )
			printf( " 2:\"%.*s\"", (int) n->v2sz, n->v2 );
		puts("");
		if( n->cond )
		{
			rsl_dump_ext( level ); puts( "(" );
			rsl_dump_line( n->cond, level + 1 );
			rsl_dump_ext( level ); puts( ")" );
		}
		if( n->sub )
		{
			rsl_dump_ext( level ); puts( "{" );
			rsl_dump_line( n->sub, level + 1 );
			rsl_dump_ext( level ); puts( "}" );
		}
		n = n->next;
	}
}

void rsl_dump( resolver* RS )
{
	rsl_dump_line( RS->root, 0 );
}

rsl_node* rsl_make_node( int type, int linenum, const char* v1, size_t v1_len, const char* v2, size_t v2_len )
{
	rsl_node* n = X_ALLOC_N( rsl_node, 1 );
	n->prev = NULL;
	n->next = NULL;
	n->parent = NULL;
	n->sub = NULL;
	n->cond = NULL;
	n->type = type;
	n->linenum = linenum;
	n->v1 = NULL;
	n->v2 = NULL;
	n->v1sz = 0;
	n->v2sz = 0;
	if( v1_len || v2_len )
	{
		n->v1 = X_ALLOC_N( char, v1_len + v2_len + 2 );
		n->v2 = n->v1 + v1_len + 1;
		if( v1_len )
		{
			memcpy( n->v1, v1, v1_len );
			n->v1[ v1_len ] = 0;
			n->v1sz = v1_len;
		}
		if( v2_len )
		{
			memcpy( n->v2, v2, v2_len );
			n->v2[ v2_len ] = 0;
			n->v2sz = v2_len;
		}
	}
	return n;
}

void rsl_destroy_node( rsl_node* n )
{
	if( !n ) return;
	if( n->next ) rsl_destroy_node( n->next );
	if( n->sub ) rsl_destroy_node( n->sub );
	if( n->cond ) rsl_destroy_node( n->cond );
	X_FREE( n->v1 );
	X_FREE( n );
}


#define IS_INLWS( c ) ((c)==' '||(c)=='\t')
#define IS_CMD( p, e, chk ) (!strncmp(p,chk,sizeof(chk)-1) && ((e-p==sizeof(chk)-1) || IS_INLWS((p)[sizeof(chk)-1])))
#define IS_WHITESPACE( c ) ((c)==' '||(c)=='\t'||(c)=='\n'||(c)=='\r')

#define STRING_TRIM( p, e ) \
	while( (p) < (e) && IS_WHITESPACE( *(p) ) ) (p)++; \
	while( (p) < (e) && IS_WHITESPACE( *((e)-1) ) ) (e)--;

static const char* strbuf_find( const char* p, size_t sz, const char* s )
{
	size_t ssz = strlen( s );
	const char *e = p + sz, *pe = p + sz - ssz;
	while( p <= pe )
	{
		if( !memcmp( p, s, ssz ) )
			return p;
		p++;
	}
	return e;
}

typedef struct _resolver_compile_state resolver_compile_state;

struct _resolver_compile_state
{
	int curline;
	rsl_node* curnode;
};

#define _IS2( type, t1, t2 ) ((type)==t1||(type)==t2)
#define _IS3( type, t1, t2, t3 ) ((type)==t1||(type)==t2||(type)==t3)

static void rsl_next_node( resolver_compile_state* RCS, rsl_node* N )
{
	rsl_node* P = RCS->curnode;
	P->next = N;
	N->prev = P;
	N->parent = P->parent;
	RCS->curnode = N;
}

static void rsl_sub_node( resolver_compile_state* RCS, rsl_node* N )
{
	rsl_node* P = RCS->curnode;
	P->sub = N;
	N->parent = P;
	RCS->curnode = N;
}

static void rsl_act_node( resolver_compile_state* RCS, rsl_node* N )
{
	if( _IS3( RCS->curnode->type, RSLT_IF, RSLT_ELSE, RSLT_ELSEIF ) )
		rsl_sub_node( RCS, N );
	else
		rsl_next_node( RCS, N );
}

static int rsl_find_split( const char* p, const char* e, const char** sb, const char** se, int* type )
{
	int bestitemtype = -1;
	int bestitempwr = 0;
	int pwrmult = 1, pwr;
	const char* bestsplitpos = NULL;
	
	X_TRACE_SB( "find split on", p, e );
	
//	const char* b = p;
	while( p < e )
	{
		if( ( *p == '(' /*&& ( p == b || *(p-1) != '$' )*/ ) || *p == ')' )
		{
			// subexpressions (only control strength of item)
			if( *p == '(' )
			{
				X_TRACE( "subexp++" );
				pwrmult++;
			}
			else
			{
				X_TRACE( "subexp--" );
				pwrmult--;
				if( pwrmult < 1 )
					return RSLE_BRACES;
			}
		}
		else if( *p == '&' || *p == '|' )
		{
			// logical op
			X_TRACE( "logical op" );
			pwr = 7 + pwrmult * 10;
			if( pwr >= bestitempwr )
			{
				X_TRACE( "is better" );
				bestitemtype = *p == '&' ? RSLT_AND : RSLT_OR;
				bestsplitpos = p;
				bestitempwr = pwr;
			}
		}
		else if( *p == '=' || *p == '<' || *p == '>' || ( *p == '!' && p<e-1 && p[1] == '=' ) )
		{
			// comparison
			X_TRACE( "comparison" );
			pwr = 4 + pwrmult * 10;
			if( pwr >= bestitempwr )
			{
				X_TRACE( "is better" );
				if( *p == '=' ) bestitemtype = RSLT_EQUAL;
				else if( *p == '<' ) bestitemtype = p<e-1 && p[1] == '=' ? RSLT_LESSEQUAL : RSLT_LESS;
				else if( *p == '>' ) bestitemtype = p<e-1 && p[1] == '=' ? RSLT_GRTREQUAL : RSLT_GRTR;
				else if( *p == '!' && p<e-1 && p[1] == '=' ) bestitemtype = RSLT_NOTEQUAL;
				else
				{
					X_TRACE( "find split - unrecognized comparison" );
					/* software error? */
					return RSLE_UNEXP;
				}
				bestsplitpos = p;
				bestitempwr = pwr;
			}
		}
		p++;
	}
	
	if( pwrmult != 1 )
	{
		X_TRACE( "brace mismatch" );
		return RSLE_BRACES;
	}
	
	if( bestsplitpos )
	{
		*type = bestitemtype;
		*sb = bestsplitpos;
		*se = bestsplitpos + ( _IS3( bestitemtype, RSLT_NOTEQUAL, RSLT_LESSEQUAL, RSLT_GRTREQUAL ) ? 2 : 1 );
		X_TRACE_SB( "best split on", *sb, *se );
		X_TRACE_SB( "..before", b, *sb );
		X_TRACE_SB( "..after", *se, e );
		return 1;
	}
	X_TRACE( "no split" );
	return 0;
}

static int rsl_compile_expr( resolver_compile_state* RCS, rsl_node** outN, const char* p, const char* e )
{
	X_TRACE_SB( "compiling expression", p, e );
	
	int type, err;
	const char *sb, *se;
	STRING_TRIM( p, e );
	if( ( err = rsl_find_split( p, e, &sb, &se, &type ) ) < 1 )
	{
		/* just a string */
		*outN = NULL;
		return 1;
	}
	
	rsl_node *n, *nA, *nB;
	err = rsl_compile_expr( RCS, &nA, p, sb );
	if( err < 0 ) return err;
	err = rsl_compile_expr( RCS, &nB, se, e );
	if( err < 0 ) return err;
	n = rsl_make_node( type, RCS->curline, !nA ? p : NULL, !nA ? (sb-p) : 0, !nB ? se : NULL, !nB ? (e-se) : 0 );
	if( !nA && nB )
	{
		nA = nB;
		nB = NULL;
	}
	if( nA )
	{
		n->sub = nA;
		nA->parent = nB;
		if( nB )
		{
			nB->prev = nA;
			nA->next = nB;
		}
	}
	
	*outN = n;
	return 0;
}

static int rsl_compile_line( resolver* RS, resolver_compile_state* RCS, const char* p, const char* e )
{
	int err;
	rsl_node* n;
	
	X_TRACE_SB( "compiling line", p, e );
	
	if( IS_CMD( p, e, "if" ) )
	{
		X_TRACE( "'if'" );
		if( e - p <= 2 )
		{
			X_TRACE( "it's empty" );
			return RSLE_NOEXP;
		}
		n = rsl_make_node( RSLT_IF, RCS->curline, NULL, 0, NULL, 0 );
		rsl_next_node( RCS, n );
		err = rsl_compile_expr( RCS, &n->cond, p + 2, e );
		if( err == 1 )
		{
			p += 2;
			STRING_TRIM( p, e );
			n->cond = rsl_make_node( RSLT_NONZERO, RCS->curline, p, e-p, NULL, 0 );
			err = 0;
		}
		if( err )
			return err;
	}
	else if( IS_CMD( p, e, "else" ) )
	{
		X_TRACE( "'else'" );
		if( RCS->curnode && RCS->curnode->parent && !_IS2( RCS->curnode->parent->type, RSLT_IF, RSLT_ELSEIF ) )
		{
			X_TRACE( "unbased" );
			return RSLE_UNBASED;
		}
		RCS->curnode = RCS->curnode->parent;
		if( e - p > 4 )
		{
			X_TRACE( "it's not empty" );
			return RSLE_UNEXP;
		}
		n = rsl_make_node( RSLT_ELSE, RCS->curline, NULL, 0, NULL, 0 );
		rsl_next_node( RCS, n );
	}
	else if( IS_CMD( p, e, "elseif" ) || IS_CMD( p, e, "elif" ) )
	{
		X_TRACE( "'elseif'" );
		if( RCS->curnode && RCS->curnode->parent && !_IS2( RCS->curnode->parent->type, RSLT_IF, RSLT_ELSEIF ) )
		{
			X_TRACE( "unbased" );
			return RSLE_UNBASED;
		}
		RCS->curnode = RCS->curnode->parent;
		size_t cmdlen = IS_CMD( p, e, "elseif" ) ? 6 : 4;
		if( e - p <= cmdlen )
		{
			X_TRACE( "it's empty" );
			return RSLE_NOEXP;
		}
		n = rsl_make_node( RSLT_ELSEIF, RCS->curline, NULL, 0, NULL, 0 );
		rsl_next_node( RCS, n );
		err = rsl_compile_expr( RCS, &n->cond, p + cmdlen, e );
		if( err == 1 )
		{
			p += cmdlen;
			STRING_TRIM( p, e );
			n->cond = rsl_make_node( RSLT_NONZERO, RCS->curline, p, e-p, NULL, 0 );
			err = 0;
		}
		if( err )
			return err;
	}
	else if( IS_CMD( p, e, "endif" ) )
	{
		X_TRACE( "'endif'" );
		if( RCS->curnode && RCS->curnode->parent && !_IS3( RCS->curnode->parent->type, RSLT_IF, RSLT_ELSE, RSLT_ELSEIF ) )
		{
			X_TRACE( "unbased" );
			return RSLE_UNBASED;
		}
		RCS->curnode = RCS->curnode->parent;
		if( e - p > 5 )
		{
			X_TRACE( "it's not empty" );
			return RSLE_UNEXP;
		}
		n = rsl_make_node( RSLT_ENDIF, RCS->curline, NULL, 0, NULL, 0 );
		rsl_next_node( RCS, n );
	}
	else /* assignment? */
	{
		const char *kb, *ke, *vb, *ve, *pos;
		X_TRACE( "assignment?" );
		
		pos = strbuf_find( p, e-p, "=" );
		if( pos >= e )
		{
			X_TRACE( "nope!" );
			return RSLE_NOEXP;
		}
		kb = p;
		ke = pos;
		vb = pos + 1;
		ve = e;
		
		STRING_TRIM( kb, ke );
		STRING_TRIM( vb, ve );
		
		X_TRACE_SB( "key:", kb, ke );
		X_TRACE_SB( "value:", vb, ve );
		
		n = rsl_make_node( RSLT_SET, RCS->curline, kb, ke-kb, vb, ve-vb );
		rsl_act_node( RCS, n );
	}
	return RSLE_NONE;
}

int rsl_compile( resolver* RS, const char* str, int* line )
{
	resolver_compile_state RCS = { 0, RS->root };
	int errcode;
	const char* p = str;
	while( *p )
	{
		RCS.curline++;
		
		/* find EOL */
		const char *e, *pend = p;
		while( *pend && *pend != '\n' && *pend != '\r' )
			pend++;
		
		/* trim line */
		e = pend;
		while( p < e && IS_WHITESPACE( *p ) ) p++;
		while( p < e && IS_WHITESPACE( *(e-1) ) ) e--;
		
		/* parse trimmed line (p->e) if it's not a comment */
		if( p != e && ( p + 2 > e || ( p[0] != '-' && p[1] != '-' ) ) )
		{
			errcode = rsl_compile_line( RS, &RCS, p, e );
			if( errcode )
			{
				if( line )
					*line = RCS.curline;
				return errcode;
			}
		}
		
		/* skip line */
		if( *pend == '\n' )
			pend++;
		else if( *pend == '\r' )
			pend += pend[1] == '\n' ? 2 : 1;
		p = pend;
	}
	if( ( RCS.curnode->parent && RCS.curnode->parent->parent ) ||
		_IS3( RCS.curnode->type, RSLT_IF, RSLT_ELSEIF, RSLT_ELSE ) ||
		( RCS.curnode->parent && _IS3( RCS.curnode->parent->type, RSLT_IF, RSLT_ELSEIF, RSLT_ELSE ) ) )
	{
		X_TRACE( "incomplete file" );
		return RSLE_INCOMP;
	}
	return RSLE_NONE;
}


static int rsl_do( resolver* RS, rsl_node* N );


static int rsl_preproc( resolver* RS, int outbuf, const char* instr, size_t insz )
{
	const char* instrend = instr + insz;
	STRING_TRIM( instr, instrend );
	
	char preproc_buf[ MAX_KEY_LENGTH ];
	char* bufstart = outbuf ? RS->cmpbuf1 : RS->cmpbuf0;
	char* buf = bufstart;
	size_t* bsz = outbuf ? &RS->cb1sz : &RS->cb0sz;
	size_t charsleft = CMPBUF_SIZE - 1, n = 0;
	while( charsleft > 0 && instr < instrend )
	{
		if( *instr == '$' )
		{
			size_t kn = 0;
			if( instr < instrend-1 && instr[1] == '$' )
			{
				instr++;
				goto preproc_addchar;
			}
			else if( instr < instrend-1 && instr[1] == '(' )
			{
				instr += 2;
				size_t kcleft = MAX_KEY_LENGTH;
				while( kcleft > 0 && instr < instrend && *instr != ')' )
				{
					preproc_buf[ kn++ ] = *instr++;
				}
				if( !kcleft ) return RSLE_LIMIT;
				if( !kn ) return RSLE_NOEXP;
				if( instr >= instrend ) return RSLE_INCOMP;
				if( *instr != ')' ) return RSLE_INCOMP;
				instr++;
			}
			else
			{
				instr += 1;
				size_t kcleft = MAX_KEY_LENGTH;
				while( kcleft > 0 && instr < instrend && !IS_WHITESPACE( *instr ) )
				{
					preproc_buf[ kn++ ] = *instr++;
				}
				if( !kcleft ) return RSLE_LIMIT;
				if( !kn ) return RSLE_NOEXP;
			}
			
			variable* V = vl_find( &RS->varlist, preproc_buf, kn );
			if( V )
			{
				if( V->valsz > charsleft )
					return RSLE_LIMIT;
				memcpy( buf, V->val, V->valsz );
				buf += V->valsz;
				n += V->valsz;
				charsleft -= V->valsz;
			}
		}
		else
		{
preproc_addchar:
			*buf++ = *instr++;
			n++;
			charsleft--;
		}
	}
	bufstart[ n ] = 0;
	*bsz = n;
	return RSLE_NONE;
}

static int rsl_cmpbufs_equal( resolver* RS )
{
	return RS->cb0sz == RS->cb1sz && !memcmp( RS->cmpbuf0, RS->cmpbuf1, RS->cb0sz );
}

static int rsl_not_null( resolver* RS, rsl_node* N, int which )
{
	int res = rsl_preproc( RS, 0, which ? N->v2 : N->v1, which ? N->v2sz : N->v1sz );
	if( res < 0 )
		return res;
	X_DBG( printf( "NOT-NULL: \"%.*s\" (%d)\n", RS->cb0sz, RS->cmpbuf0, RS->cb0sz ) );
	return RS->cb0sz && ( RS->cmpbuf0[0] != '0' || RS->cmpbuf0[1] != 0 );
}

static int rsl_extract( resolver* RS, rsl_node* N, int which )
{
	X_ASSERT( which == 0 || which == 1 );
	if( N->sub )
	{
		if( N->sub->next )
			return which ? rsl_do( RS, N->sub->next ) : rsl_do( RS, N->sub );
		else
			return which ? rsl_do( RS, N->sub ) : ( rsl_not_null( RS, N, 0 ) || rsl_not_null( RS, N, 1 ) );
	}
	return rsl_not_null( RS, N, which );
}

static int rsl_do( resolver* RS, rsl_node* N )
{
	while( N )
	{
		X_DBG( printf( "DO %s", rsl_nodetypestr( N->type ) ) );
		if( N->type == RSLT_IF || N->type == RSLT_ELSEIF )
		{
			X_DBG( puts( ":" ) );
			int res = rsl_do( RS, N->cond );
			if( res < 0 )
				return res;
			
			if( res )
			{
				rsl_do( RS, N->sub );
				
				/* skip until ENDIF */
				while( N && N->type != RSLT_ENDIF )
					N = N->next;
				continue;
			}
			/* otherwise (if didn't hit), just go to next test */
		}
		else if( N->type == RSLT_ELSE )
		{
			rsl_do( RS, N->sub );
		}
		else if( N->type == RSLT_SET )
		{
			int res = rsl_preproc( RS, 1, N->v2, N->v2sz );
			if( res < 0 )
				return res;
			X_DBG( printf( " 1:\"%.*s\" (%d) 2:\"%.*s\" (%d)", N->v1sz, N->v1, N->v1sz, RS->cb1sz, RS->cmpbuf1, RS->cb1sz ) );
			vl_set( &RS->varlist, N->v1, N->v1sz, RS->cmpbuf1, RS->cb1sz );
			if( RS->change_cb )
				RS->change_cb( RS, N->v1, N->v1sz, RS->cmpbuf1, RS->cb1sz );
		}
		
		else if( N->type == RSLT_AND )
		{
			int res = rsl_extract( RS, N, 0 );
			if( res <= 0 )
				return res;
			res = rsl_extract( RS, N, 1 );
			return res;
		}
		else if( N->type == RSLT_OR )
		{
			int res = rsl_extract( RS, N, 0 );
			if( res != 0 )
				return res;
			res = rsl_extract( RS, N, 1 );
			return res;
		}
		
		else if( N->type == RSLT_EQUAL )
		{
			int res = rsl_preproc( RS, 0, N->v1, N->v1sz );
			if( res < 0 )
				return res;
			res = rsl_preproc( RS, 1, N->v2, N->v2sz );
			if( res < 0 )
				return res;
			res = rsl_cmpbufs_equal( RS );
			X_DBG( printf( " 1:\"%.*s\" 2:\"%.*s\" => %d\n", RS->cb0sz, RS->cmpbuf0, RS->cb1sz, RS->cmpbuf1, res ) );
			return res;
		}
		
		else if( N->type == RSLT_NONZERO )
		{
			return rsl_not_null( RS, N, 0 );
		}
		
		X_DBG( puts("") );
		N = N->next;
	}
	return RSLE_NONE;
}

int rsl_run( resolver* RS )
{
	return rsl_do( RS, RS->root );
}

#if 0
static void rsl_invalidate( resolver* RS, rsl_node* node )
{
	if( RS->invnodecount == RS->invnodemem )
	{
		RS->invnodemem *= 2;
		rsl_node** nlist = X_ALLOC_N( rsl_node*, RS->invnodemem );
		memcpy( nlist, RS->invnodes, sizeof(*nlist) * RS->invnodecount );
		X_FREE( RS->invnodes );
		RS->invnodes = nlist;
	}
	RS->invnodes[ RS->invnodecount++ ] = node;
}

static void rsl_reduce_invalidated( resolver* RS, rsl_node* N )
{
	size_t i;
	while( N )
	{
		for( i = 0; i < RS->invnodecount; ++i )
		{
			if( RS->invnodes[i] == N )
			{
				if( i < RS->invnodecount-1 )
					;
			}
		}
		if( N->sub ) rsl_reduce_invalidated( RS, N->sub );
		if( N->cond ) rsl_reduce_invalidated( RS, N->cond );
		N = N->next;
	}
}
#endif

void rsl_resolve( resolver* RS )
{
#if 0
	while( RS->invnodecount --> 0 )
		rsl_reduce_invalidated( RS, RS->invnodes[ RS->invnodecount ] );
	
	size_t i;
	for( i = 0; i < RS->invnodecount; ++i )
		rsl_do( RS, RS->invnodes[i] );
#endif
	if( RS->changed )
	{
		rsl_run( RS );
		RS->changed = 0;
	}
}

void rsl_set( resolver* RS, const char* key, size_t key_len, const char* val, size_t val_len )
{
	if( vl_set( &RS->varlist, key, key_len, val, val_len ) )
	{
		if( RS->change_cb )
			RS->change_cb( RS, key, key_len, val, val_len );
	}
#if 0
	size_t i, j;
	for( i = 0; i < RS->depmapsz; ++i )
	{
		rsl_deplist* D = RS->depmap + i;
		if( D->keysz == key_len )
		{
			for( j = 0; j < D->count; ++j )
				rsl_invalidate( RS, D->deps[j] );
		}
	}
#endif
}

