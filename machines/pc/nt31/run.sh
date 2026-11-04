#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Windows NT 3.1 on a PC: Pentium, 1 GB RAM, ISA S3 86C928 card with a Bt485 DAC
# (1280x1024, 256 colours), ISA NE2000, IDE disk.
# There is no kit for this machine, so this script is an example. It runs
# qemu-system-i386 from PATH, which must be LEMU's: upstream QEMU has no s3-928.
# Put these files next to it:
# - nt31.qcow2: NT 3.1 on the IDE disk. NT 3.1 can't use QEMU's IDE CD-ROM or
#   SCSI cards, so install it from \I386 copied to a second IDE disk.
# - bios-nt31.bin: LEMU's pc-bios/bios-256k.bin with one value changed. Without
#   it NTLDR sees only 64 MB. More than 1 GB hangs NTLDR.
#     cp pc-bios/bios-256k.bin bios-nt31.bin
#     printf '\074\0\0' | dd of=bios-nt31.bin bs=1 seek=245559 conv=notrunc
# - 928-n9.bin: the card's VGA BIOS; NT's S3 driver sets every mode through it.
#   86Box's Metheus BIOS (roms/video/s3/928.VBI) with the "Number Nine Computer"
#   name that NT's S3.SYS looks for, and the checksum fixed:
#     cp 928.VBI 928-n9.bin
#     printf 'Number Nine Computer' | dd of=928-n9.bin bs=1 seek=32738 conv=notrunc
#     printf '\246' | dd of=928-n9.bin bs=1 seek=32767 conv=notrunc
# Keep the BIOS disk geometry at 127 heads or fewer: NT 3.1's ATDISK.SYS reads the
# head count as a signed byte. The boot partition must end within the first 2016 MB.
# NT 3.1 has no DHCP client: give it 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3.
# The terminal holds the QEMU monitor: sendkey ctrl-alt-delete logs on,
# change fd0 FILE.img inserts a floppy.
cd "$(dirname "$0")" || exit 1
exec qemu-system-i386 \
	-name "Windows NT 3.1 X86 PC" \
	-M pc -cpu pentium -m 1024 \
	-bios bios-nt31.bin \
	-rtc base=localtime \
	-vga none -device s3-928,vram-size-mb=4,romfile=928-n9.bin \
	-drive file=nt31.qcow2,if=none,id=hd0,format=qcow2 \
	-device ide-hd,drive=hd0,bus=ide.0,unit=0,cyls=8128,heads=16,secs=63,lcyls=1024,lheads=127,lsecs=63 \
	-drive if=floppy,index=0,id=fd0 \
	-netdev user,id=n0 -device ne2k_isa,iobase=0x300,irq=5,netdev=n0 \
	-monitor stdio \
	"$@"
