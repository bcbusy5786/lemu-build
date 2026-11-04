@echo off
call D:\MSDEV\BIN\VCVARS32.BAT >nul 2>&1
if not exist D:\TEMP\vmb md D:\TEMP\vmb
cd /d D:\TEMP\vmb
if exist vmmouse.sys del vmmouse.sys
echo === cl vmmouse.c ===
cl -nologo -c -W3 -Ox -D_MIPS_=1 -DDBG=0 -IT:\NTOS\vmmouse\SRC T:\NTOS\vmmouse\SRC\vmmouse.c
echo === cl mips-reg.c ===
cl -nologo -c -W3 -Ox -D_MIPS_=1 -DDBG=0 -IT:\NTOS\vmmouse\SRC T:\NTOS\vmmouse\mipsbuild\mips-reg.c
echo === lib ntoskrnl ===
lib -nologo -machine:mips -def:T:\NTOS\vmmouse\mipsbuild\ntoskrnl.def -out:ntoskrnl.lib
echo === link vmmouse.sys ===
link -nologo -out:vmmouse.sys -machine:mips -subsystem:native -align:0x200 -base:0x10000 -entry:DriverEntry -debug:none vmmouse.obj mips-reg.obj ntoskrnl.lib
echo === dump headers ===
link -dump -headers vmmouse.sys
if exist vmmouse.sys (echo SYS_BUILT) else (echo SYS_MISSING)
dir vmmouse.sys
echo === BUILD DONE ===
