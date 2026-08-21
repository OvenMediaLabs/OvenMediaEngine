//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>

#include "sample.h"
#include "cenc.h"

namespace bmff
{
    class SampleBuffer
    {
    public:
        SampleBuffer(const std::shared_ptr<const MediaTrack> &media_track, const CencProperty &cenc_property);

        bool AppendSample(const std::shared_ptr<const MediaPacket> &media_packet);
        std::shared_ptr<Samples> GetSamples() const;
        // Detach exactly the first count samples for emission; the rest stay
        // buffered. A request that cannot be fulfilled exactly (empty buffer,
        // count of 0, or more than is buffered) returns nullptr, never a part.
        std::shared_ptr<Samples> PopFront(size_t count);
        void Reset();

    private:
        std::shared_ptr<Samples> _samples = nullptr;

        std::shared_ptr<const MediaTrack> _media_track = nullptr;
        std::shared_ptr<Encryptor> _encryptor = nullptr;

		[[maybe_unused]] double _duration = 0;
    };
}
