/*
 * ppc-reg.c - READ/WRITE_REGISTER_ULONG for the PowerPC build of vmmouse.sys.
 *
 * On PowerPC these are macros in the DDK, not exports of ntoskrnl.exe or
 * hal.dll: a plain volatile access (writes add KeFlushWriteBuffer, which QEMU
 * does not need - it does every MMIO access in program order). MmMapIoSpace
 * returns an uncached pointer, so this is the whole implementation, the same
 * as mips-reg.c.
 */
#include "ntmini.h"

ULONG READ_REGISTER_ULONG(IN PULONG Register)
{
    return *(volatile ULONG *)Register;
}

VOID WRITE_REGISTER_ULONG(IN PULONG Register, IN ULONG Value)
{
    *(volatile ULONG *)Register = Value;
}
