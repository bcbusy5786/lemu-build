/*
 * ntmini.h - the small part of the Windows NT DDK that vmmouse.sys uses.
 *
 * Copied from the Windows NT 3.1 DDK headers (ntdef.h, ntddk.h, ntstatus.h,
 * devioctl.h, kbdmou.h, ntddmou.h): same types, same field order, same
 * packing. With it, vmmouse.sys builds with a plain Win32 SDK compiler,
 * without the DDK, on any CPU that Windows NT runs on.
 *
 * Only what vmmouse.c needs is here. Structures that the driver only points
 * at are left incomplete, and DEVICE_OBJECT and IRP stop after the fields
 * the driver uses: never use sizeof on those.
 */

#ifndef NTMINI_H
#define NTMINI_H

#include <string.h>                 /* memcpy and memset, inline with -Ox */

/* ---- basic types (ntdef.h) ---- */

#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define VOID void
#ifndef NULL
#define NULL ((void *) 0)
#endif
#define TRUE 1
#define FALSE 0

#if (_MSC_VER >= 800)
#define NTAPI __stdcall
#else
#define NTAPI
#endif

typedef char CHAR, CCHAR, *PCHAR;
typedef short SHORT, CSHORT;
typedef long LONG, *PLONG;
typedef unsigned char UCHAR, *PUCHAR;
typedef unsigned short USHORT, *PUSHORT;
typedef unsigned long ULONG, *PULONG;
typedef void *PVOID;
typedef UCHAR BOOLEAN, *PBOOLEAN;
typedef unsigned short WCHAR, *PWSTR;   /* NT WCHAR is 16-bit, not C wchar_t */
typedef CONST WCHAR *PCWSTR;
typedef LONG NTSTATUS;

#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)

/*
 * 64-bit integers. The NT 3.1 DDK uses double on x86 and MIPS, whose
 * compilers had no __int64 yet; it has the same size and alignment.
 */
#if defined(_M_ALPHA) || defined(_ALPHA_) || defined(_M_PPC) || defined(_PPC_)
typedef __int64 LONGLONG;
typedef unsigned __int64 ULONGLONG;
#else
typedef double LONGLONG;
typedef double ULONGLONG;
#endif

typedef union _LARGE_INTEGER {
    struct {
        ULONG LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct {
        ULONG LowPart;
        ULONG HighPart;
    };
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

typedef struct _LIST_ENTRY {
    struct _LIST_ENTRY *Flink;
    struct _LIST_ENTRY *Blink;
} LIST_ENTRY, *PLIST_ENTRY;

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

/* ---- status codes (ntstatus.h) ---- */

#define STATUS_SUCCESS              ((NTSTATUS) 0x00000000L)
#define STATUS_INVALID_PARAMETER    ((NTSTATUS) 0xC000000DL)
#define STATUS_NO_SUCH_DEVICE       ((NTSTATUS) 0xC000000EL)
#define STATUS_SHARING_VIOLATION    ((NTSTATUS) 0xC0000043L)

/* ---- kernel objects (ntddk.h) ---- */

typedef UCHAR KIRQL, *PKIRQL;
typedef CCHAR KPROCESSOR_MODE;
typedef ULONG KSPIN_LOCK, *PKSPIN_LOCK;

typedef struct _DISPATCHER_HEADER {
    CSHORT Type;
    CSHORT Size;
    LONG SignalState;
    LIST_ENTRY WaitListHead;
} DISPATCHER_HEADER;

struct _KDPC;

typedef VOID (*PKDEFERRED_ROUTINE)(
    IN struct _KDPC *Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
    );

typedef struct _KDPC {
    CSHORT Type;
    CSHORT Size;
    LIST_ENTRY DpcListEntry;
    PKDEFERRED_ROUTINE DeferredRoutine;
    PVOID DeferredContext;
    PVOID SystemArgument1;
    PVOID SystemArgument2;
    BOOLEAN Inserted;
} KDPC, *PKDPC;

typedef struct _KTIMER {
    DISPATCHER_HEADER Header;
    ULARGE_INTEGER DueTime;
    LIST_ENTRY TimerListEntry;
    struct _KDPC *Dpc;
    BOOLEAN Inserted;
} KTIMER, *PKTIMER;

typedef struct _KDEVICE_QUEUE_ENTRY {
    LIST_ENTRY DeviceListEntry;
    ULONG SortKey;
    BOOLEAN Inserted;
} KDEVICE_QUEUE_ENTRY;

typedef enum _POOL_TYPE {
    NonPagedPool,
    PagedPool,
    NonPagedPoolMustSucceed,
    DontUseThisType,
    NonPagedPoolCacheAligned,
    PagedPoolCacheAligned,
    NonPagedPoolCacheAlignedMustS,
    MaxPoolType
} POOL_TYPE;

typedef VOID (*PWORKER_THREAD_ROUTINE)(IN PVOID Parameter);

typedef struct _WORK_QUEUE_ITEM {
    LIST_ENTRY List;
    PWORKER_THREAD_ROUTINE WorkerRoutine;
    PVOID Parameter;
} WORK_QUEUE_ITEM, *PWORK_QUEUE_ITEM;

typedef enum _WORK_QUEUE_TYPE {
    CriticalWorkQueue,
    DelayedWorkQueue,
    MaximumWorkQueue
} WORK_QUEUE_TYPE;

#define ExInitializeWorkItem(Item, Routine, Context) \
    (Item)->WorkerRoutine = (Routine);               \
    (Item)->Parameter = (Context);                   \
    (Item)->List.Flink = NULL;

/* ---- I/O manager (ntddk.h, devioctl.h) ---- */

#define DEVICE_TYPE ULONG
#define FILE_DEVICE_MOUSE           0x0000000f
#define METHOD_NEITHER              3
#define FILE_ANY_ACCESS             0
#define CTL_CODE(DeviceType, Function, Method, Access) ( \
    ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method) \
)

#define IRP_MJ_INTERNAL_DEVICE_CONTROL  0x0f
#define IRP_MJ_MAXIMUM_FUNCTION         0x15    /* NT 3.1; later NTs have more */

#define DO_BUFFERED_IO              0x00000004
#define DO_DIRECT_IO                0x00000010
#define DO_DEVICE_INITIALIZING      0x00000080

#define IO_NO_INCREMENT             0

struct _DEVICE_OBJECT;
struct _DRIVER_OBJECT;
struct _IRP;
struct _IO_STACK_LOCATION;

typedef NTSTATUS (*PDRIVER_DISPATCH)(
    IN struct _DEVICE_OBJECT *DeviceObject,
    IN struct _IRP *Irp
    );

typedef NTSTATUS (*PIO_COMPLETION_ROUTINE)(
    IN struct _DEVICE_OBJECT *DeviceObject,
    IN struct _IRP *Irp,
    IN PVOID Context
    );

typedef struct _IO_STATUS_BLOCK {
    NTSTATUS Status;
    ULONG Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

/* Incomplete: the fields after DeviceType are left out. */
typedef struct _DEVICE_OBJECT {
    CSHORT Type;
    USHORT Size;
    LONG ReferenceCount;
    struct _DRIVER_OBJECT *DriverObject;
    struct _DEVICE_OBJECT *NextDevice;
    struct _DEVICE_OBJECT *AttachedDevice;
    struct _IRP *CurrentIrp;
    PVOID Timer;
    ULONG Flags;
    ULONG Characteristics;
    PVOID Vpb;
    PVOID DeviceExtension;
    DEVICE_TYPE DeviceType;
} DEVICE_OBJECT, *PDEVICE_OBJECT;

typedef struct _DRIVER_OBJECT {
    CSHORT Type;
    CSHORT Size;
    PDEVICE_OBJECT DeviceObject;
    ULONG Flags;
    PVOID DriverStart;
    ULONG DriverSize;
    PVOID DriverSection;
    ULONG Count;
    UNICODE_STRING DriverName;
    PUNICODE_STRING HardwareDatabase;
    PVOID FastIoDispatch;
    PVOID DriverInit;
    PVOID DriverStartIo;
    PVOID DriverUnload;
    PDRIVER_DISPATCH MajorFunction[IRP_MJ_MAXIMUM_FUNCTION + 1];
} DRIVER_OBJECT, *PDRIVER_OBJECT;

/* Incomplete: the Tail union's KAPC member is left out. */
typedef struct _IRP {
    CSHORT Type;
    USHORT Size;
    PVOID MdlAddress;
    ULONG Flags;
    union {
        struct _IRP *MasterIrp;
        LONG IrpCount;
        PVOID SystemBuffer;
    } AssociatedIrp;
    LIST_ENTRY ThreadListEntry;
    IO_STATUS_BLOCK IoStatus;
    KPROCESSOR_MODE RequestorMode;
    BOOLEAN PendingReturned;
    CCHAR StackCount;
    CCHAR CurrentLocation;
    BOOLEAN Cancel;
    KIRQL CancelIrql;
    CCHAR ApcEnvironment;
    BOOLEAN Zoned;
    PIO_STATUS_BLOCK UserIosb;
    PVOID UserEvent;
    union {
        struct {
            PVOID UserApcRoutine;
            PVOID UserApcContext;
        } AsynchronousParameters;
        LARGE_INTEGER AllocationSize;
    } Overlay;
    PVOID CancelRoutine;
    PVOID UserBuffer;
    union {
        struct {
            KDEVICE_QUEUE_ENTRY DeviceQueueEntry;
            PVOID Thread;
            PCHAR AuxiliaryBuffer;
            LIST_ENTRY ListEntry;
            struct _IO_STACK_LOCATION *CurrentStackLocation;
            PVOID OriginalFileObject;
        } Overlay;
    } Tail;
} IRP, *PIRP;

/*
 * The DDK packs the stack location to 4 bytes on every CPU except Alpha
 * (ntddk.h: "#if !defined(_ALPHA_)" around "#pragma pack(4)"). Alpha keeps
 * natural alignment, so Parameters - it holds LARGE_INTEGERs - starts at
 * offset 8 and a stack location is 0x28 bytes there, 0x24 everywhere else.
 * Get this wrong and every request the filter passes down lands 4 bytes off:
 * i8042prt rejects them, and keyboard and mouse both die.
 */
#if !defined(_ALPHA_) && !defined(_M_ALPHA)
#pragma pack(4)
#endif
typedef struct _IO_STACK_LOCATION {
    UCHAR MajorFunction;
    UCHAR MinorFunction;
    UCHAR Flags;
    UCHAR Control;
    union {
        struct {
            ULONG Length;
            ULONG Key;
            LARGE_INTEGER ByteOffset;
        } Read;
        struct {
            ULONG OutputBufferLength;
            ULONG InputBufferLength;
            ULONG IoControlCode;
            PVOID Type3InputBuffer;
        } DeviceIoControl;
        struct {
            PVOID Argument1;
            PVOID Argument2;
            PVOID Argument3;
            PVOID Argument4;
        } Others;
    } Parameters;
    PDEVICE_OBJECT DeviceObject;
    PVOID FileObject;
    PIO_COMPLETION_ROUTINE CompletionRoutine;
    PVOID Context;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
#if !defined(_ALPHA_) && !defined(_M_ALPHA)
#pragma pack()
#endif

/* The build fails here if the layout ever drifts from the kernel's. NT
 * targets only: a 64-bit host compiler that only checks syntax gets other
 * sizes. */
#if defined(_ALPHA_) || defined(_M_ALPHA)
typedef char IoStackLocationSizeCheck[sizeof(IO_STACK_LOCATION) == 0x28 ? 1 : -1];
#elif defined(_X86_) || defined(_M_IX86) || defined(_MIPS_) || defined(_M_MRX000) \
   || defined(_PPC_) || defined(_M_PPC)
typedef char IoStackLocationSizeCheck[sizeof(IO_STACK_LOCATION) == 0x24 ? 1 : -1];
#endif

#define IoGetCurrentIrpStackLocation(Irp) ((Irp)->Tail.Overlay.CurrentStackLocation)
#define IoGetNextIrpStackLocation(Irp) ((Irp)->Tail.Overlay.CurrentStackLocation - 1)

/* ---- registry (ntddk.h) ---- */

#define REG_SZ                      1
#define RTL_REGISTRY_ABSOLUTE       0
#define RTL_REGISTRY_DEVICEMAP      4

typedef NTSTATUS (*PRTL_QUERY_REGISTRY_ROUTINE)(
    IN PWSTR ValueName,
    IN ULONG ValueType,
    IN PVOID ValueData,
    IN ULONG ValueLength,
    IN PVOID Context,
    IN PVOID EntryContext
    );

typedef struct _RTL_QUERY_REGISTRY_TABLE {
    PRTL_QUERY_REGISTRY_ROUTINE QueryRoutine;
    ULONG Flags;
    PWSTR Name;
    PVOID EntryContext;
    ULONG DefaultType;
    PVOID DefaultData;
    ULONG DefaultLength;
} RTL_QUERY_REGISTRY_TABLE, *PRTL_QUERY_REGISTRY_TABLE;

#define RtlCopyMemory(Destination, Source, Length) memcpy((Destination), (Source), (Length))
#define RtlZeroMemory(Destination, Length) memset((Destination), 0, (Length))

/* ---- mouse class and port interface (kbdmou.h, ntddmou.h) ---- */

typedef struct _CONNECT_DATA {
    IN PDEVICE_OBJECT ClassDeviceObject;
    IN PVOID ClassService;
} CONNECT_DATA, *PCONNECT_DATA;

typedef VOID (*PSERVICE_CALLBACK_ROUTINE)(
    IN PVOID NormalContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2,
    IN OUT PVOID SystemArgument3
    );

#define IOCTL_INTERNAL_MOUSE_CONNECT CTL_CODE(FILE_DEVICE_MOUSE, 0x0080, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_INTERNAL_MOUSE_ENABLE  CTL_CODE(FILE_DEVICE_MOUSE, 0x0200, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_INTERNAL_MOUSE_DISABLE CTL_CODE(FILE_DEVICE_MOUSE, 0x0400, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef struct _MOUSE_INPUT_DATA {
    USHORT UnitId;
    USHORT Flags;
    ULONG Buttons;
    ULONG RawButtons;
    LONG LastX;
    LONG LastY;
    ULONG ExtraInformation;
} MOUSE_INPUT_DATA, *PMOUSE_INPUT_DATA;

#define MOUSE_MOVE_RELATIVE         0
#define MOUSE_MOVE_ABSOLUTE         1
#define MOUSE_LEFT_BUTTON_DOWN      0x0001
#define MOUSE_LEFT_BUTTON_UP        0x0002
#define MOUSE_RIGHT_BUTTON_DOWN     0x0004
#define MOUSE_RIGHT_BUTTON_UP       0x0008
#define MOUSE_MIDDLE_BUTTON_DOWN    0x0010
#define MOUSE_MIDDLE_BUTTON_UP      0x0020
/*
 * The wheel (NT 4.0; NT 3.x has none and ignores the flag). The delta,
 * WHEEL_DELTA per notch and positive away from the user, goes in the high
 * word of Buttons: the DDK's ButtonData. NT is little-endian on every CPU.
 */
#define MOUSE_WHEEL                 0x0400
#define WHEEL_DELTA                 120

/* ---- kernel functions: all of them exist in NT 3.1 ---- */

PVOID ExAllocatePool(IN POOL_TYPE PoolType, IN ULONG NumberOfBytes);

/*
 * Physical-memory mapping and device-register access. Used only by the RISC
 * (MIPS/Alpha/PowerPC) build, to reach the memory-mapped vmmouse backdoor.
 * READ/WRITE_REGISTER_ULONG are plain macros on x86 but real HAL functions on
 * RISC; the x86 build here uses the IN/OUT port path instead and never calls
 * any of these.
 */
typedef ULONG SIZE_T;
typedef LARGE_INTEGER PHYSICAL_ADDRESS;
typedef enum _MEMORY_CACHING_TYPE {
    MmNonCached = 0,
    MmCached = 1,
    MmWriteCombined = 2
} MEMORY_CACHING_TYPE;
PVOID MmMapIoSpace(IN PHYSICAL_ADDRESS PhysicalAddress, IN SIZE_T NumberOfBytes,
                   IN MEMORY_CACHING_TYPE CacheType);
VOID MmUnmapIoSpace(IN PVOID BaseAddress, IN SIZE_T NumberOfBytes);
ULONG READ_REGISTER_ULONG(IN PULONG Register);
VOID WRITE_REGISTER_ULONG(IN PULONG Register, IN ULONG Value);

#if defined(_ALPHA_) || defined(_M_ALPHA)
/*
 * DEC Jensen (Alpha) only. Its EISA memory is "sparse": the HAL turns a bus
 * address into a QVA that READ/WRITE_REGISTER_ULONG know how to poke. We ask
 * for it the standard way. (Only the Isa entry of INTERFACE_TYPE is used; the
 * values match the DDK.)
 */
typedef enum _INTERFACE_TYPE {
    InterfaceTypeUndefined = -1,
    Internal = 0,
    Isa = 1,
    Eisa = 2
} INTERFACE_TYPE;
BOOLEAN HalTranslateBusAddress(IN INTERFACE_TYPE InterfaceType, IN ULONG BusNumber,
                               IN PHYSICAL_ADDRESS BusAddress,
                               IN OUT PULONG AddressSpace,
                               OUT PHYSICAL_ADDRESS *TranslatedAddress);
#endif

VOID ExFreePool(IN PVOID P);
VOID ExQueueWorkItem(IN PWORK_QUEUE_ITEM WorkItem, IN WORK_QUEUE_TYPE QueueType);

NTSTATUS IoAttachDevice(IN PDEVICE_OBJECT SourceDevice, IN PUNICODE_STRING TargetDevice,
                        OUT PDEVICE_OBJECT *AttachedDevice);
NTSTATUS IoCallDriver(IN PDEVICE_OBJECT DeviceObject, IN OUT PIRP Irp);
VOID IoCompleteRequest(IN PIRP Irp, IN CCHAR PriorityBoost);
NTSTATUS IoCreateDevice(IN PDRIVER_OBJECT DriverObject, IN ULONG DeviceExtensionSize,
                        IN PUNICODE_STRING DeviceName OPTIONAL, IN DEVICE_TYPE DeviceType,
                        IN ULONG DeviceCharacteristics, IN BOOLEAN Exclusive,
                        OUT PDEVICE_OBJECT *DeviceObject);
VOID IoDeleteDevice(IN PDEVICE_OBJECT DeviceObject);

VOID KeInitializeSpinLock(IN PKSPIN_LOCK SpinLock);
VOID KeAcquireSpinLock(IN PKSPIN_LOCK SpinLock, OUT PKIRQL OldIrql);
VOID KeReleaseSpinLock(IN PKSPIN_LOCK SpinLock, IN KIRQL NewIrql);
VOID KeAcquireSpinLockAtDpcLevel(IN PKSPIN_LOCK SpinLock);
VOID KeReleaseSpinLockFromDpcLevel(IN PKSPIN_LOCK SpinLock);
VOID KeInitializeDpc(IN PKDPC Dpc, IN PKDEFERRED_ROUTINE DeferredRoutine, IN PVOID DeferredContext);
VOID KeInitializeTimer(IN PKTIMER Timer);
BOOLEAN KeSetTimer(IN PKTIMER Timer, IN LARGE_INTEGER DueTime, IN PKDPC Dpc OPTIONAL);
BOOLEAN KeCancelTimer(IN PKTIMER Timer);

VOID NTAPI RtlInitUnicodeString(PUNICODE_STRING DestinationString, PCWSTR SourceString);
NTSTATUS NTAPI RtlQueryRegistryValues(IN ULONG RelativeTo, IN PWSTR Path,
                                      IN PRTL_QUERY_REGISTRY_TABLE QueryTable,
                                      IN PVOID Context, IN PVOID Environment OPTIONAL);
NTSTATUS NTAPI RtlWriteRegistryValue(IN ULONG RelativeTo, IN PWSTR Path, IN PWSTR ValueName,
                                     IN ULONG ValueType, IN PVOID ValueData, IN ULONG ValueLength);

#endif /* NTMINI_H */
