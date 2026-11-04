@echo off
start /wait %SystemDrive%\TEMP\vmb-ppc\vmmsetup.exe /install /q
echo install errorlevel=%errorlevel%
dir %SystemRoot%\system32\drivers\vmmouse*
