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

			if (_samples->AppendSample(cipher_sample) == false)
			{
				return false;
			}
		}

        return true;
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