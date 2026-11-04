@echo off
call D:\MSDEV\BIN\VCVARS32.BAT >nul 2>&1
if not exist D:\TEMP\vmb-axp md D:\TEMP\vmb-axp
cd /d D:\TEMP\vmb-axp
if exist vmmouse.sys del vmmouse.sys
echo === cl vmmouse.c ===
cl -nologo -c -W3 -Ox -D_ALPHA_=1 -DDBG=0 -IT:\NTOS\vmmouse\SRC T:\NTOS\vmmouse\SRC\vmmouse.c
echo === lib ntoskrnl ===
lib -nologo -machine:alpha -def:T:\NTOS\vmmouse\alphabuild\ntoskrnl.def -out:ntoskrnl.lib
echo === lib hal ===
lib -nologo -machine:alpha -def:T:\NTOS\vmmouse\alphabuild\hal.def -out:hal.lib
echo === link vmmouse.sys ===
link -nologo -out:vmmouse.sys -machine:alpha -subsystem:native -align:0x200 -base:0x10000 -entry:DriverEntry -debug:none vmmouse.obj ntoskrnl.lib hal.lib
echo === dump headers ===
link -dump -headers vmmouse.sys
echo === dump imports ===
link -dump -imports vmmouse.sys
if exist vmmouse.sys (echo SYS_BUILT) else (echo SYS_MISSING)
dir vmmouse.sys
echo === BUILD DONE ===
