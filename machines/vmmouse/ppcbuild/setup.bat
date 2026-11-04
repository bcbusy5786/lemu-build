@echo off
rem vmmsetup.exe (the installer, vmmouse.sys embedded) for PowerPC NT 4.0.
set VC=T:\NTOS\_POWERPC\msvc\setup\MSDEV
if exist C:\MSDEV\BIN\CL.EXE set VC=C:\MSDEV
if exist D:\MSDEV\BIN\CL.EXE set VC=D:\MSDEV
set PATH=%VC%\BIN;%PATH%
set INCLUDE=%VC%\INCLUDE
set LIB=%VC%\LIB
cd /d %SystemDrive%\TEMP\vmb-ppc
if exist vmmsetup.exe del vmmsetup.exe
echo --- stage resources ---
copy T:\NTOS\vmmouse\SRC\vmmouse.ico vmmouse.ico >nul
copy T:\NTOS\vmmouse\SRC\vmmouse.h vmmouse.h >nul
copy T:\NTOS\vmmouse\SRC\vmmsetup.rc vmmsetup.rc >nul
copy T:\NTOS\vmmouse\SRC\vmmsetup.h vmmsetup.h >nul
copy T:\NTOS\vmmouse\SRC\vmmsetup.c vmmsetup.c >nul
echo === cl vmmsetup.c ===
cl -nologo -c -W3 -Ox -DWIN32 -D_PPC_=1 vmmsetup.c
echo === rc vmmsetup.rc ===
rc -r -fo vmmsetup.res vmmsetup.rc
echo === link vmmsetup.exe ===
link -nologo -subsystem:windows -machine:ppc -out:vmmsetup.exe vmmsetup.obj vmmsetup.res user32.lib advapi32.lib
echo === dump headers ===
link -dump -headers vmmsetup.exe
if exist vmmsetup.exe (echo EXE_BUILT) else (echo EXE_MISSING)
dir vmmsetup.exe
echo === DONE ===
