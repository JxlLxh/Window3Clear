@echo off
echo Building Window3Clear V19 Alpha...
g++ gen_ico.cpp -o gen_ico.exe
gen_ico.exe
echo 1 ICON "icon.ico" > app.rc
windres app.rc -O coff -o app.res

g++ Window3Clear.cpp app.res -o Window3Clear.exe -std=c++14 -O2 -mwindows -lgdi32 -lshell32 -lshlwapi -lcomctl32 -static
if errorlevel 1 goto fail
echo Build Successful!
pause
exit
:fail
echo Build Failed!
pause
