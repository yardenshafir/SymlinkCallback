#include <ntifs.h>
#include <wdm.h>

#pragma warning (disable: 4201)

EXTERN_C_START

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD DriverUnload;

typedef struct _OBJECT_SYMBOLIC_LINK
{
    /* 0x0000 */ union _LARGE_INTEGER CreationTime;
    union
    {
        /* 0x0008 */ struct _UNICODE_STRING LinkTarget;
        struct
        {
            /* 0x0008 */ void* Callback /* function */;
            /* 0x0010 */ void* CallbackContext;
        }; /* size: 0x0010 */
    }; /* size: 0x0010 */
    /* 0x0018 */ unsigned long DosDeviceDriveIndex;
    /* 0x001c */ unsigned long Flags;
    /* 0x0020 */ unsigned long AccessMask;
    /* 0x0024 */ long __PADDING__[1];
} OBJECT_SYMBOLIC_LINK, *POBJECT_SYMBOLIC_LINK; /* size: 0x0028 */

NTSYSAPI
NTSTATUS
NTAPI
ZwCreateSymbolicLinkObject (
    _Out_ PHANDLE pHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ PUNICODE_STRING DestinationName
);

NTSYSAPI 
NTSTATUS 
NTAPI 
ObReferenceObjectByName (
    PUNICODE_STRING ObjectName,
    ULONG Attributes,
    PACCESS_STATE AccessState,
    ACCESS_MASK DesiredAccess,
    POBJECT_TYPE ObjectType,
    KPROCESSOR_MODE AccessMode,
    PVOID ParseContext OPTIONAL,
    PVOID* Object
);

__declspec(code_seg(".call$1"))
NTSTATUS
SymLinkCallback (
    _In_ POBJECT_SYMBOLIC_LINK Symlink,
    _In_ PVOID SymlinkContext,
    _Out_ PUNICODE_STRING SymlinkPath,
    _Outptr_ PVOID* Object
);

extern POBJECT_TYPE *IoDeviceObjectType;

EXTERN_C_END

UNICODE_STRING origStr;
POBJECT_SYMBOLIC_LINK symlinkObj;

_Use_decl_annotations_
VOID
DriverUnload (
    PDRIVER_OBJECT DriverObject
)
{
    UNREFERENCED_PARAMETER(DriverObject);

    symlinkObj->Flags &= ~0x10;
    symlinkObj->LinkTarget = origStr;

    ObDereferenceObject(symlinkObj);
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
#pragma section(".call$0",write)
__declspec(allocate(".call$0")) UCHAR buffer[0xb000] = { 0 };

#pragma code_seg(".text")

_Use_decl_annotations_
NTSTATUS
DriverEntry (
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath
)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    NTSTATUS status;
    HANDLE symLinkHandle = NULL;
    UNICODE_STRING symlinkName = RTL_CONSTANT_STRING(L"\\GLOBAL??\\c:");
    OBJECT_ATTRIBUTES objAttr = RTL_CONSTANT_OBJECT_ATTRIBUTES(&symlinkName, 
                                                               OBJ_KERNEL_HANDLE | 
                                                               OBJ_CASE_INSENSITIVE);

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
        DbgPrintEx(77, 
                   0, 
                   "Failed opening symbolic link with error: %x", 
                   status);
        goto Exit;
    }

    //
    // Get the symbolic link object 
    //
    status = ObReferenceObjectByHandle(symLinkHandle, 
                                       SYMBOLIC_LINK_ALL_ACCESS, 
                                       NULL, 
                                       KernelMode, 
                                       (PVOID*)&symlinkObj, 
                                       NULL);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(77, 
                   0, 
                   "Failed referencing symbolic link with error: %x", 
                   status);
        goto Exit;
    }

    //
    // Save the original string that the symlink points to
    // so we can change the object back when we unload
    //
    origStr = symlinkObj->LinkTarget;

    //
    // Modify the symlink to point to our callback instead of the string
    // and change the flags so the union will be treated as a callback. 
    // Set CallbackContext to the original string so we can 
    // return it from the callback and allow the system to run normally. 
    //
    symlinkObj->Callback = SymLinkCallback;
    symlinkObj->CallbackContext = &origStr;
    symlinkObj->Flags |= 0x10;

Exit:
    return status;
}

#pragma section(".call$1",execute)

__declspec(code_seg(".call$1"))
NTSTATUS
SymLinkCallback (
    _In_ POBJECT_SYMBOLIC_LINK Symlink,
    _In_ PVOID SymlinkContext,
    _Out_ PUNICODE_STRING SymlinkPath,
    _Outptr_ PVOID* Object)
{
    UNREFERENCED_PARAMETER(Symlink);

    //
    // We need to either return the right object for this symlink
    // or the correct target string.
    // It's a lot easier to get the string, so we can set Object to Null. 
    //
    *Object = NULL;
    *SymlinkPath = *(PUNICODE_STRING)(SymlinkContext);  // OrigStr

    return STATUS_SUCCESS;
}
