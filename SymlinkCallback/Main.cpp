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
    _Out_ PHANDLE             pHandle,
    _In_ ACCESS_MASK          DesiredAccess,
    _In_ POBJECT_ATTRIBUTES   ObjectAttributes,
    _In_ PUNICODE_STRING      DestinationName
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
SymLinkCallback(
    _In_ POBJECT_SYMBOLIC_LINK Symlink,
    _In_ PVOID SymlinkContext,
    _In_ PUNICODE_STRING SymlinkPath,
    _Outptr_ PVOID* Object
);

extern POBJECT_TYPE *IoDeviceObjectType;

EXTERN_C_END

UNICODE_STRING origStr;
POBJECT_SYMBOLIC_LINK symlinkObj;
POBJECT_SYMBOLIC_LINK fakeSymlinkObj;

_Use_decl_annotations_
VOID
DriverUnload (
    PDRIVER_OBJECT DriverObject
)
{
    UNREFERENCED_PARAMETER(DriverObject);

    symlinkObj->LinkTarget = origStr;
    symlinkObj->Flags &= ~0x10;

    ObDereferenceObject(symlinkObj);
    ObDereferenceObject(fakeSymlinkObj);
}

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
    HANDLE fakeSymlinkHandle;
    UNICODE_STRING symlinkName = RTL_CONSTANT_STRING(L"\\GLOBAL??\\c:");
    UNICODE_STRING symlinkFakeName = RTL_CONSTANT_STRING(L"\\AAA");
    OBJECT_ATTRIBUTES objAttr = RTL_CONSTANT_OBJECT_ATTRIBUTES(&symlinkName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE);
    OBJECT_ATTRIBUTES fakeSymlinkObjAttr = RTL_CONSTANT_OBJECT_ATTRIBUTES(&symlinkFakeName, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE);

    DriverObject->DriverUnload = DriverUnload;
    status = ZwOpenSymbolicLinkObject(&symLinkHandle, SYMBOLIC_LINK_ALL_ACCESS, &objAttr);

    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(77, 0, "Failed opening symbolic link with error: %x", status);
        goto Exit;
    }
    
    status = ObReferenceObjectByHandle(symLinkHandle, SYMBOLIC_LINK_ALL_ACCESS, NULL, KernelMode, (PVOID*)&symlinkObj, NULL);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(77, 0, "Failed referencing symbolic link with error: %x", status);
        goto Exit;
    }

    status = ZwCreateSymbolicLinkObject(&fakeSymlinkHandle, SYMBOLIC_LINK_ALL_ACCESS, &fakeSymlinkObjAttr, &symlinkObj->LinkTarget);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(77, 0, "Failed creating fake symbolic link with error: %x", status);
        goto Exit;
    }

    status = ObReferenceObjectByHandle(fakeSymlinkHandle, SYMBOLIC_LINK_ALL_ACCESS, NULL, KernelMode, (PVOID*)&fakeSymlinkObj, NULL);
    ObCloseHandle(&fakeSymlinkHandle, KernelMode);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(77, 0, "Failed referencing fake symlink with error: %x", status);
        goto Exit;
    }
    
    origStr = symlinkObj->LinkTarget;

    symlinkObj->Callback = SymLinkCallback;
    symlinkObj->CallbackContext = fakeSymlinkObj;
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
    _In_ PUNICODE_STRING SymlinkPath,
    _Outptr_ PVOID* Object)
{
    UNREFERENCED_PARAMETER(Symlink);
    UNREFERENCED_PARAMETER(SymlinkPath);

    *Object = SymlinkContext;
    return STATUS_REPARSE;
}