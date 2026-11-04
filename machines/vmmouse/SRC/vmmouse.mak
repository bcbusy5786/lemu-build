# vmmouse.mak - build vmmouse.sys and vmmsetup.exe with the Win32 SDK alone.
#
#   nmake -f vmmouse.mak              build both  (needs the NT 3.1 SDK)
#   nmake -f vmmouse.mak clean        remove the build products
#
# No DDK is needed. ntmini.h is our own small subset of the DDK headers, and
# the kernel import libraries (ntoskrnl.lib, hal.lib) are made from ntstubs.c
# plus the .def files, so lib32 learns the x86-decorated names.
#
# This builds the i386 driver and installer. For MIPS, Alpha or PowerPC,
# build on that machine with its own SDK; the sources are the same.

!include <ntwin32.mak>

all: vmmouse.sys vmmsetup.exe

# ---- the kernel-mode driver ----------------------------------------

DRVCF = -nologo -c -Zel -Zp8 -W3 -Gz -H63 -G4 -Ox -Oy- -D_X86_=1 -Di386=1 -DSTD_CALL -DDBG=0
DRVLF = -machine:i386 -subsystem:native -align:0x200 -base:0x10000 \
        -entry:DriverEntry@8 -debug:none

ntstubs.obj: ntstubs.c ntmini.h
    cl386 $(DRVCF) ntstubs.c

ntoskrnl.lib: ntoskrnl.def ntstubs.obj
    lib32 -machine:i386 -def:ntoskrnl.def ntstubs.obj -out:ntoskrnl.lib

hal.lib: hal.def ntstubs.obj
    lib32 -machine:i386 -def:hal.def ntstubs.obj -out:hal.lib

vmmouse.obj: vmmouse.c ntmini.h vmmouse.h
    cl386 $(DRVCF) vmmouse.c

vmmouse.sys: vmmouse.obj ntoskrnl.lib hal.lib
    link32 -out:vmmouse.sys $(DRVLF) vmmouse.obj ntoskrnl.lib hal.lib

# ---- the user-mode installer (GUI) ---------------------------------

vmmsetup.obj: vmmsetup.c vmmsetup.h vmmouse.h
    $(cc) $(cflags) $(cvars) vmmsetup.c

# the .rc embeds vmmouse.sys and vmmouse.ico, so the driver must exist first
vmmsetup.res: vmmsetup.rc vmmsetup.h vmmouse.ico vmmouse.sys
    $(rc) -r -fo vmmsetup.res vmmsetup.rc

vmmsetup.rbj: vmmsetup.res
    $(cvtres) -$(CPU) vmmsetup.res -o vmmsetup.rbj

vmmsetup.exe: vmmsetup.obj vmmsetup.rbj
    $(link) $(guiflags) -out:vmmsetup.exe vmmsetup.obj vmmsetup.rbj \
        $(guilibs) advapi32.lib

clean:
    -del *.obj *.res *.rbj *.exp 2>nul
    -del vmmouse.sys vmmsetup.exe ntoskrnl.lib hal.lib 2>nul
