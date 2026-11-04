@echo off
start /wait D:\TEMP\vmb-axp\vmmsetup.exe /install /q
echo install errorlevel=%errorlevel%
dir D:\WINNT\system32\drivers\vmmouse*
