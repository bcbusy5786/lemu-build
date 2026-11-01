@echo off
cd /d "%~dp0"
bin\windows-x86_64\qemu-system-ppc64.exe ^
 -name "AIX 7.2 POWER PSERIES" ^
 -M pseries,ic-mode=xics -cpu POWER8 -m 8G ^
 -serial mon:stdio ^
 -nographic ^
 -vga none ^
 -prom-env input-device=/vdevice/vty@71000000 ^
 -prom-env output-device=/vdevice/vty@71000000 ^
 -boot order=c ^
 -device virtio-scsi-pci,id=scsi ^
 -device scsi-hd,drive=hd0 ^
 -drive file=aix72.qcow2,if=none,id=hd0,format=qcow2 ^
 -drive if=scsi,media=cdrom,id=cd0 ^
 -nic user,model=spapr-vlan,hostfwd=tcp:127.0.0.1:7223-:23,hostfwd=tcp:127.0.0.1:7222-:22 ^
 %*
