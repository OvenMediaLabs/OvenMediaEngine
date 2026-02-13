#pragma once

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/ovlibrary/ovlibrary.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
};

namespace ffmpeg
{
	class ImageReader
	{
	public:
		static std::shared_ptr<ImageReader> Create(const ov::String filename);

	public:
		ImageReader(const ov::String filename);
		~ImageReader();

		bool Read();
		bool Resize(int32_t output_width, int32_t output_height, cmn::VideoPixelFormatId output_format = cmn::VideoPixelFormatId::YUV420P);

	public:
		AVFrame* GetOriginalFrame() const
		{
			return _original_frame;
		}

		int32_t GetOriginalWidth() const
		{
			return _original_frame ? _original_frame->width : 0;
		}

		int32_t GetOriginalHeight() const
		{
			return _original_frame ? _original_frame->height : 0;
		}

	public:
		AVFrame* GetOutputFrame() const
		{
			return _output_frame;
		}

		int32_t GetOutputWidth() const
		{
			return _output_frame ? _output_frame->width : 0;
		}

		int32_t GetOutputHeight() const
		{
			return _output_frame ? _output_frame->height : 0;
		}

		uint8_t* GetOutputData(int index) const
		{
			if (index < 0 || index >= AV_NUM_DATA_POINTERS)
			{
				return nullptr;
			}

			return _output_frame ? _output_frame->data[index] : nullptr;
		}

		int32_t GetOutputStride(int32_t index) const
		{
			if (index < 0 || index >= AV_NUM_DATA_POINTERS)
			{
				return 0;
			}

			return _output_frame ? _output_frame->linesize[index] : 0;
		}

		cmn::VideoPixelFormatId GetOutputFormat() const
		{
			return _output_format;
		}

		uint8_t* GetOpacityData() const
		{
			return _opacity_frame;
		}

	private:
		bool Open(const ov::String& filename);
		void Cleanup();

		ov::String _filename;
		AVFormatContext* _format_context;
		AVCodecContext* _codec_context;
		int32_t _video_stream_index = -1;

		cmn::VideoPixelFormatId _output_format;
		int32_t _output_width;
		int32_t _output_height;

		AVFrame* _original_frame = nullptr;
		AVFrame* _output_frame	 = nullptr;
		uint8_t* _opacity_frame	 = nullptr;

	private:
		static AVFrame* Convert(AVFrame* frame, cmn::VideoPixelFormatId output_format, int32_t output_width, int32_t output_height);
	};
}  // namespace ffmpeg