/*
 * mips-reg.c - READ/WRITE_REGISTER_ULONG for the MIPS build of vmmouse.sys.
 *
 * On MIPS these are not exported by ntoskrnl.exe or hal.dll (unlike Alpha,
 * where the HAL provides them for sparse I/O). MmMapIoSpace returns an
 * uncached pointer, so a plain volatile access is the whole implementation.
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
