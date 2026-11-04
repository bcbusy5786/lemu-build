/*
 * ntstubs.c - empty copies of the kernel functions that vmmouse.sys calls.
 *
 * Never linked into the driver. lib32 reads them while it turns
 * ntoskrnl.def and hal.def into import libraries: from them it learns each
 * function's decorated name on x86 (_IoCallDriver@8), while the driver still
 * imports the plain name (IoCallDriver) that Windows NT exports.
 */

#include "ntmini.h"

PVOID ExAllocatePool(IN POOL_TYPE PoolType, IN ULONG NumberOfBytes) { return NULL; }
VOID ExFreePool(IN PVOID P) { }
VOID ExQueueWorkItem(IN PWORK_QUEUE_ITEM WorkItem, IN WORK_QUEUE_TYPE QueueType) { }

NTSTATUS IoAttachDevice(IN PDEVICE_OBJECT SourceDevice, IN PUNICODE_STRING TargetDevice,
                        OUT PDEVICE_OBJECT *AttachedDevice) { return 0; }
NTSTATUS IoCallDriver(IN PDEVICE_OBJECT DeviceObject, IN OUT PIRP Irp) { return 0; }
VOID IoCompleteRequest(IN PIRP Irp, IN CCHAR PriorityBoost) { }
NTSTATUS IoCreateDevice(IN PDRIVER_OBJECT DriverObject, IN ULONG DeviceExtensionSize,
                        IN PUNICODE_STRING DeviceName OPTIONAL, IN DEVICE_TYPE DeviceType,
                        IN ULONG DeviceCharacteristics, IN BOOLEAN Exclusive,
                        OUT PDEVICE_OBJECT *DeviceObject) { return 0; }
VOID IoDeleteDevice(IN PDEVICE_OBJECT DeviceObject) { }

VOID KeInitializeSpinLock(IN PKSPIN_LOCK SpinLock) { }
VOID KeAcquireSpinLock(IN PKSPIN_LOCK SpinLock, OUT PKIRQL OldIrql) { }
VOID KeReleaseSpinLock(IN PKSPIN_LOCK SpinLock, IN KIRQL NewIrql) { }
VOID KeAcquireSpinLockAtDpcLevel(IN PKSPIN_LOCK SpinLock) { }
VOID KeReleaseSpinLockFromDpcLevel(IN PKSPIN_LOCK SpinLock) { }
VOID KeInitializeDpc(IN PKDPC Dpc, IN PKDEFERRED_ROUTINE DeferredRoutine,
                     IN PVOID DeferredContext) { }
VOID KeInitializeTimer(IN PKTIMER Timer) { }
BOOLEAN KeSetTimer(IN PKTIMER Timer, IN LARGE_INTEGER DueTime, IN PKDPC Dpc OPTIONAL) { return 0; }
BOOLEAN KeCancelTimer(IN PKTIMER Timer) { return 0; }

VOID NTAPI RtlInitUnicodeString(PUNICODE_STRING DestinationString, PCWSTR SourceString) { }
NTSTATUS NTAPI RtlQueryRegistryValues(IN ULONG RelativeTo, IN PWSTR Path,
                                      IN PRTL_QUERY_REGISTRY_TABLE QueryTable,
                                      IN PVOID Context, IN PVOID Environment OPTIONAL) { return 0; }
NTSTATUS NTAPI RtlWriteRegistryValue(IN ULONG RelativeTo, IN PWSTR Path, IN PWSTR ValueName,
                                     IN ULONG ValueType, IN PVOID ValueData,
                                     IN ULONG ValueLength) { return 0; }
