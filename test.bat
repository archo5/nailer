
@ECHO.
@ECHO.
@ECHO ===================================
@ECHO NAILER BUILDER TEST 1, no arguments
@ECHO ===================================
bin\nailer-builder

@ECHO.
@ECHO.
@ECHO ===================================
@ECHO NAILER BUILDER TEST 2, no script
@ECHO ===================================
bin\nailer-builder -o test-out.exe -s script-that-doesnt-exist

@ECHO.
@ECHO.
@ECHO ===================================
@ECHO NAILER BUILDER TEST 3, no image dir
@ECHO ===================================
bin\nailer-builder -o test-out.exe -s testdata/script-src0.txt

@ECHO.
@ECHO.
@ECHO ===================================
@ECHO NAILER BUILDER TEST 3.1, one image
@ECHO ===================================
bin\nailer-builder -o test-out.exe -s testdata/script-src1.txt -i testdata

@ECHO.
@ECHO.
@ECHO ===================================
@ECHO NAILER BUILDER TEST 3.2, 2 good images, 1 bad
@ECHO ===================================
bin\nailer-builder -o test-out.exe -s testdata/script-src2.txt -i testdata

@ECHO.
@ECHO.
@ECHO ===================================
@ECHO NAILER BUILDER TEST 3.3, 2 good images, full dump
@ECHO ===================================
bin\nailer-builder -o test-out.exe -s testdata/script-src3.txt -i testdata -d

@ECHO.
@ECHO.
@ECHO ===================================
@ECHO NAILER BUILDER TEST 4, added files
@ECHO ===================================
bin\nailer-builder -o test-out.exe -s testdata/script-src.txt -i testdata -f testdata
