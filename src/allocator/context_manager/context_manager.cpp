/*
 *   BSD LICENSE
 *   Copyright (c) 2021 Samsung Electronics Corporation
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "src/allocator/context_manager/context_manager.h"

#include <string>
#include <vector>

#include "src/allocator/context_manager/allocator_ctx/allocator_ctx.h"
#include "src/allocator/context_manager/context_replayer.h"
#include "src/allocator/context_manager/file_io_manager.h"
#include "src/allocator/context_manager/gc_ctx/gc_ctx.h"
#include "src/allocator/context_manager/io_ctx/allocator_io_ctx.h"
#include "src/allocator/context_manager/rebuild_ctx/rebuild_ctx.h"
#include "src/allocator/context_manager/segment_ctx/segment_ctx.h"
#include "src/allocator/context_manager/wbstripe_ctx/wbstripe_ctx.h"
#include "src/allocator/include/allocator_const.h"
#include "src/event_scheduler/event_scheduler.h"
#include "src/logger/logger.h"
#include "src/telemetry/telemetry_client/telemetry_publisher.h"
#include "src/qos/qos_manager.h"

namespace pos
{
ContextManager::ContextManager(TelemetryPublisher* tp, AllocatorCtx* allocCtx_, SegmentCtx* segCtx_, RebuildCtx* rebuildCtx_,
    WbStripeCtx* wbstripeCtx_, AllocatorFileIoManager* fileManager_,
    ContextReplayer* ctxReplayer_, bool flushProgress, AllocatorAddressInfo* info_, std::string arrayName_)
: numAsyncIoIssued(0),
  flushInProgress(false),
  addrInfo(info_),
  curGcMode(MODE_NO_GC),
  prevGcMode(MODE_NO_GC),
  arrayName(arrayName_)
{
    // for UT
    allocatorCtx = allocCtx_;
    segmentCtx = segCtx_;
    rebuildCtx = rebuildCtx_;
    wbStripeCtx = wbstripeCtx_;
    fileIoManager = fileManager_;
    contextReplayer = ctxReplayer_;
    fileOwner[SEGMENT_CTX] = segCtx_;
    fileOwner[ALLOCATOR_CTX] = allocCtx_;
    fileOwner[REBUILD_CTX] = rebuildCtx_;
    flushInProgress = flushProgress;
    telPublisher = tp;
}

ContextManager::ContextManager(TelemetryPublisher* tp, AllocatorAddressInfo* info, std::string arrayName)
: ContextManager(tp, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, false, info, arrayName)
{
    allocatorCtx = new AllocatorCtx(info);
    segmentCtx = new SegmentCtx(info);
    rebuildCtx = new RebuildCtx(allocatorCtx, info);
    wbStripeCtx = new WbStripeCtx(info);
    fileIoManager = new AllocatorFileIoManager(fileNames, info, arrayName);
    contextReplayer = new ContextReplayer(allocatorCtx, segmentCtx, wbStripeCtx, info);
    fileOwner[SEGMENT_CTX] = segmentCtx;
    fileOwner[ALLOCATOR_CTX] = allocatorCtx;
    fileOwner[REBUILD_CTX] = rebuildCtx;
}

ContextManager::~ContextManager(void)
{
    delete segmentCtx;
    delete rebuildCtx;
    delete wbStripeCtx;
    delete allocatorCtx;
    delete fileIoManager;
    delete contextReplayer;
}

void
ContextManager::Init(void)
{
    wbStripeCtx->Init();
    allocatorCtx->Init();
    segmentCtx->Init();
    rebuildCtx->Init();
    fileIoManager->Init();
    _UpdateSectionInfo();
    int ret = _LoadContexts();
    assert(ret >= 0);
}

void
ContextManager::UpdateOccupiedStripeCount(StripeId lsid)
{
    SegmentId segId = lsid / addrInfo->GetstripesPerSegment();
    if (segmentCtx->IncreaseOccupiedStripeCount(segId) == (int)addrInfo->GetstripesPerSegment())
    {
        std::lock_guard<std::mutex> lock(allocatorCtx->GetSegStateLock(segId));
        if (segmentCtx->GetValidBlockCount(segId) == 0)
        {
            SegmentState eState = allocatorCtx->GetSegmentState(segId, false);
            if (eState != SegmentState::FREE)
            {
                _FreeSegment(segId);
            }
        }
        else
        {
            allocatorCtx->SetSegmentState(segId, SegmentState::SSD, false);
        }
    }
}

void
ContextManager::FreeUserDataSegment(SegmentId segId)
{
    std::lock_guard<std::mutex> lock(allocatorCtx->GetSegStateLock(segId));
    SegmentState eState = allocatorCtx->GetSegmentState(segId, false);
    if ((eState == SegmentState::SSD) || (eState == SegmentState::VICTIM))
    {
        assert(segmentCtx->GetOccupiedStripeCount(segId) == (int)addrInfo->GetstripesPerSegment());
        _FreeSegment(segId);
    }
}

void
ContextManager::Close(void)
{
    segmentCtx->Close();
    wbStripeCtx->Close();
    rebuildCtx->Close();
    allocatorCtx->Close();
    fileIoManager->Close();
}

int
ContextManager::FlushContextsSync(void)
{
    int ret = 0;
    for (int owner = 0; owner < NUM_ALLOCATOR_FILES; owner++)
    {
        ret = _FlushSync(owner);
        if (ret != 0)
        {
            break;
        }
    }
    return ret;
}

int
ContextManager::FlushContextsAsync(EventSmartPtr callback)
{
    if (flushInProgress.exchange(true) == true)
    {
        return (int)POS_EVENT_ID::ALLOCATOR_META_ARCHIVE_FLUSH_IN_PROGRESS;
    }
    int ret = 0;
    assert(numAsyncIoIssued == 0);
    numAsyncIoIssued = NUM_ALLOCATOR_FILES; // Issue 2 contexts(segmentctx, allocatorctx)
    telPublisher->PublishData(TEL_ALLOCATOR_ALLOCATORCTX_PENDING_IO_COUNT, numAsyncIoIssued);
    for (int owner = 0; owner < NUM_ALLOCATOR_FILES; owner++)
    {
        ret = _FlushAsync(owner, callback);
        if (ret != 0)
        {
            break;
        }
    }
    return ret;
}

SegmentId
ContextManager::AllocateFreeSegment(void)
{
    SegmentId segId = allocatorCtx->AllocateFreeSegment(UNMAP_SEGMENT /*scan free segment from last allocated segId*/);
    while ((segId != UNMAP_SEGMENT) && (rebuildCtx->IsRebuildTargetSegment(segId) == true))
    {
        POS_TRACE_DEBUG(EID(ALLOCATOR_REBUILDING_SEGMENT), "segmentId:{} is already rebuild target!", segId);
        allocatorCtx->ReleaseSegment(segId);
        ++segId;
        segId = allocatorCtx->AllocateFreeSegment(segId);
    }

    int freeSegCount = allocatorCtx->GetNumOfFreeSegmentWoLock();
    if (segId == UNMAP_SEGMENT)
    {
        POS_TRACE_ERROR(EID(ALLOCATOR_NO_FREE_SEGMENT), "Failed to allocate segment, free segment count:{}", freeSegCount);
    }
    else
    {
        POS_TRACE_INFO(EID(ALLOCATOR_START), "segmentId:{} @AllocateUserDataSegmentId, free segment count:{}", segId, freeSegCount);
    }
    telPublisher->PublishData(TEL_ALLOCATOR_FREE_SEGMENT_COUNT, freeSegCount);
    return segId;
}

SegmentId
ContextManager::AllocateGCVictimSegment(void)
{
    uint32_t numUserAreaSegments = addrInfo->GetnumUserAreaSegments();
    SegmentId victimSegment = UNMAP_SEGMENT;
    uint32_t minValidCount = addrInfo->GetblksPerSegment();
    for (SegmentId segId = 0; segId < numUserAreaSegments; ++segId)
    {
        uint32_t cnt = segmentCtx->GetValidBlockCount(segId);
        std::lock_guard<std::mutex> lock(allocatorCtx->GetSegStateLock(segId));
        if ((allocatorCtx->GetSegmentState(segId, false) != SegmentState::SSD) || (cnt == 0))
        {
            continue;
        }

        if (cnt < minValidCount)
        {
            victimSegment = segId;
            minValidCount = cnt;
        }
    }
    if (victimSegment != UNMAP_SEGMENT)
    {
        allocatorCtx->SetSegmentState(victimSegment, SegmentState::VICTIM, true);
        POS_TRACE_INFO(EID(ALLOCATE_GC_VICTIM), "segmentId:{} @AllocateGCVictim, free segment count:{}", victimSegment, allocatorCtx->GetNumOfFreeSegmentWoLock());
        telPublisher->PublishData(TEL_ALLOCATOR_GCVICTIM_SEGMENT, victimSegment);
    }
    return victimSegment;
}

GcMode
ContextManager::GetCurrentGcMode(void)
{
    int numFreeSegments = allocatorCtx->GetNumOfFreeSegmentWoLock();
    QosManagerSingleton::Instance()->SetGcFreeSegment(numFreeSegments);
    prevGcMode = curGcMode;
    curGcMode = gcCtx.GetCurrentGcMode(numFreeSegments);
    if (prevGcMode != curGcMode)
    {
        telPublisher->PublishData(TEL_ALLOCATOR_GCMODE, curGcMode);
    }
    return curGcMode;
}

int
ContextManager::GetGcThreshold(GcMode mode)
{
    return mode == MODE_NORMAL_GC ? gcCtx.GetNormalGcThreshold() : gcCtx.GetUrgentThreshold();
}

int
ContextManager::GetNumOfFreeSegment(bool needLock)
{
    if (needLock == true)
    {
        return allocatorCtx->GetNumOfFreeSegment();
    }
    else
    {
        return allocatorCtx->GetNumOfFreeSegmentWoLock();
    }
}

int
ContextManager::SetNextSsdLsid(void)
{
    std::unique_lock<std::mutex> lock(allocatorCtx->GetAllocatorCtxLock());
    POS_TRACE_INFO(EID(ALLOCATOR_MAKE_REBUILD_TARGET), "@SetNextSsdLsid");
    SegmentId segId = AllocateFreeSegment();
    if (segId == UNMAP_SEGMENT)
    {
        POS_TRACE_ERROR(EID(ALLOCATOR_NO_FREE_SEGMENT), "Free segmentId exhausted");
        return -EID(ALLOCATOR_NO_FREE_SEGMENT);
    }
    allocatorCtx->SetNextSsdLsid(segId);
    return 0;
}

uint64_t
ContextManager::GetStoredContextVersion(int owner)
{
    return fileOwner[owner]->GetStoredVersion();
}

SegmentId
ContextManager::AllocateRebuildTargetSegment(void)
{
    return rebuildCtx->GetRebuildTargetSegment();
}

bool
ContextManager::NeedRebuildAgain(void)
{
    return rebuildCtx->NeedRebuildAgain();
}

int
ContextManager::ReleaseRebuildSegment(SegmentId segmentId)
{
    int ret = rebuildCtx->ReleaseRebuildSegment(segmentId);
    if (ret == 1) // need to flush
    {
        _FlushAsync(REBUILD_CTX, nullptr);
        ret = 0;
    }
    return ret;
}

int
ContextManager::MakeRebuildTarget(void)
{
    int ret = rebuildCtx->MakeRebuildTarget();
    if (ret == 1) // need to flush
    {
        _FlushAsync(REBUILD_CTX, nullptr);
        ret = rebuildCtx->GetRebuildTargetSegmentsCount();
    }
    return ret;
}

int
ContextManager::StopRebuilding(void)
{
    std::unique_lock<std::mutex> lock(ctxLock);
    POS_TRACE_INFO(EID(ALLOCATOR_START), "@StopRebuilding");
    int ret = rebuildCtx->StopRebuilding();
    if (ret == 1) // need to flush
    {
        _FlushAsync(REBUILD_CTX, nullptr);
        ret = 0;
    }
    return ret;
}

char*
ContextManager::GetContextSectionAddr(int owner, int section)
{
    return fileIoManager->GetSectionAddr(owner, section);
}

int
ContextManager::GetContextSectionSize(int owner, int section)
{
    return fileIoManager->GetSectionSize(owner, section);
}

void
ContextManager::TestCallbackFunc(AsyncMetaFileIoCtx* ctx, int numIssuedIo)
{
    // only for UT
    numAsyncIoIssued = numIssuedIo;
    _FlushCompletedThenCB(ctx);
}

void
ContextManager::SetCallbackFunc(EventSmartPtr callback)
{
    flushCallback = callback;
}
//----------------------------------------------------------------------------//
void
ContextManager::_FreeSegment(SegmentId segId)
{
    segmentCtx->SetOccupiedStripeCount(segId, 0 /* count */);
    allocatorCtx->SetSegmentState(segId, SegmentState::FREE, false);
    allocatorCtx->ReleaseSegment(segId);
    int freeSegCount = allocatorCtx->GetNumOfFreeSegmentWoLock();
    telPublisher->PublishData(TEL_ALLOCATOR_FREE_SEGMENT_COUNT, freeSegCount);
    POS_TRACE_INFO(EID(ALLOCATOR_SEGMENT_FREED), "segmentId:{} was freed by allocator, free segment count:{}", segId, freeSegCount);
    int ret = rebuildCtx->FreeSegmentInRebuildTarget(segId);
    if (ret == 1)
    {
        _FlushAsync(REBUILD_CTX, nullptr);
    }
}

void
ContextManager::_FlushCompletedThenCB(AsyncMetaFileIoCtx* ctx)
{
    CtxHeader* header = reinterpret_cast<CtxHeader*>(ctx->buffer);
    if (header->sig == RebuildCtx::SIG_REBUILD_CTX)
    {
        rebuildCtx->FinalizeIo(ctx);
        POS_TRACE_DEBUG(EID(ALLOCATOR_META_ARCHIVE_STORE), "Complete to store rebuildCtx file, pendingMetaIo:{}", numAsyncIoIssued);
        delete[] ctx->buffer;
        delete ctx;
        return;
    }

    if (header->sig == SegmentCtx::SIG_SEGMENT_CTX)
    {
        segmentCtx->FinalizeIo(reinterpret_cast<AllocatorIoCtx*>(ctx));
        POS_TRACE_DEBUG(EID(ALLOCATOR_META_ARCHIVE_STORE), "SegmentCtx file stored, version:{}, pendingMetaIo:{}", header->ctxVersion, numAsyncIoIssued);
    }
    else
    {
        allocatorCtx->FinalizeIo(reinterpret_cast<AllocatorIoCtx*>(ctx));
        POS_TRACE_DEBUG(EID(ALLOCATOR_META_ARCHIVE_STORE), "AllocatorCtx file stored, version:{}, pendingMetaIo:{}", header->ctxVersion, numAsyncIoIssued);
    }

    delete[] ctx->buffer;
    delete ctx;
    assert(numAsyncIoIssued > 0);
    numAsyncIoIssued--;
    telPublisher->PublishData(TEL_ALLOCATOR_ALLOCATORCTX_PENDING_IO_COUNT, numAsyncIoIssued);
    if (numAsyncIoIssued == 0)
    {
        POS_TRACE_DEBUG(EID(ALLOCATOR_META_ARCHIVE_STORE), "Complete to flush allocator files");
        flushInProgress = false;
        EventSchedulerSingleton::Instance()->EnqueueEvent(flushCallback);
    }
}

void
ContextManager::_UpdateSectionInfo()
{
    int currentOffset = 0;
    int section = 0;
    for (section = 0; section < NUM_SEGMENT_CTX_SECTION; section++) // segmentCtx file
    {
        int size = segmentCtx->GetSectionSize(section);
        fileIoManager->UpdateSectionInfo(SEGMENT_CTX, section, segmentCtx->GetSectionAddr(section), size, currentOffset);
        currentOffset += size;
    }

    currentOffset = 0;
    for (section = 0; section < NUM_ALLOCATION_INFO; section++) // allocatorCtx file
    {
        int size = allocatorCtx->GetSectionSize(section);
        fileIoManager->UpdateSectionInfo(ALLOCATOR_CTX, section, allocatorCtx->GetSectionAddr(section), size, currentOffset);
        currentOffset += size;
    }
    assert(section == AC_ALLOCATE_WBLSID_BITMAP);
    for (; section < NUM_ALLOCATOR_CTX_SECTION; section++) // wbstripeCtx in allocatorCtx file
    {
        int size = wbStripeCtx->GetSectionSize(section);
        fileIoManager->UpdateSectionInfo(ALLOCATOR_CTX, section, wbStripeCtx->GetSectionAddr(section), size, currentOffset);
        currentOffset += size;
    }

    currentOffset = 0;
    for (section = 0; section < NUM_REBUILD_CTX_SECTION; section++) // rebuildCtx file
    {
        int size = rebuildCtx->GetSectionSize(section);
        fileIoManager->UpdateSectionInfo(REBUILD_CTX, section, rebuildCtx->GetSectionAddr(section), size, currentOffset);
        currentOffset += size;
    }
}

int
ContextManager::_LoadContexts(void)
{
    int ret = 0;
    for (int owner = 0; owner < NUM_FILES; owner++)
    {
        int fileSize = fileIoManager->GetFileSize(owner);
        char* buf = new char[fileSize]();
        ret = fileIoManager->LoadSync(owner, buf);
        if (ret == 0) // case for creating new file
        {
            ret = _FlushSync(owner);
            if (ret < 0)
            {
                delete[] buf;
                break;
            }
        }
        else if (ret == 1) // case for file exists
        {
            fileIoManager->LoadSectionData(owner, buf);
            fileOwner[owner]->AfterLoad(buf);
            if (owner == ALLOCATOR_CTX)
            {
                wbStripeCtx->AfterLoad(buf);
            }
        }
        else
        {
            delete[] buf;
            break;
        }
        delete[] buf;
    }
    return ret;
}

int
ContextManager::_FlushSync(int owner)
{
    int size = fileIoManager->GetFileSize(owner);
    char* buf = new char[size]();
    _PrepareBuffer(owner, buf);
    int ret = fileIoManager->StoreSync(owner, buf);
    if (ret != 0)
    {
        POS_TRACE_ERROR(EID(FAILED_TO_ISSUE_ASYNC_METAIO), "Failed to issue AsyncMetaIo:{} owner:{}", ret, owner);
        ret = -1;
    }
    delete[] buf;
    return ret;
}

int
ContextManager::_FlushAsync(int owner, EventSmartPtr callbackEvent)
{
    int size = fileIoManager->GetFileSize(owner);
    char* buf = new char[size]();
    _PrepareBuffer(owner, buf);
    if (callbackEvent != nullptr)
    {
        flushCallback = callbackEvent;
    }
    int ret = fileIoManager->StoreAsync(owner, buf, std::bind(&ContextManager::_FlushCompletedThenCB, this, std::placeholders::_1));
    if (ret != 0)
    {
        POS_TRACE_ERROR(EID(FAILED_TO_ISSUE_ASYNC_METAIO), "Failed to issue AsyncMetaIo:{} owner:{}", ret, owner);
        delete[] buf;
        ret = -1;
    }
    return ret;
}

void
ContextManager::_PrepareBuffer(int owner, char* buf)
{
    fileOwner[owner]->BeforeFlush(0 /*all Header*/, buf); // segmentCtx, allocatorCtx, rebuildCtx
    if (owner == SEGMENT_CTX)
    {
        std::lock_guard<std::mutex> lock(segmentCtx->GetSegmentCtxLock());
        fileIoManager->CopySectionData(owner, buf, 0, NUM_SEGMENT_CTX_SECTION);
    }
    else if (owner == ALLOCATOR_CTX)
    {
        { // lock boundary
            std::lock_guard<std::mutex> lock(allocatorCtx->GetAllocatorCtxLock());
            fileIoManager->CopySectionData(owner, buf, 0, NUM_ALLOCATION_INFO);
        }
        { // lock boundary
            std::lock_guard<std::mutex> lock(wbStripeCtx->GetAllocWbLsidBitmapLock());
            wbStripeCtx->BeforeFlush(AC_HEADER, buf);
            wbStripeCtx->BeforeFlush(AC_ALLOCATE_WBLSID_BITMAP, buf + fileIoManager->GetSectionOffset(owner, AC_ALLOCATE_WBLSID_BITMAP));
        }
        wbStripeCtx->BeforeFlush(AC_ACTIVE_STRIPE_TAIL, buf + fileIoManager->GetSectionOffset(owner, AC_ACTIVE_STRIPE_TAIL));
    }
}

} // namespace pos
