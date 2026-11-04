@echo off
rem vmmouse.sys for PowerPC NT 4.0 with Visual C++ 4.1 for PowerPC.
rem Uses a local MSDEV if there is one, else the copy on the share.
set VC=T:\NTOS\_POWERPC\msvc\setup\MSDEV
if exist C:\MSDEV\BIN\CL.EXE set VC=C:\MSDEV
if exist D:\MSDEV\BIN\CL.EXE set VC=D:\MSDEV
set PATH=%VC%\BIN;%PATH%
set INCLUDE=%VC%\INCLUDE
set LIB=%VC%\LIB
if not exist %SystemDrive%\TEMP md %SystemDrive%\TEMP
if not exist %SystemDrive%\TEMP\vmb-ppc md %SystemDrive%\TEMP\vmb-ppc
cd /d %SystemDrive%\TEMP\vmb-ppc
if exist vmmouse.sys del vmmouse.sys
echo === compiler: %VC% ===
echo === cl vmmouse.c ===
cl -nologo -c -W3 -Ox -D_PPC_=1 -DDBG=0 -IT:\NTOS\vmmouse\SRC T:\NTOS\vmmouse\SRC\vmmouse.c
echo === cl ppc-reg.c ===
cl -nologo -c -W3 -Ox -D_PPC_=1 -DDBG=0 -IT:\NTOS\vmmouse\SRC T:\NTOS\vmmouse\ppcbuild\ppc-reg.c
echo === lib ntoskrnl ===
lib -nologo -machine:ppc -def:T:\NTOS\vmmouse\ppcbuild\ntoskrnl.def -out:ntoskrnl.lib
echo === link vmmouse.sys ===
link -nologo -out:vmmouse.sys -machine:ppc -subsystem:native -align:0x200 -base:0x10000 -entry:DriverEntry -debug:none vmmouse.obj ppc-reg.obj ntoskrnl.lib
echo === dump headers ===
link -dump -headers vmmouse.sys
echo === dump imports ===
link -dump -imports vmmouse.sys
if exist vmmouse.sys (echo SYS_BUILT) else (echo SYS_MISSING)
dir vmmouse.sys
echo === BUILD DONE ===
