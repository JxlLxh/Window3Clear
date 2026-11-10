@echo off
echo Building Window3Clear v19.1-beta (All Architectures)...
g++ gen_ico.cpp -o gen_ico.exe
gen_ico.exe

echo 1 ICON "icon.ico" > app.rc

echo.
echo [1/2] Compiling x86_64...
windres app.rc -O coff -o app64.res
g++ Window3Clear_x86_64.cpp app64.res -o Window3Clear_x86_64.exe -O2 -municode -mwindows -std=c++17 -lgdi32 -luser32 -lshell32 -lcomctl32 -ldwmapi -lshlwapi -static-libgcc -static-libstdc++
if errorlevel 1 goto fail

echo.
echo [2/2] Compiling x86 (32-bit)...
windres -F pe-i386 app.rc -O coff -o app32.res
i686-w64-mingw32-g++ Window3Clear_x86.cpp app32.res -o Window3Clear_x86.exe -O2 -municode -mwindows -std=c++17 -lgdi32 -luser32 -lshell32 -lcomctl32 -ldwmapi -lshlwapi -static-libgcc -static-libstdc++
if errorlevel 1 goto fail

echo.
echo All Builds Successful!
pause
exit

:fail
echo Build Failed!
pause
