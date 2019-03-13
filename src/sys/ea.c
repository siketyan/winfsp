/**
 * @file sys/ea.c
 *
 * @copyright 2015-2019 Bill Zissimopoulos
 */
/*
 * This file is part of WinFsp.
 *
 * You can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 3 as published by the Free Software
 * Foundation.
 *
 * Licensees holding a valid commercial license may use this software
 * in accordance with the commercial license agreement provided in
 * conjunction with the software.  The terms and conditions of any such
 * commercial license agreement shall govern, supersede, and render
 * ineffective any application of the GPLv3 license to this software,
 * notwithstanding of any reference thereto in the software or
 * associated repository.
 */

#include <sys/driver.h>

static VOID FspFsvolQueryEaGetCopy(
    BOOLEAN CasePreservedExtendedAttributes,
    BOOLEAN ReturnSingleEntry,
    PFILE_GET_EA_INFORMATION GetBufBgn, ULONG GetSize,
    PFILE_FULL_EA_INFORMATION SrcBufBgn, ULONG SrcSize,
    PFILE_FULL_EA_INFORMATION DstBufBgn, ULONG DstSize,
    PIO_STATUS_BLOCK IoStatus);
static VOID FspFsvolQueryEaIndexCopy(
    BOOLEAN CasePreservedExtendedAttributes,
    BOOLEAN ReturnSingleEntry,
    BOOLEAN IndexSpecified, PULONG PEaIndex,
    PFILE_FULL_EA_INFORMATION SrcBufBgn, ULONG SrcSize,
    PFILE_FULL_EA_INFORMATION DstBufBgn, ULONG DstSize,
    PIO_STATUS_BLOCK IoStatus);
static VOID FspFsvolQueryEaCopy(
    BOOLEAN CasePreservedExtendedAttributes,
    PIO_STACK_LOCATION IrpSp,
    PFILE_FULL_EA_INFORMATION SrcBufBgn, ULONG SrcSize,
    PFILE_FULL_EA_INFORMATION DstBufBgn, ULONG DstSize,
    PIO_STATUS_BLOCK IoStatus);
static NTSTATUS FspFsvolQueryEa(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp);
FSP_IOCMPL_DISPATCH FspFsvolQueryEaComplete;
static FSP_IOP_REQUEST_FINI FspFsvolQueryEaRequestFini;
static NTSTATUS FspFsvolSetEa(
    PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp);
FSP_IOCMPL_DISPATCH FspFsvolSetEaComplete;
static FSP_IOP_REQUEST_FINI FspFsvolSetEaRequestFini;
FSP_DRIVER_DISPATCH FspQueryEa;
FSP_DRIVER_DISPATCH FspSetEa;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, FspFsvolQueryEaGetCopy)
#pragma alloc_text(PAGE, FspFsvolQueryEaIndexCopy)
#pragma alloc_text(PAGE, FspFsvolQueryEa)
#pragma alloc_text(PAGE, FspFsvolQueryEaComplete)
#pragma alloc_text(PAGE, FspFsvolQueryEaRequestFini)
#pragma alloc_text(PAGE, FspFsvolSetEa)
#pragma alloc_text(PAGE, FspFsvolSetEaComplete)
#pragma alloc_text(PAGE, FspFsvolSetEaRequestFini)
#pragma alloc_text(PAGE, FspQueryEa)
#pragma alloc_text(PAGE, FspSetEa)
#endif

enum
{
    /* QueryEa */
    RequestFileNode                     = 0,
    RequestEaChangeNumber               = 1,

    /* SetEa */
    //RequestFileNode                   = 0,
};

static VOID FspFsvolQueryEaGetCopy(
    BOOLEAN CasePreservedExtendedAttributes,
    BOOLEAN ReturnSingleEntry,
    PFILE_GET_EA_INFORMATION GetBufBgn, ULONG GetSize,
    PFILE_FULL_EA_INFORMATION SrcBufBgn, ULONG SrcSize,
    PFILE_FULL_EA_INFORMATION DstBufBgn, ULONG DstSize,
    PIO_STATUS_BLOCK IoStatus)
{
    PAGED_CODE();

    PFILE_GET_EA_INFORMATION GetBuf, GetBufEnd = (PVOID)((PUINT8)GetBufBgn + GetSize);
    PFILE_GET_EA_INFORMATION Get;
    PFILE_FULL_EA_INFORMATION SrcBuf, SrcBufEnd = (PVOID)((PUINT8)SrcBufBgn + SrcSize);
    PFILE_FULL_EA_INFORMATION DstBuf, DstBufEnd = (PVOID)((PUINT8)DstBufBgn + DstSize);
    PFILE_FULL_EA_INFORMATION PrevDstBuf;
    PVOID Src;
    STRING GetName, Name;
    ULONG CopyLength;

    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = 0;
    DstBuf = DstBufBgn, PrevDstBuf = 0;
    for (GetBuf = GetBufBgn;
        GetBufEnd > GetBuf && 0 != GetBuf->NextEntryOffset;
        GetBuf = (PVOID)((PUINT8)GetBuf + GetBuf->NextEntryOffset))
    {
        GetName.Length = GetName.MaximumLength = GetBuf->EaNameLength;
        GetName.Buffer = GetBuf->EaName;

        /* ignore duplicate names */
        for (Get = GetBufBgn;
            GetBuf > Get;
            Get = (PVOID)((PUINT8)Get + Get->NextEntryOffset))
        {
            Name.Length = Name.MaximumLength = Get->EaNameLength;
            Name.Buffer = Get->EaName;

            if (RtlEqualString(&GetName, &Name, TRUE/* always case-insensitive */))
                break;
        }
        if (GetBuf > Get)
            continue;

        if (!FspEaNameIsValid(&GetName))
        {
            IoStatus->Status = STATUS_INVALID_EA_NAME;
            IoStatus->Information = (ULONG)((PUINT8)GetBuf - (PUINT8)GetBufBgn);
            break;
        }

        Src = GetBuf;
        for (SrcBuf = SrcBufBgn;
            SrcBufEnd > SrcBuf && 0 != SrcBuf->NextEntryOffset;
            SrcBuf = (PVOID)((PUINT8)SrcBuf + SrcBuf->NextEntryOffset))
        {
            Name.Length = Name.MaximumLength = SrcBuf->EaNameLength;
            Name.Buffer = SrcBuf->EaName;

            if (RtlEqualString(&GetName, &Name, TRUE/* always case-insensitive */))
            {
                Src = SrcBuf;
                break;
            }
        }

        if (GetBuf != Src)
            CopyLength = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) +
                ((PFILE_FULL_EA_INFORMATION)Src)->EaNameLength + 1 +
                ((PFILE_FULL_EA_INFORMATION)Src)->EaValueLength;
        else
            CopyLength = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) +
                ((PFILE_GET_EA_INFORMATION)Src)->EaNameLength + 1;

        if ((PUINT8)DstBuf + CopyLength > (PUINT8)DstBufEnd)
        {
            IoStatus->Status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        RtlMoveMemory(DstBuf, Src, CopyLength);
        DstBuf->NextEntryOffset = 0;
        if (!CasePreservedExtendedAttributes)
        {
            Name.Length = Name.MaximumLength = DstBuf->EaNameLength;
            Name.Buffer = DstBuf->EaName;
            FspEaNameUpcase(&Name, &Name, 0);
        }
        if (0 != PrevDstBuf)
            PrevDstBuf->NextEntryOffset = (ULONG)((PUINT8)DstBuf - (PUINT8)PrevDstBuf);
        PrevDstBuf = DstBuf;
        DstBuf = (PVOID)((PUINT8)DstBuf + CopyLength);

        if (ReturnSingleEntry)
            break;

        DstBuf = (PVOID)FSP_FSCTL_ALIGN_UP((UINT_PTR)DstBuf, sizeof(ULONG));
    }

    IoStatus->Information = NT_SUCCESS(IoStatus->Status) ?
        (ULONG)((PUINT8)DstBuf - (PUINT8)DstBufBgn) : 0;
}

static VOID FspFsvolQueryEaIndexCopy(
    BOOLEAN CasePreservedExtendedAttributes,
    BOOLEAN ReturnSingleEntry,
    BOOLEAN IndexSpecified, PULONG PEaIndex,
    PFILE_FULL_EA_INFORMATION SrcBufBgn, ULONG SrcSize,
    PFILE_FULL_EA_INFORMATION DstBufBgn, ULONG DstSize,
    PIO_STATUS_BLOCK IoStatus)
{
    PAGED_CODE();

    ULONG EaIndex = 1;
    PFILE_FULL_EA_INFORMATION SrcBuf, SrcBufEnd = (PVOID)((PUINT8)SrcBufBgn + SrcSize);
    PFILE_FULL_EA_INFORMATION DstBuf, DstBufEnd = (PVOID)((PUINT8)DstBufBgn + DstSize);
    PFILE_FULL_EA_INFORMATION PrevDstBuf;
    STRING Name;
    ULONG CopyLength;

    if (IndexSpecified && 0 == *PEaIndex)
    {
        IoStatus->Status = STATUS_NONEXISTENT_EA_ENTRY;
        IoStatus->Information = 0;
        return;
    }

    for (SrcBuf = SrcBufBgn;
        EaIndex < *PEaIndex &&
        SrcBufEnd > SrcBuf && 0 != SrcBuf->NextEntryOffset;
        SrcBuf = (PVOID)((PUINT8)SrcBuf + SrcBuf->NextEntryOffset), EaIndex++)
        ;

    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = 0;
    DstBuf = DstBufBgn, PrevDstBuf = 0;
    for (;
        SrcBufEnd > SrcBuf && 0 != SrcBuf->NextEntryOffset;
        SrcBuf = (PVOID)((PUINT8)SrcBuf + SrcBuf->NextEntryOffset), EaIndex++)
    {
        if ((PUINT8)DstBuf + FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) > (PUINT8)DstBufEnd)
            break;

        CopyLength = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) +
            ((PFILE_FULL_EA_INFORMATION)SrcBuf)->EaNameLength + 1 +
            ((PFILE_FULL_EA_INFORMATION)SrcBuf)->EaValueLength;

        if ((PUINT8)DstBuf + CopyLength > (PUINT8)DstBufEnd)
        {
            CopyLength = (ULONG)((PUINT8)DstBufEnd - (PUINT8)DstBuf);
            IoStatus->Status = STATUS_BUFFER_OVERFLOW;
        }

        RtlMoveMemory(DstBuf, SrcBuf, CopyLength);
        DstBuf->NextEntryOffset = 0;
        if (!CasePreservedExtendedAttributes)
        {
            Name.Length = Name.MaximumLength = DstBuf->EaNameLength;
            Name.Buffer = DstBuf->EaName;
            FspEaNameUpcase(&Name, &Name, 0);
        }
        if (0 != PrevDstBuf)
            PrevDstBuf->NextEntryOffset = (ULONG)((PUINT8)DstBuf - (PUINT8)PrevDstBuf);
        PrevDstBuf = DstBuf;
        DstBuf = (PVOID)((PUINT8)DstBuf + CopyLength);

        if (!NT_SUCCESS(IoStatus->Status) || ReturnSingleEntry)
            break;

        DstBuf = (PVOID)FSP_FSCTL_ALIGN_UP((UINT_PTR)DstBuf, sizeof(ULONG));
    }

    if (0 != PrevDstBuf)
    {
        *PEaIndex = EaIndex;
        IoStatus->Information = (ULONG)((PUINT8)DstBuf - (PUINT8)DstBufBgn);
    }
    else
    {
        if (SrcBufBgn == SrcBuf)
            IoStatus->Status = IndexSpecified ?
                STATUS_NONEXISTENT_EA_ENTRY : STATUS_NO_EAS_ON_FILE;
        else if (SrcBufEnd > SrcBuf && 0 != SrcBuf->NextEntryOffset)
            IoStatus->Status = STATUS_BUFFER_TOO_SMALL;
        else
            IoStatus->Status = IndexSpecified && *PEaIndex != EaIndex ?
                STATUS_NONEXISTENT_EA_ENTRY : STATUS_NO_MORE_EAS;
    }
}

static VOID FspFsvolQueryEaCopy(
    BOOLEAN CasePreservedExtendedAttributes,
    PIO_STACK_LOCATION IrpSp,
    PFILE_FULL_EA_INFORMATION SrcBufBgn, ULONG SrcSize,
    PFILE_FULL_EA_INFORMATION DstBufBgn, ULONG DstSize,
    PIO_STATUS_BLOCK IoStatus)
{
    PAGED_CODE();

    PFILE_OBJECT FileObject = IrpSp->FileObject;
    FSP_FILE_DESC *FileDesc = FileObject->FsContext2;
    BOOLEAN RestartScan = BooleanFlagOn(IrpSp->Flags, SL_RESTART_SCAN);
    BOOLEAN IndexSpecified = BooleanFlagOn(IrpSp->Flags, SL_INDEX_SPECIFIED);
    BOOLEAN ReturnSingleEntry = BooleanFlagOn(IrpSp->Flags, SL_RETURN_SINGLE_ENTRY);
    PFILE_GET_EA_INFORMATION EaList = IrpSp->Parameters.QueryEa.EaList;
    ULONG EaListLength = IrpSp->Parameters.QueryEa.EaListLength;
    ULONG EaIndex;

    if (0 != EaList)
    {
        FspFsvolQueryEaGetCopy(
            CasePreservedExtendedAttributes,
            ReturnSingleEntry,
            EaList, EaListLength,
            SrcBufBgn, SrcSize,
            DstBufBgn, DstSize,
            IoStatus);
    }
    else
    {
        if (IndexSpecified)
            EaIndex = IrpSp->Parameters.QueryEa.EaIndex;
        else if (RestartScan)
            EaIndex = 0;
        else
            EaIndex = FileDesc->EaIndex;
        FspFsvolQueryEaIndexCopy(
            CasePreservedExtendedAttributes,
            ReturnSingleEntry,
            IndexSpecified, &EaIndex,
            SrcBufBgn, SrcSize,
            DstBufBgn, DstSize,
            IoStatus);
        if (NT_SUCCESS(IoStatus->Status) || STATUS_BUFFER_OVERFLOW == IoStatus->Status)
            FileDesc->EaIndex = EaIndex;
    }
}

static NTSTATUS FspFsvolQueryEa(
    PDEVICE_OBJECT FsvolDeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
    PAGED_CODE();

    /* is this a valid FileObject? */
    if (!FspFileNodeIsValid(IrpSp->FileObject->FsContext))
        return STATUS_INVALID_DEVICE_REQUEST;

    NTSTATUS Result;
    FSP_FSVOL_DEVICE_EXTENSION *FsvolDeviceExtension = FspFsvolDeviceExtension(FsvolDeviceObject);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    FSP_FILE_NODE *FileNode = FileObject->FsContext;
    FSP_FILE_DESC *FileDesc = FileObject->FsContext2;
    PVOID Buffer = Irp->UserBuffer;
    ULONG Length = IrpSp->Parameters.QueryEa.Length;
    PVOID EaBuffer;
    ULONG EaBufferSize;
    FSP_FSCTL_TRANSACT_REQ *Request;

    ASSERT(FileNode == FileDesc->FileNode);

    FspFileNodeAcquireExclusive(FileNode, Main);
    if (FspFileNodeReferenceEa(FileNode, &EaBuffer, &EaBufferSize))
    {
        FspFsvolQueryEaCopy(
            !!FsvolDeviceExtension->VolumeParams.CasePreservedExtendedAttributes,
            IrpSp,
            EaBuffer, EaBufferSize,
            Buffer, Length,
            &Irp->IoStatus);
        FspFileNodeDereferenceEa(EaBuffer);
        FspFileNodeRelease(FileNode, Main);

        return Irp->IoStatus.Status;
    }

    FspFileNodeConvertExclusiveToShared(FileNode, Main);
    FspFileNodeAcquireShared(FileNode, Pgio);

    Result = FspBufferUserBuffer(Irp, Length, IoWriteAccess);
    if (!NT_SUCCESS(Result))
    {
        FspFileNodeRelease(FileNode, Full);
        return Result;
    }

    Result = FspIopCreateRequestEx(Irp, 0, 0, FspFsvolQueryEaRequestFini, &Request);
    if (!NT_SUCCESS(Result))
    {
        FspFileNodeRelease(FileNode, Full);
        return Result;
    }

    Request->Kind = FspFsctlTransactQueryEaKind;
    Request->Req.QueryEa.UserContext = FileNode->UserContext;
    Request->Req.QueryEa.UserContext2 = FileDesc->UserContext2;

    FspFileNodeSetOwner(FileNode, Full, Request);
    FspIopRequestContext(Request, RequestFileNode) = FileNode;

    return FSP_STATUS_IOQ_POST;
}

NTSTATUS FspFsvolQueryEaComplete(
    PIRP Irp, const FSP_FSCTL_TRANSACT_RSP *Response)
{
    FSP_ENTER_IOC(PAGED_CODE());

    if (!NT_SUCCESS(Response->IoStatus.Status))
    {
        Irp->IoStatus.Information = 0;
        Result = Response->IoStatus.Status;
        FSP_RETURN();
    }

    PDEVICE_OBJECT FsvolDeviceObject = IrpSp->DeviceObject;
    FSP_FSVOL_DEVICE_EXTENSION *FsvolDeviceExtension = FspFsvolDeviceExtension(FsvolDeviceObject);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    FSP_FILE_NODE *FileNode = FileObject->FsContext;
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG Length = IrpSp->Parameters.QueryEa.Length;
    PVOID EaBuffer = 0;
    ULONG EaBufferSize = 0;
    FSP_FSCTL_TRANSACT_REQ *Request = FspIrpRequest(Irp);
    BOOLEAN Success;

    if (0 != FspIopRequestContext(Request, RequestFileNode))
    {
        /* check that the EA buffer we got back is valid */
        if (Response->Buffer + Response->Rsp.QueryEa.Ea.Size >
            (PUINT8)Response + Response->Size)
        {
            Irp->IoStatus.Information = 0;
            Result = STATUS_EA_LIST_INCONSISTENT;
            FSP_RETURN();
        }
        Irp->IoStatus.Information = 0;
        Result = IoCheckEaBufferValidity((PVOID)Response->Buffer, Response->Rsp.QueryEa.Ea.Size,
            (PULONG)&Irp->IoStatus.Information);
        if (!NT_SUCCESS(Result))
            FSP_RETURN();

        FspIopRequestContext(Request, RequestEaChangeNumber) = (PVOID)
            FspFileNodeEaChangeNumber(FileNode);
        FspIopRequestContext(Request, RequestFileNode) = 0;

        FspFileNodeReleaseOwner(FileNode, Full, Request);
    }

    Success = DEBUGTEST(90) && FspFileNodeTryAcquireExclusive(FileNode, Main);
    if (!Success)
    {
        FspIopRetryCompleteIrp(Irp, Response, &Result);
        FSP_RETURN();
    }

    Success = !FspFileNodeTrySetEa(FileNode,
        Response->Buffer, Response->Rsp.QueryEa.Ea.Size,
        (ULONG)(UINT_PTR)FspIopRequestContext(Request, RequestEaChangeNumber));
    Success = Success && FspFileNodeReferenceEa(FileNode, &EaBuffer, &EaBufferSize);
    if (Success)
    {
        FspFsvolQueryEaCopy(
            !!FsvolDeviceExtension->VolumeParams.CasePreservedExtendedAttributes,
            IrpSp,
            EaBuffer, EaBufferSize,
            Buffer, Length,
            &Irp->IoStatus);
        FspFileNodeDereferenceEa(EaBuffer);
    }
    else
    {
        EaBuffer = (PVOID)Response->Buffer;
        EaBufferSize = Response->Rsp.QueryEa.Ea.Size;
        FspFsvolQueryEaCopy(
            !!FsvolDeviceExtension->VolumeParams.CasePreservedExtendedAttributes,
            IrpSp,
            EaBuffer, EaBufferSize,
            Buffer, Length,
            &Irp->IoStatus);
    }

    FspFileNodeRelease(FileNode, Main);

    FSP_LEAVE_IOC("FileObject=%p",
        IrpSp->FileObject);
}

static VOID FspFsvolQueryEaRequestFini(FSP_FSCTL_TRANSACT_REQ *Request, PVOID Context[4])
{
    PAGED_CODE();

    FSP_FILE_NODE *FileNode = Context[RequestFileNode];

    if (0 != FileNode)
        FspFileNodeReleaseOwner(FileNode, Full, Request);
}

static NTSTATUS FspFsvolSetEa(
    PDEVICE_OBJECT FsvolDeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
    PAGED_CODE();

    /* is this a valid FileObject? */
    if (!FspFileNodeIsValid(IrpSp->FileObject->FsContext))
        return STATUS_INVALID_DEVICE_REQUEST;

    NTSTATUS Result;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    FSP_FILE_NODE *FileNode = FileObject->FsContext;
    FSP_FILE_DESC *FileDesc = FileObject->FsContext2;
    PVOID Buffer;
    ULONG Length = IrpSp->Parameters.SetEa.Length;
    FSP_FSCTL_TRANSACT_REQ *Request;

    ASSERT(FileNode == FileDesc->FileNode);

    Result = FspBufferUserBuffer(Irp, Length, IoReadAccess);
    if (!NT_SUCCESS(Result))
        return Result;

    Buffer = Irp->AssociatedIrp.SystemBuffer;

    Irp->IoStatus.Information = 0;
    Result = IoCheckEaBufferValidity(Buffer, Length,
        (PULONG)&Irp->IoStatus.Information);
    if (!NT_SUCCESS(Result))
        return Result;

    FspFileNodeAcquireExclusive(FileNode, Full);

    Result = FspIopCreateRequestEx(Irp, 0, Length, FspFsvolSetEaRequestFini,
        &Request);
    if (!NT_SUCCESS(Result))
    {
        FspFileNodeRelease(FileNode, Full);
        return Result;
    }

    Request->Kind = FspFsctlTransactSetEaKind;
    Request->Req.SetEa.UserContext = FileNode->UserContext;
    Request->Req.SetEa.UserContext2 = FileDesc->UserContext2;
    Request->Req.SetEa.Ea.Offset = 0;
    Request->Req.SetEa.Ea.Size = (UINT16)Length;
    RtlCopyMemory(Request->Buffer, Buffer, Length);

    FspFileNodeSetOwner(FileNode, Full, Request);
    FspIopRequestContext(Request, RequestFileNode) = FileNode;

    return FSP_STATUS_IOQ_POST;
}

NTSTATUS FspFsvolSetEaComplete(
    PIRP Irp, const FSP_FSCTL_TRANSACT_RSP *Response)
{
    FSP_ENTER_IOC(PAGED_CODE());

    if (!NT_SUCCESS(Response->IoStatus.Status))
    {
        Irp->IoStatus.Information = 0;
        Result = Response->IoStatus.Status;
        FSP_RETURN();
    }

    PFILE_OBJECT FileObject = IrpSp->FileObject;
    FSP_FILE_NODE *FileNode = FileObject->FsContext;
    FSP_FSCTL_TRANSACT_REQ *Request = FspIrpRequest(Irp);
    BOOLEAN Valid;

    Valid = FALSE;
    if (0 < Response->Rsp.SetEa.Ea.Size &&
        Response->Buffer + Response->Rsp.SetEa.Ea.Size <=
            (PUINT8)Response + Response->Size)
    {
        Irp->IoStatus.Information = 0;
        Result = IoCheckEaBufferValidity((PVOID)Response->Buffer, Response->Rsp.QueryEa.Ea.Size,
            (PULONG)&Irp->IoStatus.Information);
        Valid = NT_SUCCESS(Result);
    }

    /* if the EA buffer that we got back is valid */
    if (Valid)
    {
        /* update the cached EA */
        FspFileNodeSetEa(FileNode,
            Response->Buffer, Response->Rsp.SetEa.Ea.Size);
    }
    else
    {
        /* invalidate the cached EA */
        FspFileNodeSetEa(FileNode, 0, 0);
    }

    FspFileNodeNotifyChange(FileNode, FILE_NOTIFY_CHANGE_EA, FILE_ACTION_MODIFIED, FALSE);

    FspIopRequestContext(Request, RequestFileNode) = 0;
    FspFileNodeReleaseOwner(FileNode, Full, Request);

    Irp->IoStatus.Information = 0;
    Result = STATUS_SUCCESS;

    FSP_LEAVE_IOC("FileObject=%p",
        IrpSp->FileObject);
}

static VOID FspFsvolSetEaRequestFini(FSP_FSCTL_TRANSACT_REQ *Request, PVOID Context[4])
{
    PAGED_CODE();

    FSP_FILE_NODE *FileNode = Context[RequestFileNode];

    if (0 != FileNode)
        FspFileNodeReleaseOwner(FileNode, Full, Request);
}

NTSTATUS FspQueryEa(
    PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    FSP_ENTER_MJ(PAGED_CODE());

    switch (FspDeviceExtension(DeviceObject)->Kind)
    {
    case FspFsvolDeviceExtensionKind:
        FSP_RETURN(Result = FspFsvolQueryEa(DeviceObject, Irp, IrpSp));
    default:
        FSP_RETURN(Result = STATUS_INVALID_DEVICE_REQUEST);
    }

    FSP_LEAVE_MJ("FileObject=%p",
        IrpSp->FileObject);
}

NTSTATUS FspSetEa(
    PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    FSP_ENTER_MJ(PAGED_CODE());

    switch (FspDeviceExtension(DeviceObject)->Kind)
    {
    case FspFsvolDeviceExtensionKind:
        FSP_RETURN(Result = FspFsvolSetEa(DeviceObject, Irp, IrpSp));
    default:
        FSP_RETURN(Result = STATUS_INVALID_DEVICE_REQUEST);
    }

    FSP_LEAVE_MJ("FileObject=%p",
        IrpSp->FileObject);
}
