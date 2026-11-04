/*
 * vmmouse.sys - seamless mouse for Windows NT 3.1, 3.5, 3.51 and 4.0
 * under QEMU, or anything else with the VMware mouse backdoor.
 *
 * What it does
 *
 *   QEMU's vmmouse device (the VMware backdoor, I/O port 5658h) can report
 *   the host pointer as an absolute position, 0 to 65535 on each axis.
 *   Once the guest asks for that, QEMU stops grabbing the mouse, and the
 *   pointer moves in and out of the window freely. For every event QEMU
 *   also sends a fake PS/2 packet (one pixel right), only to raise the
 *   mouse IRQ.
 *
 *   i8042prt runs the PS/2 port. In NT 3.1 it has one device object for
 *   keyboard and mouse, \Device\KeyboardPort0; \Device\PointerPort0 is a
 *   symbolic link to it. This driver attaches a filter device on top of
 *   the mouse port (IoAttachDevice), so mouclass and kbdclass reach
 *   i8042prt through the filter. The filter passes every request down
 *   unchanged, except:
 *
 *   - IOCTL_INTERNAL_MOUSE_CONNECT: it keeps mouclass's callback and gives
 *     i8042prt its own one.
 *   - IOCTL_INTERNAL_MOUSE_ENABLE and _DISABLE (mouclass sends one for each
 *     open and close of the mouse): at the first open it turns on absolute
 *     mode, at the last close it turns vmmouse off.
 *
 *   In absolute mode the callback drops the fake PS/2 packets and gives
 *   mouclass the queued vmmouse events instead, flagged MOUSE_MOVE_ABSOLUTE,
 *   with the wheel as MOUSE_WHEEL (NT 4.0 scrolls; NT 3.x ignores it).
 *   The window manager (winsrv.dll in NT 3.x, win32k.sys in NT 4.0) scales
 *   them to the screen.
 *
 *   QEMU sends no doorbell while its event queue is full, and the last
 *   doorbell of a burst can wait in a full PS/2 buffer. So a watchdog timer
 *   also empties the queue ten times a second.
 *
 *   On a real PC there is no backdoor: DriverEntry fails and nothing changes.
 *
 * Built against NT 3.1's exports (see ntmini.h, our own small DDK subset, so
 * only the SDK is needed), so it calls only functions that NT 3.1 exports.
 * It writes what it is doing to the State value of its service key
 * (VMMSETUP shows it).
 */

#include "ntmini.h"                 /* our own DDK subset: no DDK needed */

/* the VMware backdoor, as QEMU's vmport and vmmouse devices implement it */
#define VMPORT_MAGIC                0x564D5868      /* "VMXh" */
#define VMPORT_CMD_GETVERSION       10
#define VMMOUSE_CMD_DATA            39
#define VMMOUSE_CMD_STATUS          40
#define VMMOUSE_CMD_COMMAND         41

#define VMMOUSE_READ_ID             0x45414552
#define VMMOUSE_DISABLE             0x000000f5
#define VMMOUSE_REQUEST_ABSOLUTE    0x53424152
#define VMMOUSE_VERSION_ID          0x3442554a

/* RISC transport only: ULONG offsets into the vmmouse-mmio register window
 * (QEMU hw/input/vmmouse-mmio.c). Reading VMM_REG_MAGIC returns VMPORT_MAGIC. */
#define VMM_REG_MAGIC    0          /* 0x00 */
#define VMM_REG_EAX      4          /* 0x10 */
#define VMM_REG_EBX      5          /* 0x14 */
#define VMM_REG_ECX      6          /* 0x18 */
#define VMM_REG_EDX      7          /* 0x1c */
#define VMM_REG_COMMAND  10         /* 0x28 strobe: run the call */

#define VMMOUSE_ERROR               0xffff0000
#define VMMOUSE_RELATIVE_PACKET     0x00010000
#define VMMOUSE_LEFT                0x20
#define VMMOUSE_RIGHT               0x10
#define VMMOUSE_MIDDLE              0x08

#define QUEUE_WORDS     1024        /* size of the vmmouse queue */
#define OUT_MAX         32          /* records per call to mouclass */
#define WATCHDOG_100NS  1000000     /* 0.1 s */
#define START_RETRIES   20          /* watchdog tries to start absolute mode */
#define PORT_NAME_MAX   64
#define STATE_MAX       256

typedef struct _VMREGS {
    ULONG Eax;
    ULONG Ebx;
    ULONG Ecx;
    ULONG Edx;
} VMREGS;

typedef struct _VMM_EXTENSION {
    PDEVICE_OBJECT Lower;           /* i8042prt's device */
    CONNECT_DATA Upper;             /* mouclass's callback */
    CONNECT_DATA Mine;              /* what i8042prt calls instead */
    KSPIN_LOCK Lock;                /* guards everything below */
    KTIMER Timer;
    KDPC TimerDpc;
    WORK_QUEUE_ITEM StateWork;
    LONG EnableCount;               /* opens of the mouse */
    ULONG Retries;                  /* watchdog start tries left */
    ULONG StateGen;                 /* counts state changes */
    BOOLEAN Absolute;               /* vmmouse is in absolute mode */
    BOOLEAN TimerOn;
    BOOLEAN StateQueued;            /* StateWork is queued or running */
    USHORT UnitId;
    ULONG Buttons;                  /* vmmouse buttons that are down */
    LONG X;                         /* last position sent */
    LONG Y;
    ULONG Doorbells;                /* PS/2 packets seen in absolute mode */
    ULONG SeenDoorbells;            /* ... at the last watchdog tick */
    ULONG OutCount;
    MOUSE_INPUT_DATA Out[OUT_MAX];
} VMM_EXTENSION, *PVMM_EXTENSION;

static PWSTR RegPath;               /* our service key */
static WCHAR PortName[PORT_NAME_MAX];

NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath);

/*
 * The backdoor. On x86 it is one IN EAX,DX with the call packed into the CPU
 * registers. RISC CPUs cannot pass registers to a port, so there the same
 * protocol goes through a memory-mapped register window (QEMU's vmmouse-mmio
 * device): write the registers, strobe COMMAND, read them back. VmCall has the
 * same shape on both, so nothing else in the driver changes.
 */
#ifdef _X86_

/* EAX = magic, EBX = argument, ECX = command, DX = port, then IN EAX,DX.
 * The answer comes back in EAX to EDX; VMware may also change ESI and EDI. */
static VOID
VmCall(ULONG Command, ULONG Arg, VMREGS *R)
{
    ULONG a, b, c, d;

    _asm {
        push    ebx
        push    esi
        push    edi
        mov     eax, 564D5868h
        mov     ebx, Arg
        mov     ecx, Command
        mov     edx, 5658h
        in      eax, dx
        mov     a, eax
        mov     b, ebx
        mov     c, ecx
        mov     d, edx
        pop     edi
        pop     esi
        pop     ebx
    }
    R->Eax = a;
    R->Ebx = b;
    R->Ecx = c;
    R->Edx = d;
}

/* On a real PC the port reads as all ones and EBX keeps its value. */
static BOOLEAN
VmPresent(VOID)
{
    VMREGS r;

    VmCall(VMPORT_CMD_GETVERSION, (ULONG) ~VMPORT_MAGIC, &r);
    return (BOOLEAN) (r.Ebx == VMPORT_MAGIC && r.Eax != 0xffffffff);
}

#else   /* RISC: the memory-mapped register window */

static volatile ULONG *VmRegs;      /* the mapped window, or NULL */

/*
 * Physical address of the register window, by machine. The driver maps each in
 * turn and keeps the first whose MAGIC reads back. One list per CPU; the QEMU
 * (lemu) machine wiring fixes these addresses.
 */
static const ULONG VmCandidates[] = {
#if defined(_MIPS_) || defined(_M_MRX000)
    0x8000e000,         /* Jazz (MIPS Magnum) - NT 3.1 / 4.0 MIPS */
    0x13ff0000,         /* SNI RM200 / RM300 - NT 4.0 MIPS */
#elif defined(_ALPHA_) || defined(_M_ALPHA)
    /*
     * DEC Jensen: the device is at EISA (bus) memory 0x00F00000 (longword-
     * aligned, below 32 MB so HAE = 0). Jensen EISA memory is SPARSE, so
     * VmMap() does not map it flat - it builds the HAL QVA (0xA0000000 | this)
     * and lets READ/WRITE_REGISTER_ULONG do the sparse access (see the
     * _ALPHA_ branch of VmMap). The register page answers in QEMU at the
     * sparse CPU address 0x278000060.
     */
    0x00f00000,
#elif defined(_PPC_) || defined(_M_PPC)
    0xc0f00000,         /* IBM 40p / PReP - in the ISA/PCI memory window */
#endif
    0                   /* list terminator */
};

/* Find the window. On MIPS/PPC MmMapIoSpace (PASSIVE_LEVEL only) maps it, so
 * this runs once from DriverEntry (through VmPresent), before any VmCall.
 * VmRegs ends up as a plain pointer (MIPS/PPC) or the HAL's QVA (Alpha);
 * either way VmCall() adds the register offsets to it unchanged, and
 * READ/WRITE_REGISTER_ULONG do the right thing. */
static BOOLEAN
VmMap(VOID)
{
    PHYSICAL_ADDRESS pa;
    volatile ULONG *base;
    int i;

    if (VmRegs != NULL)
        return TRUE;
    pa.HighPart = 0;
    for (i = 0; VmCandidates[i] != 0; i++) {
        pa.LowPart = VmCandidates[i];
#if defined(_ALPHA_) || defined(_M_ALPHA)
        /*
         * DEC Jensen EISA memory is sparse. The NT HAL reaches it with a QVA
         * (qualified virtual address): for a bus-memory byte address B with
         * HAE = 0 (B < 32 MB) the QVA is simply 0xA0000000 | B. The HAL's
         * READ/WRITE_REGISTER_ULONG decode that QVA themselves - they apply
         * the <<7 sparse shift, the EISA-memory superpage base and the
         * longword byte-enable - so a QVA is used directly and is never
         * mapped (MmMapIoSpace would turn it into an ordinary pointer and the
         * access would miss). Confirmed by disassembling HAL0JENS.DLL's
         * READ_REGISTER_ULONG. Because the QVA tracks the bus byte offset
         * one-for-one, the dense VmRegs[reg] indexing addresses each register
         * exactly as on the other platforms, so neither HalTranslateBusAddress
         * nor MmMapIoSpace is involved. */
        base = (volatile ULONG *) (0xA0000000UL | pa.LowPart);
        if (READ_REGISTER_ULONG((PULONG) &base[VMM_REG_MAGIC]) == VMPORT_MAGIC) {
            VmRegs = base;
            return TRUE;
        }
        /* not this candidate - nothing was mapped, so just try the next */
#else
        base = (volatile ULONG *) MmMapIoSpace(pa, 0x1000, MmNonCached);
        if (base == NULL)
            continue;
        if (READ_REGISTER_ULONG((PULONG) &base[VMM_REG_MAGIC]) == VMPORT_MAGIC) {
            VmRegs = base;
            return TRUE;
        }
        MmUnmapIoSpace((PVOID) base, 0x1000);
#endif
    }
    return FALSE;
}

static VOID
VmCall(ULONG Command, ULONG Arg, VMREGS *R)
{
    WRITE_REGISTER_ULONG((PULONG) &VmRegs[VMM_REG_EAX], VMPORT_MAGIC);
    WRITE_REGISTER_ULONG((PULONG) &VmRegs[VMM_REG_EBX], Arg);
    WRITE_REGISTER_ULONG((PULONG) &VmRegs[VMM_REG_ECX], Command);
    WRITE_REGISTER_ULONG((PULONG) &VmRegs[VMM_REG_EDX], 0x5658);
    WRITE_REGISTER_ULONG((PULONG) &VmRegs[VMM_REG_COMMAND], 0);
    R->Eax = READ_REGISTER_ULONG((PULONG) &VmRegs[VMM_REG_EAX]);
    R->Ebx = READ_REGISTER_ULONG((PULONG) &VmRegs[VMM_REG_EBX]);
    R->Ecx = READ_REGISTER_ULONG((PULONG) &VmRegs[VMM_REG_ECX]);
    R->Edx = READ_REGISTER_ULONG((PULONG) &VmRegs[VMM_REG_EDX]);
}

/* On RISC, "present" means we found and mapped the window. */
static BOOLEAN
VmPresent(VOID)
{
    return VmMap();
}

#endif  /* _X86_ */

/*
 * Switch vmmouse on in absolute mode. READ_ID puts the version word at the
 * end of the queue; anything before it is left over. No event word can look
 * like the version word. Caller holds the lock.
 */
static BOOLEAN
VmMouseStart(VOID)
{
    VMREGS r;
    ULONG count;

    VmCall(VMMOUSE_CMD_COMMAND, VMMOUSE_READ_ID, &r);
    VmCall(VMMOUSE_CMD_STATUS, 0, &r);
    if ((r.Eax & VMMOUSE_ERROR) == VMMOUSE_ERROR)
        return FALSE;
    count = r.Eax & 0xffff;
    if (count > QUEUE_WORDS)
        return FALSE;
    for (;;) {
        if (count == 0)
            return FALSE;           /* no version word: the queue was full */
        VmCall(VMMOUSE_CMD_DATA, 1, &r);
        count--;
        if (r.Eax == VMMOUSE_VERSION_ID)
            break;
    }
    VmCall(VMMOUSE_CMD_COMMAND, VMMOUSE_REQUEST_ABSOLUTE, &r);
    return TRUE;
}

/* Give the mouse back to PS/2. QEMU grabs the host mouse again. */
static VOID
VmMouseStop(VOID)
{
    VMREGS r;

    VmCall(VMMOUSE_CMD_COMMAND, VMMOUSE_DISABLE, &r);
}

/*
 * Start, or give the mouse back to PS/2 if that fails. Without the stop,
 * QEMU would go on feeding vmmouse, and only its fake PS/2 packets (one
 * pixel right each) would reach mouclass.
 */
static BOOLEAN
VmMouseStartOrStop(VOID)
{
    if (VmMouseStart())
        return TRUE;
    VmMouseStop();
    return FALSE;
}

static ULONG
WLen(PWSTR s)
{
    ULONG n = 0;

    while (s[n])
        n++;
    return n;
}

static VOID
WCat(PWSTR dst, ULONG max, PWSTR src)
{
    ULONG n = WLen(dst);

    while (*src && n + 1 < max)
        dst[n++] = *src++;
    dst[n] = 0;
}

/* Write the State value of the service key. PASSIVE_LEVEL only. */
static VOID
SetState(PWSTR Text, PWSTR More)
{
    WCHAR buf[STATE_MAX];

    if (RegPath == NULL)
        return;
    buf[0] = 0;
    WCat(buf, STATE_MAX, Text);
    if (More)
        WCat(buf, STATE_MAX, More);
    RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, RegPath, L"State",
                          REG_SZ, buf, (WLen(buf) + 1) * sizeof(WCHAR));
}

/*
 * Make the State value match the extension. Runs in a system worker
 * thread, one at a time, and again while the state keeps changing.
 */
static VOID
StateWorker(IN PVOID Context)
{
    PVMM_EXTENSION ext = Context;
    KIRQL irql;
    ULONG gen;
    LONG open;
    BOOLEAN on;

    for (;;) {
        KeAcquireSpinLock(&ext->Lock, &irql);
        gen = ext->StateGen;
        open = ext->EnableCount;
        on = ext->Absolute;
        KeReleaseSpinLock(&ext->Lock, irql);

        if (on)
            SetState(L"Absolute mode on: seamless mouse. Port: ", PortName);
        else if (open > 0)
            SetState(L"The mouse is open, but vmmouse did not answer: "
                     L"the mouse stays relative. Port: ", PortName);
        else
            SetState(L"Ready: waiting for the mouse to open. Port: ", PortName);

        KeAcquireSpinLock(&ext->Lock, &irql);
        if (gen == ext->StateGen) {
            ext->StateQueued = FALSE;
            KeReleaseSpinLock(&ext->Lock, irql);
            return;
        }
        KeReleaseSpinLock(&ext->Lock, irql);
    }
}

/* The state changed: have a worker thread write it down. Caller holds the lock. */
static VOID
StateChanged(PVMM_EXTENSION Ext)
{
    Ext->StateGen++;
    if (!Ext->StateQueued) {
        Ext->StateQueued = TRUE;
        ExQueueWorkItem(&Ext->StateWork, DelayedWorkQueue);
    }
}

/* Hand the collected records to mouclass. Caller holds the lock. */
static VOID
Flush(PVMM_EXTENSION Ext)
{
    ULONG consumed = 0;

    if (Ext->OutCount == 0 || Ext->Upper.ClassService == NULL) {
        Ext->OutCount = 0;
        return;
    }
    (*(PSERVICE_CALLBACK_ROUTINE) Ext->Upper.ClassService)(
        Ext->Upper.ClassDeviceObject,
        Ext->Out,
        Ext->Out + Ext->OutCount,
        &consumed);
    Ext->OutCount = 0;
}

static PMOUSE_INPUT_DATA
NewRecord(PVMM_EXTENSION Ext)
{
    PMOUSE_INPUT_DATA p;

    if (Ext->OutCount == OUT_MAX)
        Flush(Ext);
    p = &Ext->Out[Ext->OutCount++];
    RtlZeroMemory(p, sizeof(*p));
    p->UnitId = Ext->UnitId;
    return p;
}

/* Leaving absolute mode: let go of any button that is still down. Caller holds the lock. */
static VOID
ReleaseButtons(PVMM_EXTENSION Ext)
{
    PMOUSE_INPUT_DATA p;
    ULONG flags = 0;

    if (Ext->Buttons & VMMOUSE_LEFT)
        flags |= MOUSE_LEFT_BUTTON_UP;
    if (Ext->Buttons & VMMOUSE_RIGHT)
        flags |= MOUSE_RIGHT_BUTTON_UP;
    if (Ext->Buttons & VMMOUSE_MIDDLE)
        flags |= MOUSE_MIDDLE_BUTTON_UP;
    Ext->Buttons = 0;
    if (flags == 0)
        return;
    p = NewRecord(Ext);
    p->Flags = MOUSE_MOVE_RELATIVE;
    p->Buttons = flags;
    Flush(Ext);
}

/* Turn one vmmouse event into mouclass records. Caller holds the lock. */
static VOID
AddEvent(PVMM_EXTENSION Ext, ULONG Status, ULONG X, ULONG Y, ULONG Z)
{
    PMOUSE_INPUT_DATA p;
    ULONG b, changed, flags = 0, raw = 0;
    LONG x, y, dz;

    b = Status & (VMMOUSE_LEFT | VMMOUSE_RIGHT | VMMOUSE_MIDDLE);
    if (b & VMMOUSE_LEFT)
        raw |= 1;
    if (b & VMMOUSE_RIGHT)
        raw |= 2;
    if (b & VMMOUSE_MIDDLE)
        raw |= 4;
    changed = b ^ Ext->Buttons;
    if (changed & VMMOUSE_LEFT)
        flags |= (b & VMMOUSE_LEFT) ? MOUSE_LEFT_BUTTON_DOWN : MOUSE_LEFT_BUTTON_UP;
    if (changed & VMMOUSE_RIGHT)
        flags |= (b & VMMOUSE_RIGHT) ? MOUSE_RIGHT_BUTTON_DOWN : MOUSE_RIGHT_BUTTON_UP;
    if (changed & VMMOUSE_MIDDLE)
        flags |= (b & VMMOUSE_MIDDLE) ? MOUSE_MIDDLE_BUTTON_DOWN : MOUSE_MIDDLE_BUTTON_UP;
    Ext->Buttons = b;

    /*
     * The wheel: Z counts the notches since the last event, -1 for each
     * notch away from the user, +1 towards. Only its low byte counts, as
     * on VMware. It rides in the button record, as on a real IntelliMouse.
     */
    dz = (LONG) (signed char) (Z & 0xff);
    if (dz != 0)
        flags |= MOUSE_WHEEL
               | ((ULONG) (USHORT) (SHORT) (-dz * WHEEL_DELTA) << 16);

    if (Status & VMMOUSE_RELATIVE_PACKET) {
        /* queued just before absolute mode began: pass the motion on */
        p = NewRecord(Ext);
        p->Flags = MOUSE_MOVE_RELATIVE;
        p->Buttons = flags;
        p->RawButtons = raw;
        p->LastX = (LONG) X;
        p->LastY = (LONG) Y;
        return;
    }

    x = (LONG) (X & 0xffff);
    y = (LONG) (Y & 0xffff);
    /* the window manager ignores a move to exactly 0,0; 1,0 is still pixel 0,0 */
    if (x == 0 && y == 0)
        x = 1;

    /*
     * The window manager handles the buttons of a record before its move.
     * Send the move first, so that a click lands where the pointer is now.
     */
    if (x != Ext->X || y != Ext->Y) {
        p = NewRecord(Ext);
        p->Flags = MOUSE_MOVE_ABSOLUTE;
        p->RawButtons = raw;
        p->LastX = x;
        p->LastY = y;
        Ext->X = x;
        Ext->Y = y;
    }
    if (flags) {
        p = NewRecord(Ext);
        p->Flags = MOUSE_MOVE_ABSOLUTE;
        p->Buttons = flags;
        p->RawButtons = raw;
        p->LastX = x;
        p->LastY = y;
    }
}

/* vmmouse lost its state (a reset or a bad request): start it again. Caller holds the lock. */
static VOID
Restart(PVMM_EXTENSION Ext)
{
    if (VmMouseStartOrStop())
        return;
    Ext->Absolute = FALSE;
    ReleaseButtons(Ext);
    StateChanged(Ext);
}

/*
 * Give mouclass everything waiting in the vmmouse queue.
 * Caller holds the lock, at DISPATCH_LEVEL.
 */
static VOID
Drain(PVMM_EXTENSION Ext)
{
    VMREGS r;
    ULONG count, budget = QUEUE_WORDS / 4;

    while (Ext->Absolute && budget > 0) {
        VmCall(VMMOUSE_CMD_STATUS, 0, &r);
        count = r.Eax & 0xffff;
        if ((r.Eax & VMMOUSE_ERROR) == VMMOUSE_ERROR
            || count % 4 != 0 || count > QUEUE_WORDS) {
            Restart(Ext);
            break;
        }
        if (count == 0)
            break;
        for (count /= 4; count > 0 && budget > 0; count--, budget--) {
            VmCall(VMMOUSE_CMD_DATA, 4, &r);
            AddEvent(Ext, r.Eax, r.Ebx, r.Ecx, r.Edx);
        }
    }
    Flush(Ext);
}

/*
 * i8042prt calls this from its DPC with new PS/2 packets, where it would
 * have called mouclass.
 */
static VOID
ServiceCallback(
    IN PDEVICE_OBJECT DeviceObject,
    IN PMOUSE_INPUT_DATA InputDataStart,
    IN PMOUSE_INPUT_DATA InputDataEnd,
    IN OUT PULONG InputDataConsumed
    )
{
    PVMM_EXTENSION ext = DeviceObject->DeviceExtension;

    KeAcquireSpinLockAtDpcLevel(&ext->Lock);
    if (!ext->Absolute) {
        KeReleaseSpinLockFromDpcLevel(&ext->Lock);
        (*(PSERVICE_CALLBACK_ROUTINE) ext->Upper.ClassService)(
            ext->Upper.ClassDeviceObject,
            InputDataStart,
            InputDataEnd,
            InputDataConsumed);
        return;
    }
    if (InputDataStart < InputDataEnd)
        ext->UnitId = InputDataStart->UnitId;
    ext->Doorbells++;
    Drain(ext);
    KeReleaseSpinLockFromDpcLevel(&ext->Lock);

    /* the PS/2 packets only rang the doorbell: drop them */
    *InputDataConsumed = InputDataEnd - InputDataStart;
}

static VOID
ArmTimer(PVMM_EXTENSION Ext)
{
    LARGE_INTEGER due;

    due.LowPart = (ULONG) -WATCHDOG_100NS;  /* negative: relative to now */
    due.HighPart = -1;
    KeSetTimer(&Ext->Timer, due, &Ext->TimerDpc);
}

/*
 * Every 0.1 s while the mouse is open: if no PS/2 packet came, empty the
 * queue anyway. If absolute mode is off, try to start it again.
 */
static VOID
WatchdogDpc(IN PKDPC Dpc, IN PVOID Context, IN PVOID Arg1, IN PVOID Arg2)
{
    PVMM_EXTENSION ext = Context;

    KeAcquireSpinLockAtDpcLevel(&ext->Lock);
    if (ext->Absolute) {
        if (ext->Doorbells == ext->SeenDoorbells)
            Drain(ext);
    } else if (ext->EnableCount > 0 && ext->Retries > 0) {
        ext->Retries--;
        ext->Buttons = 0;
        ext->X = -1;
        ext->Y = -1;
        ext->Absolute = VmMouseStartOrStop();
        if (ext->Absolute)
            StateChanged(ext);
    }
    ext->SeenDoorbells = ext->Doorbells;
    if (ext->TimerOn)
        ArmTimer(ext);
    KeReleaseSpinLockFromDpcLevel(&ext->Lock);
}

/* The mouse was opened. PASSIVE_LEVEL. */
static VOID
Enable(PVMM_EXTENSION Ext)
{
    KIRQL irql;

    KeAcquireSpinLock(&Ext->Lock, &irql);
    if (++Ext->EnableCount == 1) {
        Ext->Buttons = 0;
        Ext->X = -1;
        Ext->Y = -1;
        Ext->Retries = START_RETRIES;
        Ext->Absolute = VmMouseStartOrStop();
        if (!Ext->TimerOn) {
            Ext->TimerOn = TRUE;
            ArmTimer(Ext);
        }
        StateChanged(Ext);
    }
    KeReleaseSpinLock(&Ext->Lock, irql);
}

/* The mouse was closed. PASSIVE_LEVEL. */
static VOID
Disable(PVMM_EXTENSION Ext)
{
    KIRQL irql;

    KeAcquireSpinLock(&Ext->Lock, &irql);
    if (Ext->EnableCount > 0 && --Ext->EnableCount == 0) {
        if (Ext->Absolute) {
            VmMouseStop();
            Ext->Absolute = FALSE;
            ReleaseButtons(Ext);
        }
        Ext->TimerOn = FALSE;
        KeCancelTimer(&Ext->Timer);
        StateChanged(Ext);
    }
    KeReleaseSpinLock(&Ext->Lock, irql);
}

/* Copy our stack location to the next one, without a completion routine. */
static VOID
CopyStack(PIRP Irp)
{
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    PIO_STACK_LOCATION next = IoGetNextIrpStackLocation(Irp);

    next->MajorFunction = sp->MajorFunction;
    next->MinorFunction = sp->MinorFunction;
    next->Flags = sp->Flags;
    next->Control = 0;
    next->Parameters = sp->Parameters;
    next->FileObject = sp->FileObject;
    next->CompletionRoutine = NULL;
    next->Context = NULL;
}

static NTSTATUS
PassDown(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PVMM_EXTENSION ext = DeviceObject->DeviceExtension;

    CopyStack(Irp);
    return IoCallDriver(ext->Lower, Irp);
}

/* mouclass connects: keep its callback, give i8042prt ours. */
static NTSTATUS
Connect(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PVMM_EXTENSION ext = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;

    if (ext->Upper.ClassService != NULL) {
        status = STATUS_SHARING_VIOLATION;
    } else if (sp->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_DATA)) {
        status = STATUS_INVALID_PARAMETER;
    } else {
        ext->Upper = *(PCONNECT_DATA) sp->Parameters.DeviceIoControl.Type3InputBuffer;
        ext->Mine.ClassDeviceObject = DeviceObject;
        ext->Mine.ClassService = (PVOID) ServiceCallback;
        CopyStack(Irp);
        IoGetNextIrpStackLocation(Irp)->Parameters.DeviceIoControl.Type3InputBuffer = &ext->Mine;
        status = IoCallDriver(ext->Lower, Irp);
        if (!NT_SUCCESS(status))
            ext->Upper.ClassService = NULL;
        return status;
    }
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS
InternalIoctl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
    PVMM_EXTENSION ext = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION sp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;

    switch (sp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_INTERNAL_MOUSE_CONNECT:
        return Connect(DeviceObject, Irp);
    case IOCTL_INTERNAL_MOUSE_ENABLE:
        status = PassDown(DeviceObject, Irp);
        if (NT_SUCCESS(status))
            Enable(ext);
        return status;
    case IOCTL_INTERNAL_MOUSE_DISABLE:
        status = PassDown(DeviceObject, Irp);
        if (NT_SUCCESS(status))
            Disable(ext);
        return status;
    default:
        return PassDown(DeviceObject, Irp);
    }
}

/*
 * HARDWARE\DEVICEMAP\PointerPort lists each pointer port device, e.g.
 * \Device\PointerPort0 = \REGISTRY\Machine\System\ControlSet001\Services\i8042prt.
 * Pick the one that i8042prt owns.
 */
static NTSTATUS
PortQuery(IN PWSTR ValueName, IN ULONG ValueType, IN PVOID ValueData,
          IN ULONG ValueLength, IN PVOID Context, IN PVOID EntryContext)
{
    static WCHAR tail[] = L"\\i8042prt";
    PWSTR data = ValueData;
    ULONG n = ValueLength / sizeof(WCHAR), t = sizeof(tail) / sizeof(WCHAR) - 1, i;
    WCHAR c;

    if (PortName[0] || ValueType != REG_SZ || data == NULL)
        return STATUS_SUCCESS;
    while (n > 0 && data[n - 1] == 0)
        n--;
    if (n < t)
        return STATUS_SUCCESS;
    for (i = 0; i < t; i++) {
        c = data[n - t + i];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != tail[i])
            return STATUS_SUCCESS;
    }
    WCat(PortName, PORT_NAME_MAX, ValueName);
    return STATUS_SUCCESS;
}

static BOOLEAN
FindPort(VOID)
{
    RTL_QUERY_REGISTRY_TABLE table[2];

    RtlZeroMemory(table, sizeof(table));
    table[0].QueryRoutine = PortQuery;
    PortName[0] = 0;
    RtlQueryRegistryValues(RTL_REGISTRY_DEVICEMAP, L"PointerPort", table, NULL, NULL);
    return (BOOLEAN) (PortName[0] != 0);
}

/* DriverEntry fails: say why, free what we kept. */
static NTSTATUS
Fail(NTSTATUS Status, PWSTR Text, PWSTR More)
{
    SetState(Text, More);
    if (RegPath) {
        ExFreePool(RegPath);
        RegPath = NULL;
    }
    return Status;
}

NTSTATUS
DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath)
{
    PDEVICE_OBJECT dev;
    PVMM_EXTENSION ext;
    UNICODE_STRING port;
    NTSTATUS status;
    ULONG i;

    RegPath = ExAllocatePool(NonPagedPool, RegistryPath->Length + sizeof(WCHAR));
    if (RegPath) {
        RtlCopyMemory(RegPath, RegistryPath->Buffer, RegistryPath->Length);
        RegPath[RegistryPath->Length / sizeof(WCHAR)] = 0;
    }

    if (!VmPresent())
        return Fail(STATUS_NO_SUCH_DEVICE,
                    L"No QEMU or VMware backdoor (port 5658h): not started.", NULL);
    if (!FindPort())
        return Fail(STATUS_NO_SUCH_DEVICE,
                    L"No i8042prt mouse in HARDWARE\\DEVICEMAP\\PointerPort: not started. "
                    L"VMMouse must load after i8042prt.", NULL);

    status = IoCreateDevice(DriverObject, sizeof(VMM_EXTENSION), NULL,
                            FILE_DEVICE_MOUSE, 0, FALSE, &dev);
    if (!NT_SUCCESS(status))
        return Fail(status, L"IoCreateDevice failed: not started.", NULL);
    ext = dev->DeviceExtension;
    RtlZeroMemory(ext, sizeof(VMM_EXTENSION));
    KeInitializeSpinLock(&ext->Lock);
    KeInitializeTimer(&ext->Timer);
    KeInitializeDpc(&ext->TimerDpc, WatchdogDpc, ext);
    ExInitializeWorkItem(&ext->StateWork, StateWorker, ext);

    /* the dispatch table must be ready before the filter joins the stack */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
        DriverObject->MajorFunction[i] = PassDown;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = InternalIoctl;

    RtlInitUnicodeString(&port, PortName);
    status = IoAttachDevice(dev, &port, &ext->Lower);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(dev);
        return Fail(status, L"IoAttachDevice failed: not started. Port: ", PortName);
    }
    dev->Flags |= ext->Lower->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO);
    dev->Flags &= ~DO_DEVICE_INITIALIZING;

    SetState(L"Ready: waiting for the mouse to open. Port: ", PortName);
    return STATUS_SUCCESS;
}
