#include "image_reader.h"

#include <modules/ffmpeg/compat.h>

#define OV_LOG_TAG "FFmpegFrameReader"

namespace ffmpeg
{
	std::shared_ptr<ffmpeg::ImageReader> ImageReader::Create(const ov::String filename)
	{
		auto reader = std::make_shared<ffmpeg::ImageReader>(filename);
		return reader;
	}

	ImageReader::ImageReader(const ov::String filename)
		: _filename(filename),
		  _format_context(nullptr),
		  _codec_context(nullptr),
		  _video_stream_index(-1),
		  _output_format(cmn::VideoPixelFormatId::None),
		  _output_width(0),
		  _output_height(0),
		  _original_frame(nullptr),
		  _output_frame(nullptr),
		  _opacity_frame(nullptr)
	{
	}

	ImageReader::~ImageReader()
	{
		Cleanup();

		if (_original_frame)
		{
			av_frame_free(&_original_frame);
		}

		if (_output_frame)
		{
			av_frame_free(&_output_frame);
		}

		if (_opacity_frame)
		{
			av_free(_opacity_frame);
			_opacity_frame = nullptr;
		}
	}

	bool ImageReader::Open(const ov::String& filename)
	{
		if (avformat_open_input(&_format_context, filename.CStr(), nullptr, nullptr) != 0)
		{
			// Failed to open input file
			return false;
		}

		if (avformat_find_stream_info(_format_context, nullptr) < 0)
		{
			// Could not find stream info
			return false;
		}

		for (unsigned i = 0; i < _format_context->nb_streams; i++)
		{
			if (_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			{
				_video_stream_index = i;
				break;
			}
		}
		if (_video_stream_index == -1)
		{
			// No video stream found
			Cleanup();
			return false;
		}

		AVCodecParameters* codecpar = _format_context->streams[_video_stream_index]->codecpar;
		const AVCodec* decoder		= avcodec_find_decoder(codecpar->codec_id);
		if (!decoder)
		{
			// Decoder not found
			Cleanup();
			return false;
		}

		_codec_context = avcodec_alloc_context3(decoder);
		avcodec_parameters_to_context(_codec_context, codecpar);
		if (avcodec_open2(_codec_context, decoder, nullptr) < 0)
		{
			// Could not open codec
			Cleanup();
			return false;
		}

		return true;
	}

	// Create an alpha map using the alpha channel from RGBA
	bool ImageReader::Read()
	{
		if (_output_frame != nullptr || _format_context != nullptr || _codec_context != nullptr)
		{
			Cleanup();
		}

		if (Open(_filename) == false)
		{
			// Initialization failed
			return false;
		}

		AVFrame frame	= {0};
		AVPacket packet = {0};

		while (av_read_frame(_format_context, &packet) >= 0)
		{
			if (packet.stream_index == _video_stream_index)
			{
				if (avcodec_send_packet(_codec_context, &packet) == 0)
				{
					int ret = avcodec_receive_frame(_codec_context, &frame);
					if (ret == 0)
					{
						_original_frame = ImageReader::Convert(&frame, cmn::VideoPixelFormatId::RGBA, frame.width, frame.height);
						av_frame_unref(&frame);
						av_packet_unref(&packet);
						break;
					}
					else
					{
						logtt("Receive frame failed: %d\n", ret);
					}
				}
			}

			av_packet_unref(&packet);
		}

		Cleanup();

		return (_original_frame != nullptr);  // No more frames to read
	}

	bool ImageReader::Resize(int32_t output_width, int32_t output_height, cmn::VideoPixelFormatId output_format)
	{
		if (_original_frame == nullptr || output_format == cmn::VideoPixelFormatId::None)
		{
			return false;  // No original frame or invalid output format
		}

		_output_width  = (output_width % 2) ? output_width + 1 : output_width;	   // Ensure even width
		_output_height = (output_height % 2) ? output_height + 1 : output_height;  // Ensure even height
		if (_output_width <= 0 || _output_height <= 0)
		{
			return false;  // Invalid dimensions
		}

		// Resize
		auto tmp_frame = ImageReader::Convert(_original_frame, cmn::VideoPixelFormatId::RGBA, _output_width, _output_height);
		if (!tmp_frame)
		{
			return false;  // Conversion failed
		}

		// Extract the alpha channel
		if (tmp_frame->format == AV_PIX_FMT_RGBA)
		{
			// Extract the alpha channel from the RGBA frame
			int32_t alpha_size = tmp_frame->width * tmp_frame->height;
			_opacity_frame	   = (uint8_t*)av_mallocz(alpha_size);
			if (_opacity_frame)
			{
				for (int y = 0; y < tmp_frame->height; y++)
				{
					for (int x = 0; x < tmp_frame->width; x++)
					{
						int rgba_index							 = y * tmp_frame->linesize[0] + x * 4;
						_opacity_frame[y * tmp_frame->width + x] = tmp_frame->data[0][rgba_index + 3];
					}
				}
			}
		}

		// Convert to final format
		_output_frame = ImageReader::Convert(tmp_frame, output_format, _output_width, _output_height);
		av_frame_free(&tmp_frame);
		if (!_output_frame)
		{
			return false;
		}
		_output_format = output_format;

		return true;
	}

	void ImageReader::Cleanup()
	{
		if (_codec_context)
		{
			avcodec_free_context(&_codec_context);
		}
		if (_format_context)
		{
			avformat_close_input(&_format_context);
		}
	}

	AVFrame* ImageReader::Convert(AVFrame* frame, cmn::VideoPixelFormatId output_format, int32_t output_width, int32_t output_height)
	{
		if (!frame || output_format == cmn::VideoPixelFormatId::None)
		{
			return nullptr;
		}

		SwsContext* sws_context = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
												 output_width, output_height, ffmpeg::compat::ToAVPixelFormat(output_format),
												 SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (!sws_context)
		{
			return nullptr;
		}

		AVFrame* output_frame = av_frame_alloc();
		if (!output_frame)
		{
			sws_freeContext(sws_context);
			return nullptr;
		}

		output_frame->format = ffmpeg::compat::ToAVPixelFormat(output_format);
		output_frame->width	 = output_width;
		output_frame->height = output_height;

		if (av_frame_get_buffer(output_frame, 0) < 0)
		{
			sws_freeContext(sws_context);
			av_frame_free(&output_frame);
			return nullptr;
		}

		if (sws_scale(sws_context, frame->data, frame->linesize, 0, frame->height, output_frame->data, output_frame->linesize) < 0)
		{
			sws_freeContext(sws_context);
			av_frame_free(&output_frame);
			return nullptr;
		}

		sws_freeContext(sws_context);

		return output_frame;
	}

}  // namespace ffmpeg
