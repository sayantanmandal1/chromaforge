@echo off
cd /d "C:\Users\H661893\Downloads\perf\chromaforge"
call "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if not exist build mkdir build

echo === Building tests ===
cl /nologo /std:c++17 /O2 /EHsc /MD /I include src\color.cpp src\filters.cpp src\image.cpp src\lut.cpp tests\tests.cpp /Fe:build\cf_tests.exe /Fo:build\ 2>&1 | findstr /V "^Generating ^color.cpp ^filters.cpp ^image.cpp ^lut.cpp ^tests.cpp ^bench.cpp ^cli.cpp"

echo.
echo === Building bench ===
cl /nologo /std:c++17 /O2 /EHsc /MD /I include src\color.cpp src\filters.cpp src\image.cpp src\lut.cpp bench\bench.cpp /Fe:build\cf_bench.exe /Fo:build\ 2>&1 | findstr /V "^Generating ^color.cpp ^filters.cpp ^image.cpp ^lut.cpp ^tests.cpp ^bench.cpp ^cli.cpp"

echo.
echo === Building cli ===
cl /nologo /std:c++17 /O2 /EHsc /MD /I include src\color.cpp src\filters.cpp src\image.cpp src\lut.cpp apps\cli.cpp /Fe:build\cf.exe /Fo:build\ 2>&1 | findstr /V "^Generating ^color.cpp ^filters.cpp ^image.cpp ^lut.cpp ^tests.cpp ^bench.cpp ^cli.cpp"

echo.
echo === Running tests ===
build\cf_tests.exe
echo tests exit code: %ERRORLEVEL%
