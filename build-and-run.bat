@echo off
setlocal

set CC=gcc
set SRC=src\main.c
set OUT=ted.exe

set CFLAGS=-Wall -Wextra -std=c99 -Ivendor\SDL3\include -O2
set LDFLAGS=-Lvendor\SDL3\lib\windows -lSDL3 -lkernel32 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lversion -luuid -ladvapi32 -lsetupapi -lshell32 -ldinput8 -ldxguid -lm -static -mwindows

echo Building ted...
%CC% %SRC% %CFLAGS% %LDFLAGS% -o %OUT%
if %ERRORLEVEL% neq 0 (
    echo Build failed.
    exit /b 1
)

echo Running ted...
%OUT%
