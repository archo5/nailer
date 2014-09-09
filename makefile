
include core.mk

COREFLAGS = -g -m32
CFLAGS = $(COREFLAGS) -Wall
LIBFLAGS = -Lobj\
	$(call fnIF_OS,window,-lgdi32 -lmsimg32)

_DEPS=config window
_TST_OBJ=runner config window_win32 filesys_common filesys_test
_RUN_OBJ=runner config window_win32 filesys_common filesys_exe
_BLD_OBJ=builder config regex libjpg1 libjpg2 libjpg3 zlib1 zlib2 zlib3 libpng1
DEPS=$(patsubst %,src/%.h,$(_DEPS))
TST_OBJ=$(patsubst %,obj/%.o,$(_TST_OBJ))
RUN_OBJ=$(patsubst %,obj/%.o,$(_RUN_OBJ))
BLD_OBJ=$(patsubst %,obj/%.o,$(_BLD_OBJ))

_LZMA_OBJ=7zAlloc 7zBuf 7zBuf2 7zCrc 7zCrcOpt 7zDec 7zFile 7zIn 7zStream Alloc \
	Bcj2 Bra Bra86 BraIA64 CpuArch Delta LzFind LzFindMt Lzma2Dec Lzma2Enc \
	Lzma86Dec Lzma86Enc LzmaDec LzmaEnc LzmaLib MtCoder Ppmd7 Ppmd7Dec \
	Ppmd7Enc Sha256 Threads Xz XzCrc64 XzDec XzEnc XzIn
LZMA_OBJ=$(patsubst %,obj/%.o,$(_LZMA_OBJ))

_LZMA_CPP_OBJ=7zCompressionMode 7zDecode 7zEncode 7zExtract 7zFolderInStream 7zFolderOutStream \
	7zHandler 7zHandlerOut 7zHeader 7zIn 7zOut 7zProperties 7zRegister 7zSpecStream 7zUpdate \
	CoderMixer2 CoderMixer2MT CrossThreadProgress DummyOutStream HandlerOut \
	InStreamWithCRC ItemNameUtils MultiStream OutStreamWithCRC ParseProperties \
	Bcj2Coder Bcj2Register BcjCoder BcjRegister BranchCoder BranchMisc BranchRegister ByteSwap \
	CodecExports CopyCoder CopyRegister DeltaFilter Lzma2Decoder Lzma2Encoder \
	Lzma2Register LzmaDecoder LzmaEncoder LzmaRegister \
	BenchCon ConsoleClose ExtractCallbackConsole List OpenCallbackConsole PercentPrinter StdAfx UpdateCallbackConsole UserInputUtils \
	CommandLineParser C_FileIO IntToString ListFileUtils MyString MyVector NewHandler \
	StdInStream StdOutStream StringConvert StringToInt UTFConvert Wildcard \
	ArchiveCommandLine ArchiveExtractCallback ArchiveName ArchiveOpenCallback Bench DefaultName \
	EnumDirItems Extract ExtractingFilePath LoadCodecs OpenArchive PropIDUtils SetProperties \
	SortUtils TempFiles Update UpdateAction UpdateCallback UpdatePair UpdateProduce WorkDir \
	DLL Error FileDir FileFind FileIO FileMapping FileName MemoryLock \
	PropVariant PropVariantConversions Registry Synchronization System Time \
	CreateCoder CWrappers FilePathAutoRename FileStreams FilterCoder InBuffer \
	InOutTempBuffer LimitedStreams LockedStream MethodId MethodProps OffsetStream \
	OutBuffer ProgressUtils StreamBinder StreamObjects StreamUtils VirtThread \
	ArchiveExports LzmaHandler SplitHandler XzHandler \
	EmbeddedEncoder
LZMA_CPP_OBJ=$(patsubst %,obj/%.cxx.o,$(_LZMA_CPP_OBJ))

LZCXXFLAGS=$(COREFLAGS) -DEXTERNAL_CODECS -Ilzma-sdk/CPP -Ilzma-sdk/CPP/7zip/UI/Console -Wno-conversion-null

# CORE
.PHONY: all test runner builder clean

all: test runner builder
test: bin/nlrtest$(BINEXT)
runner: bin/nailer-runner$(BINEXT)
builder: bin/nailer-builder$(BINEXT)

bin/nlrtest$(BINEXT): $(TST_OBJ) obj/liblzma.a
	$(CC) -o $@ $(TST_OBJ) $(CFLAGS) $(LIBFLAGS) -llzma
bin/nailer-runner$(BINEXT): $(RUN_OBJ) obj/liblzma.a
	$(CC) -o $@ $(RUN_OBJ) $(CFLAGS) $(LIBFLAGS) -llzma $(call fnIF_OS,windows,-mwindows)
bin/nailer-builder$(BINEXT): $(BLD_OBJ) obj/liblzmacpp.a
	$(CXX) -o $@ $(BLD_OBJ) $(CFLAGS) $(LIBFLAGS) -llzmacpp -llzma -DPNG_NO_STDIO -static-libgcc -static-libstdc++ \
	$(call fnIF_OS,windows,-loleaut32 -luuid)
obj/liblzma.a: $(LZMA_OBJ)
	ar rcs $@ $(LZMA_OBJ)
obj/liblzmacpp.a: $(LZMA_CPP_OBJ)
	ar rcs $@ $(LZMA_CPP_OBJ)

obj/%.o: src/%.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<
obj/%.o: lzma-sdk/C/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
obj/%.o: image-sdks/%.c
	$(CC) $(CFLAGS) -c -o $@ $< -Iimage-sdks/zlib

# LZMA-CPP
obj/%.cxx.o: lzma-sdk/CPP/7zip/Archive/7z/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<
obj/%.cxx.o: lzma-sdk/CPP/7zip/Archive/Common/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<
obj/%.cxx.o: lzma-sdk/CPP/7zip/Archive/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<
obj/%.cxx.o: lzma-sdk/CPP/7zip/Common/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<
obj/%.cxx.o: lzma-sdk/CPP/7zip/Compress/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<
obj/%.cxx.o: lzma-sdk/CPP/7zip/UI/Common/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<
obj/%.cxx.o: lzma-sdk/CPP/7zip/UI/Console/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<
obj/%.cxx.o: lzma-sdk/CPP/7zip/Common/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<
obj/%.cxx.o: lzma-sdk/CPP/Common/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<
obj/%.cxx.o: lzma-sdk/CPP/Windows/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<
obj/%.cxx.o: lzma-sdk/CPP/%.cpp
	$(CXX) $(LZCXXFLAGS) -c -o $@ $<

clean:
	$(fnREMOVE_ALL) $(call fnFIX_PATH,obj/*.o bin/nailer-* bin/nlrtest*)
