@echo off
call T:\NTOS\vmmouse\ppcbuild\vcenv.bat
link -dump -exports %SystemRoot%\system32\ntoskrnl.exe > %SystemDrive%\TEMP\vmb-ppc\ntos-exp.txt
for %%f in (ExAllocatePool ExFreePool ExQueueWorkItem IoAttachDevice IoCallDriver IoCompleteRequest IoCreateDevice IoDeleteDevice KeAcquireSpinLock KeReleaseSpinLock KeAcquireSpinLockAtDpcLevel KeReleaseSpinLockFromDpcLevel KeCancelTimer KeInitializeDpc KeInitializeSpinLock KeInitializeTimer KeSetTimer RtlInitUnicodeString RtlQueryRegistryValues RtlWriteRegistryValue MmMapIoSpace MmUnmapIoSpace) do findstr /c:" %%f " %SystemDrive%\TEMP\vmb-ppc\ntos-exp.txt >nul && echo ok %%f || echo MISSING %%f
