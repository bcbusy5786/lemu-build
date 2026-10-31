@echo off
cd /d "%~dp0"
qemu\windows-x64\qemu-system-mips64.exe ^
 -name "SINIX-N 5.45 MIPS RM200" ^
 -M sni-rm200,nvram=nvram-5.45.bin ^
 -m 64 ^
 -bios "RM200_SIEMENS_M29F040B@PLCC32.BIN" ^
 -monitor stdio ^
 -nic user,model=pcnet,net=10.10.16.0/24,host=10.10.16.2,dns=10.10.16.3,hostfwd=tcp::2323-:23,hostfwd=tcp::2121-:21 ^
 -drive file=sinix-5.45-4g.hda,if=none,id=hd0,format=raw ^
 -device scsi-hd,drive=hd0,scsi-id=0,cyls=3832,heads=20,secs=110,vendor=QUANTUM,product=XP34300 ^
 %*
