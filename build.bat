@echo off
rem Builds the Prose speech library for Windows with MinGW-w64 gcc (32- or 64-bit):
rem
rem   build.bat          build\prose.dll, build\prose_say.exe and the samples in build\
rem   build.bat clean
rem
rem The tests and replay tools are built with CMake instead (src\CMakeLists.txt, REFERENCE section 14).
setlocal enabledelayedexpansion
cd /d "%~dp0"
set OUT=build

if /i "%~1"=="clean" (
	if exist %OUT% rmdir /s /q %OUT%
	exit /b 0
)

where gcc >nul 2>nul
if errorlevel 1 (
	echo gcc was not found. Install MinGW-w64 and put its bin directory on PATH.
	exit /b 1
)

set DIRS=common data dsp frame input lexical paramgen prosody textrules v1 dll
set INC=-Isrc\include
for %%d in (%DIRS%) do set INC=!INC! -Isrc\%%d
set CFLAGS=-std=c99 -O2 -Wall -Wextra
if not exist %OUT%\obj mkdir %OUT%\obj

echo Compiling the library...
set OBJS=
for %%d in (%DIRS%) do (
	for %%f in (src\%%d\*.c) do (
		gcc %CFLAGS% -DPROSE_BUILD_DLL %INC% -c %%f -o %OUT%\obj\%%d_%%~nf.o
		if errorlevel 1 goto fail
		set OBJS=!OBJS! %OUT%\obj\%%d_%%~nf.o
	)
)

echo Linking prose.dll...
gcc -shared -static-libgcc -o %OUT%\prose.dll !OBJS! -lwinmm -Wl,--out-implib,%OUT%\libprose.dll.a
if errorlevel 1 goto fail

echo Building prose_say.exe and the samples...
gcc %CFLAGS% -Isrc\include src\cli\prose_say.c -o %OUT%\prose_say.exe -L%OUT% -lprose -static-libgcc
if errorlevel 1 goto fail
for %%s in (samples\*.c) do (
	gcc %CFLAGS% -Isrc\include %%s -o %OUT%\%%~ns.exe -L%OUT% -lprose -static-libgcc
	if errorlevel 1 goto fail
)

echo Done: %OUT%\prose.dll, %OUT%\prose_say.exe and the samples.
exit /b 0

:fail
echo Build failed.
exit /b 1
