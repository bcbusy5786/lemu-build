@echo off
cd /d "%~dp0"
rem SLOF runs nvramrc at every boot; it evaluates the device-tree fixes for AIX
rem in aixrc1 and the PCI graphics and USB fixes in aixrc2 (see run.sh).
bin\windows-x86_64\qemu-system-ppc64.exe ^
 -name "AIX 5.3 POWER PSERIES" ^
 -M pseries,ic-mode=xics,usb=off,max-cpu-compat=power6,cap-hpt-max-page-size=16M ^
 -cpu power7_v2.3 -smp 1 -m 2G ^
 -serial mon:stdio ^
 -vga none ^
 -prom-env input-device=/vdevice/vty@71000000 ^
 -prom-env output-device=/vdevice/vty@71000000 ^
 -prom-env use-nvramrc?=true ^
 -prom-env "aixrc1=dev /vdevice 1100 encode-int 100 encode-int encode+ s\" interrupt-ranges\" property device-end dev / new-device s\" xicp\" device-name s\" PowerPC-External-Interrupt-Presentation\" encode-string s\" device_type\" property s\" IBM,ppc-xicp\" encode-string s\" compatible\" property 3ffe encode-int 0 encode-int encode+ 0 encode-int encode+ 1000 encode-int encode+ s\" reg\" property 0 encode-int 1 encode-int encode+ s\" ibm,interrupt-server-ranges\" property finish-device device-end dev / 1 encode-int s\" ibm,partition-no\" property s\" aix53\" encode-string s\" ibm,partition-name\" property s\" IBM,0210A53A1\" encode-string s\" system-id\" property device-end dev /vdevice/v-scsi@71000003/disk@8100000000000000 : load drop 0 ; device-end dev /vdevice/v-scsi@71000004/disk@8100000000000000 : load drop 0 ; device-end" ^
 -prom-env "aixrc2=device-end : aixdma 80000000 encode-int 0 encode-int encode+ 0 encode-int encode+ 0 encode-int encode+ 0 encode-int encode+ 40000000 encode-int encode+ s\" ibm,dma-window\" s\" /pci@800000020000000\" find-node set-property 3 encode-int s\" ibm,#dma-address-cells\" s\" /pci@800000020000000\" find-node set-property ; dev / : ibm,client-architecture-support ibm,client-architecture-support aixdma ; device-end aixdma dev /pci@800000020000000/vga@0 0 0 s\" ibm,init1\" property 0 0 s\" ibm,init2\" property 0 0 s\" ibm,pllinit\" property 0 0 s\" ibm,meminit\" property device-end dev /pci@800000020000000/usb-ohci@1 80000000 encode-int 0 encode-int encode+ 0 encode-int encode+ 0 encode-int encode+ 40000000 encode-int encode+ s\" ibm,dma-window\" property device-end" ^
 -prom-env "nvramrc=aixrc1 evaluate aixrc2 evaluate" ^
 -device spapr-vscsi,id=vscsi0 ^
 -drive if=none,id=cd0,media=cdrom,readonly=on ^
 -device scsi-cd,bus=vscsi0.0,drive=cd0,scsi-id=1,vendor=AIX,product=VOPTA ^
 -device spapr-vscsi,id=vscsi1 ^
 -drive file=aix53.qcow2,if=none,id=hd0,format=qcow2 ^
 -device scsi-hd,bus=vscsi1.0,drive=hd0,scsi-id=1,vendor=AIX,product=VDASD ^
 -boot order=c ^
 -device ati-vga,model=es1000,romfile=,addr=0 ^
 -device nec-usb-ohci,id=usb0,addr=1 ^
 -device usb-kbd,bus=usb0.0 ^
 -device usb-mouse,bus=usb0.0 ^
 -nic user,model=spapr-vlan,hostfwd=tcp:127.0.0.1:5323-:23,hostfwd=tcp:127.0.0.1:53514-:514 ^
 %*
