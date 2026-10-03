@echo off
setlocal
where g++ >nul 2>&1 || (echo [ERROR] g++ not found in PATH. Install MinGW-w64 / w64devkit and add its bin folder to PATH. & exit /b 1)
where windres >nul 2>&1 || (echo [ERROR] windres not found in PATH. & exit /b 1)
if not exist build mkdir build

echo [1/2] Compiling resources...
windres boost11.rc -O coff -o build\boost11_res.o || exit /b 1

echo [2/2] Compiling...
g++ -std=c++17 -Os -s -Wall -mwindows -static -static-libgcc -static-libstdc++ ^
    -Isrc src\main.cpp build\boost11_res.o -o Boost11.exe ^
    -lgdiplus -lgdi32 -luser32 -lshell32 -lshlwapi -lole32 -luuid ^
    -lpowrprof -ldwmapi -ladvapi32 -lshcore || exit /b 1

echo Done: Boost11.exe