@echo off
echo Building Window3Clear v19.0-beta...
g++ gen_ico.cpp -o gen_ico.exe
gen_ico.exe
echo 1 ICON "icon.ico" > app.rc
windres app.rc -O coff -o app.res

g++ Window3Clear_x86_64.cpp app.res -o Window3Clear_x86_64.exe -std=c++14 -O2 -mwindows -lgdi32 -lshell32 -lshlwapi -lcomctl32 -static
if errorlevel 1 goto fail
echo Build Successful!
pause
exit
:fail
echo Build Failed!
pause
