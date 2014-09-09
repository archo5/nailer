

#include "filesys.h"
#include "window.h"

#include "../lzma-sdk/C/7z.h"
#include "../lzma-sdk/C/7zAlloc.h"
#include "../lzma-sdk/C/7zCrc.h"
#include "../lzma-sdk/C/7zFile.h"
#include "../lzma-sdk/C/7zVersion.h"


FILE* g_archiveFile = NULL;
static SRes _fsc_Read( void *p, void *buf, size_t *size )
{
	size_t osize = *size;
	*size = fread( buf, 1, osize, g_archiveFile );
	return *size == osize ? SZ_OK : SZ_ERROR_DATA;
}
static SRes _fsc_Seek( void *p, Int64 *pos, ESzSeek origin )
{
	int moveMethod;
	switch( origin )
	{
	case SZ_SEEK_SET: moveMethod = SEEK_SET; break;
	case SZ_SEEK_CUR: moveMethod = SEEK_CUR; break;
	case SZ_SEEK_END: moveMethod = SEEK_END; break;
	default: return 1;
	}
	SRes res = fseek( g_archiveFile, *pos, moveMethod );
	*pos = ftell( g_archiveFile );
	return res;
}
ISeekInStream g_archiveStream = { _fsc_Read, _fsc_Seek };
i32 g_archiveOffset = 0;


static void PrintError(char *sz)
{
	printf("\nERROR: %s\n", sz);
}

int fsc_open_archive( FILE* fh )
{
	if( !fh )
	{
		PrintError( "cannot open input file" );
		return 1;
	}
	g_archiveFile = fh;
	g_archiveOffset = ftell( fh );
	
	return 0;
}

void fsc_close_archive()
{
	fclose( g_archiveFile );
}


static ISzAlloc g_Alloc = { SzAlloc, SzFree };

static int Buf_EnsureSize(CBuf *dest, size_t size)
{
	if (dest->size >= size)
		return 1;
	Buf_Free(dest, &g_Alloc);
	return Buf_Create(dest, size, &g_Alloc);
}

static Byte kUtf8Limits[5] = { 0xC0, 0xE0, 0xF0, 0xF8, 0xFC };

static Bool Utf16_To_Utf8(Byte *dest, size_t *destLen, const UInt16 *src, size_t srcLen)
{
	size_t destPos = 0, srcPos = 0;
	for (;;)
	{
		unsigned numAdds;
		UInt32 value;
		if (srcPos == srcLen)
		{
			*destLen = destPos;
			return True;
		}
		value = src[srcPos++];
		if (value < 0x80)
		{
			if (dest)
				dest[destPos] = (char)value;
			destPos++;
			continue;
		}
		if (value >= 0xD800 && value < 0xE000)
		{
			UInt32 c2;
			if (value >= 0xDC00 || srcPos == srcLen)
				break;
			c2 = src[srcPos++];
			if (c2 < 0xDC00 || c2 >= 0xE000)
				break;
			value = (((value - 0xD800) << 10) | (c2 - 0xDC00)) + 0x10000;
		}
		for (numAdds = 1; numAdds < 5; numAdds++)
			if (value < (((UInt32)1) << (numAdds * 5 + 6)))
				break;
		if (dest)
			dest[destPos] = (char)(kUtf8Limits[numAdds - 1] + (value >> (6 * numAdds)));
		destPos++;
		do
		{
			numAdds--;
			if (dest)
				dest[destPos] = (char)(0x80 + ((value >> (6 * numAdds)) & 0x3F));
			destPos++;
		}
		while (numAdds != 0);
	}
	*destLen = destPos;
	return False;
}

static SRes Utf16_To_Utf8Buf(CBuf *dest, const UInt16 *src, size_t srcLen)
{
	size_t destLen = 0;
	Bool res;
	Utf16_To_Utf8(NULL, &destLen, src, srcLen);
	destLen += 1;
	if (!Buf_EnsureSize(dest, destLen))
		return SZ_ERROR_MEM;
	res = Utf16_To_Utf8(dest->data, &destLen, src, srcLen);
	dest->data[destLen] = 0;
	return res ? SZ_OK : SZ_ERROR_FAIL;
}

#if DEBUG_LEVEL > 0
static SRes Utf16_To_Char(CBuf *buf, const UInt16 *s, int fileMode)
{
	int len = 0;
	for (len = 0; s[len] != '\0'; len++);

	#ifdef _WIN32
	{
		int size = len * 3 + 100;
		if (!Buf_EnsureSize(buf, size))
			return SZ_ERROR_MEM;
		{
			char defaultChar = '_';
			BOOL defUsed;
			int numChars = WideCharToMultiByte(fileMode ?
					(
					#ifdef UNDER_CE
					CP_ACP
					#else
					AreFileApisANSI() ? CP_ACP : CP_OEMCP
					#endif
					) : CP_OEMCP,
					0, s, len, (char *)buf->data, size, &defaultChar, &defUsed);
			if (numChars == 0 || numChars >= size)
				return SZ_ERROR_FAIL;
			buf->data[numChars] = 0;
			return SZ_OK;
		}
	}
	#else
	fileMode = fileMode;
	return Utf16_To_Utf8Buf(buf, s, len);
	#endif
}

static SRes PrintString(const UInt16 *s)
{
	CBuf buf;
	SRes res;
	Buf_Init(&buf);
	res = Utf16_To_Char(&buf, s, 0);
	if (res == SZ_OK)
		fputs((const char *)buf.data, stdout);
	Buf_Free(&buf, &g_Alloc);
	return res;
}
#endif

int fsc_extract_files( int test, fs_progress_callback pcb )
{
	CLookToRead lookStream;
	CSzArEx db;
	SRes res;
	ISzAlloc allocImp;
	ISzAlloc allocTempImp;
	UInt16 *temp = NULL;
	size_t tempSize = 0;
	
	allocImp.Alloc = SzAlloc;
	allocImp.Free = SzFree;

	allocTempImp.Alloc = SzAllocTemp;
	allocTempImp.Free = SzFreeTemp;
	
	fseek( g_archiveFile, g_archiveOffset, SEEK_SET );
	
	LookToRead_CreateVTable(&lookStream, False);
	lookStream.realStream = &g_archiveStream;
	LookToRead_Init(&lookStream);

	CrcGenerateTable();

	SzArEx_Init(&db);
	res = SzArEx_Open(&db, &lookStream.s, &allocImp, &allocTempImp);
	if (res == SZ_OK)
	{
		UInt32 i;

		/*
		if you need cache, use these 3 variables.
		if you use external function, you can make these variable as static.
		*/
		UInt32 blockIndex = 0xFFFFFFFF; /* it can have any value before first call (if outBuffer = 0) */
		Byte *outBuffer = 0; /* it must be 0 before first call for each new archive. */
		size_t outBufferSize = 0;  /* it can have any value before first call (if outBuffer = 0) */

		for (i = 0; i < db.db.NumFiles; i++)
		{
			size_t offset = 0;
			size_t outSizeProcessed = 0;
			const CSzFileItem *f = db.db.Files + i;
			size_t len;
			
			len = SzArEx_GetFileNameUtf16(&db, i, NULL);

			if (len > tempSize)
			{
				SzFree(NULL, temp);
				tempSize = len;
				temp = (UInt16 *)SzAlloc(NULL, tempSize * sizeof(temp[0]));
				if (temp == 0)
				{
					res = SZ_ERROR_MEM;
					break;
				}
			}

			SzArEx_GetFileNameUtf16(&db, i, temp);
			
			if( pcb )
			{
				int pcbres;
				CBuf buf;
				Buf_Init( &buf );
				if( Utf16_To_Utf8Buf( &buf, temp, len ) != SZ_OK )
					pcbres = pcb( i, db.db.NumFiles, "<error>" );
				else
					pcbres = pcb( i, db.db.NumFiles, (const char*) buf.data );
				Buf_Free( &buf, &g_Alloc );
				if( pcbres )
				{
					res = SZ_OK;
					break;
				}
			}
			
			X_DBG(
				fputs(test ?
						"Testing    ":
						"Extracting ",
						stdout);
				res = PrintString(temp);
				if (res != SZ_OK)
					break;
				if (f->IsDir)
					printf("/");
			);
			if (!f->IsDir)
			{
				res = SzArEx_Extract(&db, &lookStream.s, i,
						&blockIndex, &outBuffer, &outBufferSize,
						&offset, &outSizeProcessed,
						&allocImp, &allocTempImp);
				if (res != SZ_OK)
					break;
			}
			if (!test)
			{
				size_t j;
				UInt16 *name = (UInt16 *)temp;
				const UInt16 *destPath = (const UInt16 *)name;
				for (j = 0; name[j] != 0; j++)
					if (name[j] == '/')
					{
						name[j] = 0;
						platfs_createdir_utf16(name);
						name[j] = CHAR_PATH_SEPARATOR;
					}
	
				if (f->IsDir)
				{
					platfs_createdir_utf16(destPath);
					X_DBG( printf("\n") );
					continue;
				}
				else if (platfs_openwritefile_utf16(destPath))
				{
					PrintError("can not open output file");
					res = SZ_ERROR_FAIL;
					break;
				}
				if (platfs_writefile(outBuffer + offset, outSizeProcessed))
				{
					PrintError("can not write output file");
					res = SZ_ERROR_FAIL;
					break;
				}
				if (platfs_closewritefile())
				{
					PrintError("can not close output file");
					res = SZ_ERROR_FAIL;
					break;
				}
				#ifdef USE_WINDOWS_FILE
				if (f->AttribDefined)
					SetFileAttributesW(destPath, f->Attrib);
				#endif
			}
			X_DBG( printf("\n") );
		}
		IAlloc_Free(&allocImp, outBuffer);
	}
	SzArEx_Free(&db, &allocImp);
	SzFree(NULL, temp);

	if (res == SZ_OK)
	{
		X_DBG( printf("\nEverything is Ok\n") );
		return 0;
	}
	if (res == SZ_ERROR_UNSUPPORTED)
		PrintError("decoder doesn't support this archive");
	else if (res == SZ_ERROR_MEM)
		PrintError("can not allocate memory");
	else if (res == SZ_ERROR_CRC)
		PrintError("CRC error");
	else
		printf("\nERROR #%d\n", res);
	return 1;
}

