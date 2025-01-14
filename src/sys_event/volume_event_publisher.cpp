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
 *     * Neither the name of Samsung Electronics Corporation nor the names of
 *       its contributors may be used to endorse or promote products derived
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

#include "volume_event_publisher.h"

#include <iostream>
#include <utility>

#include "src/include/pos_event_id.h"
#include "src/logger/logger.h"

namespace pos
{
VolumeEventPublisher::VolumeEventPublisher()
{
}

VolumeEventPublisher::~VolumeEventPublisher()
{
    subscribers.clear();
}

void
VolumeEventPublisher::RegisterSubscriber(VolumeEvent* subscriber, std::string arrayName, int arrayId)
{
    subscribers.push_back(std::pair<int, VolumeEvent*>(arrayId, subscriber));
    POS_TRACE_INFO(EID(VOLUME_EVENT),
        "VolumeEvent subscriber {} is registered", subscriber->Tag());
}

void
VolumeEventPublisher::RemoveSubscriber(VolumeEvent* subscriber, std::string arrayName, int arrayId)
{
    for (auto it = subscribers.begin(); it != subscribers.end(); ++it)
    {
        if (it->first == arrayId)
        {
            if (it->second == subscriber)
            {
                POS_TRACE_INFO(EID(VOLUME_EVENT),
                    "VolumeEvent subscriber {} is removed", it->second->Tag());
                subscribers.erase(it);
                break;
            }
        }
    }
}

bool
VolumeEventPublisher::NotifyVolumeCreated(VolumeEventBase* volEventBase, VolumeEventPerf* volEventPerf, VolumeArrayInfo* volArrayInfo)
{
    POS_TRACE_INFO(EID(VOLUME_CREATE_EVENT),
        "NotifyVolumeCreated, # of subscribers: {}", subscribers.size());

    bool ret = true;
    int curPrgress = 1;

    for (auto it = subscribers.rbegin(); it != subscribers.rend(); ++it)
    {
        if (it->first == volArrayInfo->arrayId)
        {
            POS_TRACE_INFO(EID(VOLUME_CREATE_EVENT),
                "NotifyVolumeCreated to {} : {} {} {} {} {}",
                it->second->Tag(), volEventBase->volName, volEventBase->volId, volEventBase->volSizeByte,
                volEventPerf->maxiops, volEventPerf->maxbw);
            int res = it->second->VolumeCreated(volEventBase, volEventPerf, volArrayInfo);
            if (res != EID(VOL_EVENT_OK))
            {
                POS_TRACE_WARN(EID(VOLUME_CREATE_EVENT),
                    "Failure returned during volume event(CREATE) notification to {}, res:{}",
                    it->second->Tag(), res);
                ret = false;
            }

            POS_REPORT_TRACE(EID(VOLUME_CREATE_EVENT),
                "POS Volume {} Create Sequence In Progress({}/{}) : NotifyVolumeCreated to {} done, res: {}",
                volEventBase->volName, curPrgress, subscribers.size(), it->second->Tag(), res);

            curPrgress++;
        }        
    }

    return ret;
}

bool
VolumeEventPublisher::NotifyVolumeUpdated(VolumeEventBase* volEventBase, VolumeEventPerf* volEventPerf, VolumeArrayInfo* volArrayInfo)
{
    POS_TRACE_INFO(EID(VOLUME_UPDATE_EVENT),
        "NotifyVolumeUpdated, # of subscribers: {}", subscribers.size());

    bool ret = true;
    int curPrgress = 1;

    for (auto it = subscribers.begin(); it != subscribers.end(); ++it)
    {
        if (it->first == volArrayInfo->arrayId)
        {
            POS_TRACE_INFO(EID(VOLUME_UPDATE_EVENT),
                "NotifyVolumeUpdated to {} : {} {} {} {}",
                it->second->Tag(), volEventBase->volName, volEventBase->volId,
                volEventPerf->maxiops, volEventPerf->maxbw);
            int res = it->second->VolumeUpdated(volEventBase, volEventPerf, volArrayInfo);
            if (res != EID(VOL_EVENT_OK))
            {
                POS_TRACE_WARN(EID(VOLUME_UPDATE_EVENT),
                    "Failure returned during volume event(UPDATE) notification to {}, res:{}",
                    it->second->Tag(), res);
                ret = false;
            }

            POS_REPORT_TRACE(EID(VOLUME_UPDATE_EVENT),
                "POS Volume {} Update Sequence In Progress({}/{}) : NotifyVolumeUpdated to {} done, res: {}",
                volEventBase->volName, curPrgress, subscribers.size(), it->second->Tag(), res);

            curPrgress++;
        }
    }

    return ret;
}

bool
VolumeEventPublisher::NotifyVolumeDeleted(VolumeEventBase* volEventBase, VolumeArrayInfo* volArrayInfo)
{
    POS_TRACE_INFO(EID(VOLUME_DELETE_EVENT),
        "NotifyVolumeDeleted, # of subscribers: {}", subscribers.size());

    bool ret = true;
    int curPrgress = 1;

    for (auto it = subscribers.begin(); it != subscribers.end(); ++it)
    {
        if (it->first == volArrayInfo->arrayId)
        {
            POS_TRACE_INFO(EID(VOLUME_DELETE_EVENT),
                "NotifyVolumeDeleted to {} : {} {} {}",
                it->second->Tag(), volEventBase->volName, volEventBase->volId, volEventBase->volSizeByte);
            int res = it->second->VolumeDeleted(volEventBase, volArrayInfo);
            if (res != EID(VOL_EVENT_OK))
            {
                POS_TRACE_WARN(EID(VOLUME_DELETE_EVENT),
                    "Failure returned during volume event(DELETE) notification to {}, res:{}",
                    it->second->Tag(), res);
                ret = false;
            }

            POS_REPORT_TRACE(EID(VOLUME_DELETE_EVENT),
                "POS Volume {} Delete Sequence In Progress({}/{}) : NotifyVolumeDeleted to {} done, res: {}",
                volEventBase->volName, curPrgress, subscribers.size(), it->second->Tag(), res);

            curPrgress++;
        }
    }

    return ret;
}

bool
VolumeEventPublisher::NotifyVolumeMounted(VolumeEventBase* volEventBase, VolumeEventPerf* volEventPerf, VolumeArrayInfo* volArrayInfo)
{
    POS_TRACE_INFO(EID(VOLUME_MOUNT_EVENT),
        "NotifyVolumeMounted, # of subscribers: {}", subscribers.size());

    bool ret = true;
    int curPrgress = 1;

    for (auto it = subscribers.rbegin(); it != subscribers.rend(); ++it)
    {
        if (it->first == volArrayInfo->arrayId)
        {
            POS_TRACE_INFO(EID(VOLUME_MOUNT_EVENT),
                "NotifyVolumeMounted to {} : {} {} {} {} {} {}",
                it->second->Tag(), volEventBase->volName, volEventBase->subnqn, volEventBase->volId, volEventBase->volSizeByte,
                volEventPerf->maxiops, volEventPerf->maxbw);
            int res = it->second->VolumeMounted(volEventBase, volEventPerf, volArrayInfo);
            if (res != EID(VOL_EVENT_OK))
            {
                POS_TRACE_WARN(EID(VOLUME_MOUNT_EVENT),
                    "Failure returned during volume event(MOUNT) notification to {}, res:{}",
                    it->second->Tag(), res);
                ret = false;
            }
            else
            {
                POS_REPORT_TRACE(EID(VOLUME_MOUNT_EVENT),
                    "POS Volume {} Mount Sequence In Progress({}/{}) : NotifyVolumeMounted to {} done, res: {}",
                    volEventBase->volName, curPrgress, subscribers.size(), it->second->Tag(), res);

                curPrgress++;
            }
        }
    }

    return ret;
}

bool
VolumeEventPublisher::NotifyVolumeUnmounted(VolumeEventBase* volEventBase, VolumeArrayInfo* volArrayInfo)
{
    POS_TRACE_INFO(EID(VOLUME_UNMOUNT_EVENT),
        "NotifyVolumeUnmounted, # of subscribers: {}", subscribers.size());

    bool ret = true;
    int curPrgress = 1;

    for (auto it = subscribers.begin(); it != subscribers.end(); ++it)
    {
        if (it->first == volArrayInfo->arrayId)
        {
            POS_TRACE_INFO(EID(VOLUME_UNMOUNT_EVENT),
                "NotifyVolumeUnmounted to {} : {} {}",
                it->second->Tag(), volEventBase->volName, volEventBase->volId);
            int res = it->second->VolumeUnmounted(volEventBase, volArrayInfo);
            if (res != EID(VOL_EVENT_OK))
            {
                POS_TRACE_WARN(EID(VOLUME_UNMOUNT_EVENT),
                    "Failure returned during volume event(UNMOUNT) notification to {}, res:{}",
                    it->second->Tag(), res);
                ret = false;
            }

            POS_REPORT_TRACE(EID(VOLUME_UNMOUNT_EVENT),
                "POS Volume {} Unmount Sequence In Progress({}/{}) : NotifyVolumeUnmounted to {} done, res: {}",
                volEventBase->volName, curPrgress, subscribers.size(), it->second->Tag(), res);

            curPrgress++;
        }
    }

    return ret;
}

bool
VolumeEventPublisher::NotifyVolumeLoaded(VolumeEventBase* volEventBase, VolumeEventPerf* volEventPerf, VolumeArrayInfo* volArrayInfo)
{
    POS_TRACE_INFO(EID(VOLUME_LOAD_EVENT),
        "NotifyVolumeLoaded, # of subscribers: {}", subscribers.size());

    bool ret = true;
    int curPrgress = 1;

    for (auto it = subscribers.rbegin(); it != subscribers.rend(); ++it)
    {
        if (it->first == volArrayInfo->arrayId)
        {
            POS_TRACE_INFO(EID(VOLUME_LOAD_EVENT),
                "NotifyVolumeLoaded to {} : {} {} {} {} {}",
                it->second->Tag(), volEventBase->volName, volEventBase->volId, volEventBase->volSizeByte,
                volEventPerf->maxiops, volEventPerf->maxbw);
            int res = it->second->VolumeLoaded(volEventBase, volEventPerf, volArrayInfo);
            if (res != EID(VOL_EVENT_OK))
            {
                POS_TRACE_WARN(EID(VOLUME_LOAD_EVENT),
                    "Failure returned during volume event(LOAD) notification to {}, res:{}",
                    it->second->Tag(), res);
                ret = false;
            }

            POS_REPORT_TRACE(EID(VOLUME_LOAD_EVENT),
                "POS Array {}' volume load Sequence In Progress({}/{}) : NotifyVolumeLoaded to {} done, res: {}",
                volArrayInfo->arrayName, curPrgress, subscribers.size(), it->second->Tag(), res);
            
            curPrgress++;
        }
    }

    return ret;
}

void
VolumeEventPublisher::NotifyVolumeDetached(vector<int> volList, VolumeArrayInfo* volArrayInfo)
{
    POS_TRACE_INFO(EID(VOLUME_DETACH_EVENT),
        "NotifyVolumeDetached, # of subscribers: {}", subscribers.size());

    int curPrgress = 1;

    for (auto it = subscribers.begin(); it != subscribers.end(); ++it)
    {
        if (it->first == volArrayInfo->arrayId)
        {
            POS_TRACE_INFO(EID(VOLUME_DETACH_EVENT),
                "NotifyVolumeDetached to {}",
                it->second->Tag());
            it->second->VolumeDetached(volList, volArrayInfo);

            POS_REPORT_TRACE(EID(VOLUME_DETACH_EVENT),
                "POS Array {}'s all volume Detach Sequence In Progress({}/{}) : NotifyVolumeDetached to {} done",
                volArrayInfo->arrayName, curPrgress, subscribers.size(), it->second->Tag());

            curPrgress++;
        }
    }

}

} // namespace pos
