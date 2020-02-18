#include <ntifs.h>
#include <wdm.h>
#include <ntstrsafe.h>

#pragma warning (disable: 4201)

#define OBJECT_SYMBOLIC_LINK_USE_CALLBACK 0x10

NTSTATUS
typedef (SYMLINK_CALLBACK_FUNCTION) (
    _In_ struct _OBJECT_SYMBOLIC_LINK* Symlink,
    _In_ PVOID SymlinkContext,
    _Out_ PUNICODE_STRING SymlinkPath,
    _Outptr_ PVOID* Object
);
typedef SYMLINK_CALLBACK_FUNCTION *PSYMLINK_CALLBACK_FUNCTION;

typedef struct _OBJECT_SYMBOLIC_LINK
{
    LARGE_INTEGER CreationTime;
    union
    {
        UNICODE_STRING LinkTarget;
        struct
        {
            PSYMLINK_CALLBACK_FUNCTION Callback;
            PVOID CallbackContext;
        };
    };
    ULONG DosDeviceDriveIndex;
    ULONG Flags;
    ACCESS_MASK AccessMask;
} OBJECT_SYMBOLIC_LINK, *POBJECT_SYMBOLIC_LINK;
C_ASSERT(sizeof(OBJECT_SYMBOLIC_LINK) == 0x28);

EXTERN_C_START
__declspec(code_seg(".call$1")) SYMLINK_CALLBACK_FUNCTION SymLinkCallback;
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;
DRIVER_DISPATCH SymHookCreate;
EXTERN_C_END

DECLARE_UNICODE_STRING_SIZE(g_DeviceName, 64);
UNICODE_STRING g_LinkPath;
POBJECT_SYMBOLIC_LINK g_SymLinkObject;
PDEVICE_OBJECT g_DeviceObject;

_Use_decl_annotations_
VOID
DriverUnload (
    PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);

    //
    // Undo the patch, restoring the original target string
    //
    g_SymLinkObject->Flags &= ~OBJECT_SYMBOLIC_LINK_USE_CALLBACK;
    MemoryBarrier();
    g_SymLinkObject->LinkTarget = g_LinkPath;

    //
    // Delete our device object
    //
    IoDeleteDevice(g_DeviceObject);

    //
    // Dereference the symbolic link object
    //
    ObDereferenceObject(g_SymLinkObject);
}

//
// To avoid a race condition when modifying the symlink object
// we are using a trick:
// We make sure our callback function is aligned to 64k so
// while the flags are not yet changed and the callback is still
// treated as a unicode string, the last 2 bytes are 0000 so the
// string length is 0, and the buffer is ignored.
// To achieve that, we create a section that contains a buffer
// sized 0xb000 and make sure our callback function is located after it,
// and so it is aligned to 64k.
//
#pragma section(".call$0", write)
__declspec(allocate(".call$0")) UCHAR _pad_[0xB000] = { 0 };

#pragma code_seg(".text")
NTSTATUS
SymHookCreate (
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp
    )
{
    PIO_STACK_LOCATION ioStack;
    PFILE_OBJECT fileObject;
    USHORT bufferLength;
    NTSTATUS status;
    PWCHAR buffer;
    UNREFERENCED_PARAMETER(DeviceObject);

    //
    // Get the FILE_OBJECT from the I/O Stack Location
    //
    ioStack = IoGetCurrentIrpStackLocation(Irp);
    fileObject = ioStack->FileObject;

    //
    // Print the file name being accessed
    //
    DbgPrintEx(DPFLTR_IHVDRIVER_ID,
               DPFLTR_ERROR_LEVEL,
               "Opening file %wZ\n",
               &fileObject->FileName);

    //
    // Allocate space for the original device name, plus the size of the
    // file name, and adding space for the terminating NUL.
    //
    bufferLength = fileObject->FileName.Length -
                   g_LinkPath.Length +
                   sizeof(UNICODE_NULL);
    buffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool, bufferLength, 'maNF');
    if (buffer == NULL)
    {
        status =  STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    //
    // Append the original device name first
    //
    buffer[0] = UNICODE_NULL;
    NT_VERIFY(NT_SUCCESS(RtlStringCbCatNW(buffer,
                                          bufferLength,
                                          g_LinkPath.Buffer,
                                          g_LinkPath.Length)));

    //
    // Then add the name of the file name
    //
    NT_VERIFY(NT_SUCCESS(RtlStringCbCatNW(buffer,
                                          bufferLength,
                                          fileObject->FileName.Buffer,
                                          fileObject->FileName.Length)));

    //
    // Ask the I/O manager to free the original file name and use ours instead
    //
    status = IoReplaceFileObjectName(fileObject,
                                     buffer,
                                     bufferLength - sizeof(UNICODE_NULL));
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Failed to swap file object name: %lx\n",
                   status);
        ExFreePool(buffer);
        goto Exit;
    }

    //
    // Return a reparse operation so that the I/O manager uses the new file
    // object name for its lookup, and starts over
    //
    Irp->IoStatus.Information = IO_REPARSE;
    status = STATUS_REPARSE;

Exit:
    //
    // Complete the IRP with the relevant status code
    //
    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

_Use_decl_annotations_
NTSTATUS
DriverEntry (
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;
    HANDLE symLinkHandle;
    DECLARE_CONST_UNICODE_STRING(symlinkName, L"\\GLOBAL??\\c:");
    OBJECT_ATTRIBUTES objAttr = RTL_CONSTANT_OBJECT_ATTRIBUTES(&symlinkName,
                                                               OBJ_KERNEL_HANDLE |
                                                               OBJ_CASE_INSENSITIVE);
    UNREFERENCED_PARAMETER(RegistryPath);

    //
    // Make sure our alignment trick worked out
    //
    if (((ULONG_PTR)SymLinkCallback & 0xFFFF) != 0)
    {
        status = STATUS_CONFLICTING_ADDRESSES;
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Callback function not aligned correctly!\n");
        goto Exit;
    }

    //
    // Set an unload routine so we can update during testing
    //
    DriverObject->DriverUnload = DriverUnload;

    //
    // Open a handle to the symbolic link object for C: directory,
    // so we can hook it
    //
    status = ZwOpenSymbolicLinkObject(&symLinkHandle,
                                      SYMBOLIC_LINK_ALL_ACCESS,
                                      &objAttr);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Failed opening symbolic link with error: %lx\n",
                   status);
        goto Exit;
    }

    //
    // Get the symbolic link object and close the handle since we
    // no longer need it
    //
    status = ObReferenceObjectByHandle(symLinkHandle,
                                       SYMBOLIC_LINK_ALL_ACCESS,
                                       NULL,
                                       KernelMode,
                                       (PVOID*)&g_SymLinkObject,
                                       NULL);
    ObCloseHandle(symLinkHandle, KernelMode);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Failed referencing symbolic link with error: %lx\n",
                   status);
        goto Exit;
    }

    //
    // Create our device object hook
    //
    RtlAppendUnicodeToString(&g_DeviceName, L"\\Device\\HarddiskVolume0");
    status = IoCreateDevice(DriverObject,
                            0,
                            &g_DeviceName,
                            FILE_DEVICE_UNKNOWN,
                            0,
                            FALSE,
                            &g_DeviceObject);
    if (!NT_SUCCESS(status))
    {
        //
        // Fail, and drop the symlink object reference
        //
        ObDereferenceObject(g_SymLinkObject);
        DbgPrintEx(DPFLTR_IHVDRIVER_ID,
                   DPFLTR_ERROR_LEVEL,
                   "Failed create devobj with error: %lx\n",
                   status);
        goto Exit;
    }

    //
    // Attach our create handler
    //
    DriverObject->MajorFunction[IRP_MJ_CREATE] = SymHookCreate;

    //
    // Save the original string that the symlink points to
    // so we can change the object back when we unload
    //
    g_LinkPath = g_SymLinkObject->LinkTarget;

    //
    // Modify the symlink to point to our callback instead of the string
    // and change the flags so the union will be treated as a callback.
    // Set CallbackContext to the original string so we can 
    // return it from the callback and allow the system to run normally.
    //
    g_SymLinkObject->Callback = SymLinkCallback;
    g_SymLinkObject->CallbackContext = &g_DeviceName;
    MemoryBarrier();
    g_SymLinkObject->Flags |= OBJECT_SYMBOLIC_LINK_USE_CALLBACK;

Exit:
    //
    // Return the result back to the system
    //
    return status;
}

#pragma section(".call$1", execute)
__declspec(code_seg(".call$1"))
NTSTATUS
SymLinkCallback (
    _In_ POBJECT_SYMBOLIC_LINK Symlink,
    _In_ PVOID SymlinkContext,
    _Out_ PUNICODE_STRING SymlinkPath,
    _Outptr_ PVOID* Object
    )
{
    UNREFERENCED_PARAMETER(Symlink);

    //
    // We need to either return the right object for this symlink
    // or the correct target string.
    // It's a lot easier to get the string, so we can set Object to Null. 
    //
    *Object = NULL;
    *SymlinkPath = *(PUNICODE_STRING)(SymlinkContext);
    return STATUS_SUCCESS;
}
