@echo off
call D:\MSDEV\BIN\VCVARS32.BAT >nul 2>&1
cd /d D:\TEMP\vmb-axp
if exist vmmsetup.exe del vmmsetup.exe
echo --- vmmouse.sys present? ---
dir vmmouse.sys
echo --- stage resources ---
copy T:\NTOS\vmmouse\SRC\vmmouse.ico vmmouse.ico >nul
copy T:\NTOS\vmmouse\SRC\vmmouse.h vmmouse.h >nul
copy T:\NTOS\vmmouse\SRC\vmmsetup.rc vmmsetup.rc >nul
copy T:\NTOS\vmmouse\SRC\vmmsetup.h vmmsetup.h >nul
copy T:\NTOS\vmmouse\SRC\vmmsetup.c vmmsetup.c >nul
echo === cl vmmsetup.c ===
cl -nologo -c -W3 -Ox -DWIN32 -D_ALPHA_=1 vmmsetup.c
echo === rc vmmsetup.rc ===
rc -r -fo vmmsetup.res vmmsetup.rc
echo === link vmmsetup.exe ===
link -nologo -machine:alpha -subsystem:windows -out:vmmsetup.exe vmmsetup.obj vmmsetup.res user32.lib advapi32.lib
echo === dump headers ===
link -dump -headers vmmsetup.exe
if exist vmmsetup.exe (echo EXE_BUILT) else (echo EXE_MISSING)
dir vmmsetup.exe
echo === DONE ===
