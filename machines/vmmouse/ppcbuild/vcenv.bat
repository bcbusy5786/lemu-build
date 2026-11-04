@echo off
set VC=T:\NTOS\_POWERPC\msvc\setup\MSDEV
if exist C:\MSDEV\BIN\CL.EXE set VC=C:\MSDEV
if exist D:\MSDEV\BIN\CL.EXE set VC=D:\MSDEV
set PATH=%VC%\BIN;%PATH%
set INCLUDE=%VC%\INCLUDE
set LIB=%VC%\LIB
