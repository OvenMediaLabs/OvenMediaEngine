#include "bmff_private.h"
#include "sample_buffer.h"

namespace bmff
{
    SampleBuffer::SampleBuffer(const std::shared_ptr<const MediaTrack> &media_track, const CencProperty &cenc_property)
        :_media_track(media_track)
    {
		if (cenc_property.scheme != CencProtectScheme::None)
		{
			_encryptor = std::make_shared<Encryptor>(media_track, cenc_property);
		}
    }

    bool SampleBuffer::AppendSample(const std::shared_ptr<const MediaPacket> &media_packet)
    {
        if (_samples == nullptr)
        {
            _samples = std::make_shared<Samples>();
        }

		Sample sample(media_packet);

		double timescale = _media_track->GetTimeBase().GetTimescale();
		sample.timing.dts_us = TicksToUs(media_packet->GetDts(), timescale);
		sample.timing.pts_us = TicksToUs(media_packet->GetPts(), timescale);
		// The difference of two converted endpoints, so consecutive durations
		// telescope exactly to end minus start
		sample.timing.duration_us = TicksToUs(media_packet->GetDts() + media_packet->GetDuration(), timescale) - sample.timing.dts_us;
		sample.timing.independent = (media_packet->GetFlag() == MediaPacketFlag::Key) ||
									(_media_track->GetMediaType() == cmn::MediaType::Audio);

		if (_encryptor == nullptr)
		{
			if (_samples->AppendSample(sample) == false)
			{
				return false;
			}
		}
		else
		{
			Sample cipher_sample;
			if (_encryptor->Encrypt(sample, cipher_sample) == false)
			{
				return false;
			}

			cipher_sample.timing = sample.timing;

			if (_samples->AppendSample(cipher_sample) == false)
			{
				return false;
			}
		}

        return true;
    }

    std::shared_ptr<Samples> SampleBuffer::PopFront(size_t count)
    {
        if (_samples == nullptr || count == 0)
        {
            return nullptr;
        }

        // Asking for more than is buffered is a caller bug; fulfilling it
        // partially would silently emit a chunk shorter than the caller planned
        if (count > _samples->GetTotalCount())
        {
            OV_ASSERT2(false);
            return nullptr;
        }

        // Taking everything hands the whole object over without a copy
        if (count == _samples->GetTotalCount())
        {
            auto taken = _samples;
            _samples = nullptr;
            return taken;
        }

        return _samples->PopFront(count);
    }

    std::shared_ptr<Samples> SampleBuffer::GetSamples() const
    {
        return _samples;
    }

    void SampleBuffer::Reset()
    {
        _samples.reset();
    }
}