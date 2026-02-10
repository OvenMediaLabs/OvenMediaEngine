//==============================================================================
//
//  Type Convert Utilities
//
//  Created by Kwon Keuk Han
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <algorithm>
#include <memory>
#include <thread>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/channel_layout.h>
#include <libavutil/cpu.h>
#include <libavutil/file.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}

#include <base/common_types.h>
#include <base/info/media_track.h>
#include <base/info/push.h>
#include <base/mediarouter/media_type.h>
#include <base/ovlibrary/ovlibrary.h>
#include <modules/bitstream/aac/audio_specific_config.h>
#include <modules/bitstream/h264/h264_decoder_configuration_record.h>
#include <modules/bitstream/h265/h265_decoder_configuration_record.h>
#include <modules/bitstream/opus/opus_specific_config.h>
#include <transcoder/transcoder_context.h>

namespace ffmpeg
{
	class compat
	{
	public:
		static cmn::MediaType ToMediaType(enum AVMediaType codec_type);
		static enum AVMediaType ToAVMediaType(cmn::MediaType media_type);
		static cmn::MediaCodecId ToCodecId(enum AVCodecID codec_id);
		static AVCodecID ToAVCodecId(cmn::MediaCodecId codec_id);
		static cmn::AudioSample::Format ToAudioSampleFormat(int format);
		static int ToAvSampleFormat(cmn::AudioSample::Format format);
		static int ToAVChannelLayout(cmn::AudioChannel::Layout channel_layout);
		static cmn::AudioChannel::Layout ToAudioChannelLayout(int channel_layout);
		static AVPixelFormat ToAVPixelFormat(cmn::VideoPixelFormatId pixel_format);
		static cmn::VideoPixelFormatId ToVideoPixelFormat(int32_t pixel_format);
		static AVPixelFormat GetAVPixelFormatOfHWDevice(cmn::MediaCodecModuleId module_id, cmn::DeviceId gpu_id, bool is_sw_format = true);
		static ov::String GetAVOptionsString(void *opts);

		static std::shared_ptr<MediaTrack> CreateMediaTrack(AVStream* stream)
		{
			auto media_track = std::make_shared<MediaTrack>();
			if (ToMediaTrack(stream, media_track) == false)
			{
				return nullptr;
			}

			return media_track;
		}

		static bool ToMediaTrack(AVStream* stream, std::shared_ptr<MediaTrack> media_track)
		{
			media_track->SetId(stream->index);
			media_track->SetMediaType(ffmpeg::compat::ToMediaType(stream->codecpar->codec_type));
			media_track->SetCodecId(ffmpeg::compat::ToCodecId(stream->codecpar->codec_id));
			media_track->SetTimeBase(stream->time_base.num, stream->time_base.den);
			media_track->SetBitrateByConfig(stream->codecpar->bit_rate);
			media_track->SetStartFrameTime(0);
			media_track->SetLastFrameTime(0);

			if (media_track->GetMediaType() == cmn::MediaType::Unknown || media_track->GetCodecId() == cmn::MediaCodecId::None)
			{
				return false;
			}

			switch (media_track->GetMediaType())
			{
				case cmn::MediaType::Video:
					media_track->SetFrameRateByConfig(av_q2d(stream->r_frame_rate));
					media_track->SetWidth(stream->codecpar->width);
					media_track->SetHeight(stream->codecpar->height);
					break;
				case cmn::MediaType::Audio:
					media_track->SetSampleRate(stream->codecpar->sample_rate);
					media_track->GetSample().SetFormat(ffmpeg::compat::ToAudioSampleFormat(stream->codecpar->format));
					media_track->GetChannel().SetLayout(ffmpeg::compat::ToAudioChannelLayout(stream->codecpar->ch_layout.u.mask));
					break;
				default:
					break;
			}

			switch (media_track->GetCodecId())
			{
				case cmn::MediaCodecId::H264: {
					if (stream->codecpar->extradata_size > 0)
					{
						// AVCC format
						auto avc_config = std::make_shared<AVCDecoderConfigurationRecord>();
						auto extra_data = std::make_shared<ov::Data>(stream->codecpar->extradata, stream->codecpar->extradata_size, true);

						if (avc_config->Parse(extra_data) == false)
						{
							return false;
						}

						media_track->SetDecoderConfigurationRecord(avc_config);
					}

					break;
				}
				case cmn::MediaCodecId::H265: {
					if (stream->codecpar->extradata_size > 0)
					{
						// HVCC format
						auto hevc_config = std::make_shared<HEVCDecoderConfigurationRecord>();
						auto extra_data	 = std::make_shared<ov::Data>(stream->codecpar->extradata, stream->codecpar->extradata_size, true);

						if (hevc_config->Parse(extra_data) == false)
						{
							return false;
						}

						media_track->SetDecoderConfigurationRecord(hevc_config);
					}

					break;
				}
				case cmn::MediaCodecId::Aac: {
					if (stream->codecpar->extradata_size > 0)
					{
						// ASC format
						auto asc		= std::make_shared<AudioSpecificConfig>();
						auto extra_data = std::make_shared<ov::Data>(stream->codecpar->extradata, stream->codecpar->extradata_size, true);

						if (asc->Parse(extra_data) == false)
						{
							return false;
						}

						media_track->SetDecoderConfigurationRecord(asc);
					}

					break;
				}
				case cmn::MediaCodecId::Opus: {
					if (stream->codecpar->extradata_size > 0)
					{
						// ASC format
						auto opus_config = std::make_shared<OpusSpecificConfig>();
						auto extra_data	 = std::make_shared<ov::Data>(stream->codecpar->extradata, stream->codecpar->extradata_size, true);

						if (opus_config->Parse(extra_data) == false)
						{
							return false;
						}

						media_track->SetDecoderConfigurationRecord(opus_config);
					}

					break;
				}
				case cmn::MediaCodecId::Vp8:
				case cmn::MediaCodecId::Vp9:
				case cmn::MediaCodecId::Flv:
				case cmn::MediaCodecId::Mp3:
				case cmn::MediaCodecId::Jpeg:
				case cmn::MediaCodecId::Png:
				default:
					break;
			}

			return true;
		}

		static inline std::shared_ptr<MediaFrame> ToMediaFrame(cmn::MediaType media_type, AVFrame* frame)
		{
			switch (media_type)
			{
				case cmn::MediaType::Video: {
					auto media_frame = std::make_shared<MediaFrame>();

					media_frame->SetMediaType(media_type);
					media_frame->SetWidth(frame->width);
					media_frame->SetHeight(frame->height);
					media_frame->SetFormat((int32_t)ffmpeg::compat::ToVideoPixelFormat(frame->format));
					media_frame->SetPts((frame->pts == AV_NOPTS_VALUE) ? -1LL : frame->pts);
					media_frame->SetDuration(frame->pkt_duration);

					AVFrame* moved_frame = av_frame_alloc();
					av_frame_move_ref(moved_frame, frame);
					media_frame->SetPrivData(moved_frame);

					return media_frame;
				}
				break;
				case cmn::MediaType::Audio: {
					auto media_frame = std::make_shared<MediaFrame>();

					media_frame->SetMediaType(media_type);
					media_frame->SetBytesPerSample(::av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format)));
					media_frame->SetNbSamples(frame->nb_samples);
					media_frame->GetChannels().SetLayout(ffmpeg::compat::ToAudioChannelLayout(frame->ch_layout.u.mask));
					media_frame->SetSampleRate(frame->sample_rate);
					media_frame->SetFormat(frame->format);
					media_frame->SetDuration(frame->pkt_duration);
					media_frame->SetPts((frame->pts == AV_NOPTS_VALUE) ? -1LL : frame->pts);

					AVFrame* moved_frame = av_frame_alloc();
					av_frame_move_ref(moved_frame, frame);
					media_frame->SetPrivData(moved_frame);

					return media_frame;
				}
				break;
				default:
					return nullptr;
					break;
			}

			return nullptr;
		}

		static inline std::shared_ptr<MediaFrame> ToMediaFrameV2(cmn::MediaType media_type, AVFrame* frame)
		{
			switch (media_type)
			{
				case cmn::MediaType::Video: {
					auto media_frame = std::make_shared<MediaFrame>();

					media_frame->SetMediaType(media_type);
					media_frame->SetWidth(frame->width);
					media_frame->SetHeight(frame->height);
					media_frame->SetFormat((int32_t)ffmpeg::compat::ToVideoPixelFormat(frame->format));
					media_frame->SetPts((frame->pts == AV_NOPTS_VALUE) ? -1LL : frame->pts);
					media_frame->SetDuration(frame->pkt_duration);

					AVFrame* moved_frame = av_frame_alloc();
					av_frame_move_ref(moved_frame, frame);
					media_frame->SetPrivData(moved_frame);

					return media_frame;
				}
				break;
				case cmn::MediaType::Audio: {
					auto media_frame = std::make_shared<MediaFrame>();

					media_frame->SetMediaType(media_type);
					media_frame->SetBytesPerSample(::av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame->format)));
					media_frame->SetNbSamples(frame->nb_samples);
					media_frame->GetChannels().SetLayout(ffmpeg::compat::ToAudioChannelLayout(frame->ch_layout.u.mask));
					media_frame->SetSampleRate(frame->sample_rate);
					media_frame->SetFormat(frame->format);
					media_frame->SetDuration(frame->pkt_duration);
					media_frame->SetPts((frame->pts == AV_NOPTS_VALUE) ? -1LL : frame->pts);

					AVFrame* moved_frame = av_frame_alloc();
					av_frame_move_ref(moved_frame, frame);
					media_frame->SetPrivData(moved_frame);

					return media_frame;
				}
				break;
				default:
					return nullptr;
					break;
			}

			return nullptr;
		}

		static inline int64_t GetDurationPerFrame(cmn::MediaType media_type, std::shared_ptr<MediaTrack>& track, AVFrame* frame = nullptr)
		{
			if (frame == nullptr || track->GetTimeBase().GetDen() == 0)
			{
				return 0LL;
			}

			switch (track->GetMediaType())
			{
				case cmn::MediaType::Video: {
					// The video frame rate changes, so this code finds the average duration. It does not show the exact frame rate.
					double frame_duration_per_timebase = (double)(track->GetTimeBase().GetDen()) / track->GetFrameRate();
					return static_cast<int64_t>(frame_duration_per_timebase);
				}
				break;
				case cmn::MediaType::Audio:
				default: {
					double frame_duration_per_timebase = ((double)frame->nb_samples * (double)(track->GetTimeBase().GetDen())) / (double)frame->sample_rate;
					return static_cast<int64_t>(frame_duration_per_timebase);
				}
				break;
			}

			return 0LL;
		}

		static AVFrame* ToAVFrame(cmn::MediaType media_type, std::shared_ptr<const MediaFrame> src)
		{
			if (src->GetPrivData() != nullptr)
			{
				return static_cast<AVFrame*>(src->GetPrivData());
			}

			return nullptr;
		}

		static std::shared_ptr<MediaPacket> ToMediaPacket(AVPacket* src, cmn::MediaType media_type, cmn::BitstreamFormat format, cmn::PacketType packet_type)
		{
			auto packet_buffer = std::make_shared<MediaPacket>(
				0,
				media_type,
				0,
				src->data,
				src->size,
				src->pts,
				src->dts,
				src->duration,
				(src->flags & AV_PKT_FLAG_KEY) ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
				format,
				packet_type
			);

			if (packet_buffer == nullptr)
			{
				return nullptr;
			}


			return packet_buffer;
		}

		static std::shared_ptr<MediaPacket> ToMediaPacket(uint32_t msid, int32_t track_id, AVPacket* src, cmn::MediaType media_type, cmn::BitstreamFormat format, cmn::PacketType packet_type)
		{
			auto packet_buffer = std::make_shared<MediaPacket>(
				msid,
				media_type,
				track_id,
				src->data,
				src->size,
				src->pts,
				src->dts,
				src->duration,
				(src->flags & AV_PKT_FLAG_KEY) ? MediaPacketFlag::Key : MediaPacketFlag::NoFlag,
				format,
				packet_type);

			if (packet_buffer == nullptr)
			{
				return nullptr;
			}


			return packet_buffer;
		}

		static AVRational TimebaseToAVRational(const cmn::Timebase& timebase)
		{
			return (AVRational){
				.num = timebase.GetNum(),
				.den = timebase.GetDen()};
		}

		static ov::String CodecInfoToString(const AVCodecContext* context)
		{
			ov::String message;

			message.AppendFormat("[%s] ", ::av_get_media_type_string(context->codec_type));

			switch (context->codec_type)
			{
				case AVMEDIA_TYPE_UNKNOWN:
					message = "Unknown media type";
					break;

				case AVMEDIA_TYPE_VIDEO:
				case AVMEDIA_TYPE_AUDIO:
					// H.264 (Baseline
					message.AppendFormat("%s (%s", ::avcodec_get_name(context->codec_id), ::avcodec_profile_name(context->codec_id, context->profile));

					if (context->level >= 0)
					{
						// lv: 5.2
						message.AppendFormat(" %.1f", context->level / 10.0f);
					}
					message.Append(')');

					if (context->codec_tag != 0)
					{
						char tag[AV_FOURCC_MAX_STRING_SIZE]{};
						::av_fourcc_make_string(tag, context->codec_tag);

						// (avc1 / 0x31637661
						message.AppendFormat(", (%s / 0x%08X", tag, context->codec_tag);

						if (context->extradata_size != 0)
						{
							// extra: 1234
							message.AppendFormat(", extra: %d", context->extradata_size);
						}
						// frame_size: 1234
						message.AppendFormat(", frame_size: %d", context->frame_size);

						message.Append(')');
					}

					message.Append(", ");

					if (context->codec_type == AVMEDIA_TYPE_VIDEO)
					{
						int gcd = ::av_gcd(context->width, context->height);

						if (gcd == 0)
						{
							OV_ASSERT2(false);
							gcd = 1;
						}

						int digit = 0;

						if (context->framerate.den > 1)
						{
							digit = 3;
						}

						// yuv420p, 1920x1080 [SAR 1:1 DAR 16:9], 24 fps
						message.AppendFormat("%s, %dx%d [SAR %d:%d DAR %d:%d], %.*f fps, ",
											 ::av_get_pix_fmt_name(static_cast<AVPixelFormat>(context->pix_fmt)),
											 context->width, context->height,
											 context->sample_aspect_ratio.num, context->sample_aspect_ratio.den,
											 context->width / gcd, context->height / gcd,
											 digit, ::av_q2d(context->framerate));
					}
					else
					{
						char channel_layout[16]{};
						av_channel_layout_describe(&context->ch_layout, channel_layout, OV_COUNTOF(channel_layout));
						// 48000 Hz, stereo, fltp,
						message.AppendFormat("%d Hz, %s, %s, ", context->sample_rate, channel_layout, ::av_get_sample_fmt_name(static_cast<AVSampleFormat>(context->sample_fmt)));
					}

					message.AppendFormat("%" PRId64 " kbps, ", (context->bit_rate / 1024));

					// timebase: 1/48000
					message.AppendFormat("timebase: %d/%d", context->time_base.num, context->time_base.den);

					break;

				case AVMEDIA_TYPE_DATA:
					message = "Data";
					break;

				case AVMEDIA_TYPE_SUBTITLE:
					message = "Subtitle";
					break;

				case AVMEDIA_TYPE_ATTACHMENT:
					message = "Attachment";
					break;

				case AVMEDIA_TYPE_NB:
					message = "NB";
					break;
			}

			return message;
		}

		static ov::String AVErrorToString(int32_t error)
		{
			char error_buffer[AV_ERROR_MAX_STRING_SIZE]{};
			::av_strerror(error, error_buffer, OV_COUNTOF(error_buffer));

			ov::String message = error_buffer;

			return error_buffer;
		}

		static bool ToAVStream(std::shared_ptr<MediaTrack> media_track, AVStream* av_stream)
		{
			if (media_track == nullptr || av_stream == nullptr)
			{
				return false;
			}

			av_stream->start_time		= 0;
			av_stream->time_base		= AVRational{media_track->GetTimeBase().GetNum(), media_track->GetTimeBase().GetDen()};
			AVCodecParameters* codecpar = av_stream->codecpar;
			codecpar->codec_type		= ToAVMediaType(media_track->GetMediaType());
			codecpar->codec_id			= ToAVCodecId(media_track->GetCodecId());
			codecpar->codec_tag			= 0;
			codecpar->bit_rate			= media_track->GetBitrate();

			// Set Decoder Configuration Record to extradata
			if (media_track->GetDecoderConfigurationRecord() != nullptr &&
				media_track->GetDecoderConfigurationRecord()->GetData() != nullptr &&
				media_track->GetDecoderConfigurationRecord()->GetData()->GetLength() > 0)
			{
				codecpar->extradata_size = media_track->GetDecoderConfigurationRecord()->GetData()->GetLength();
				codecpar->extradata		 = (uint8_t*)av_malloc(codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
				memset(codecpar->extradata, 0, codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
				memcpy(codecpar->extradata, media_track->GetDecoderConfigurationRecord()->GetData()->GetDataAs<uint8_t>(), codecpar->extradata_size);
			}

			switch (media_track->GetMediaType())
			{
				case cmn::MediaType::Video: {
					av_stream->r_frame_rate		   = AVRational{(int)round(media_track->GetFrameRate() * 1000), 1000};
					av_stream->avg_frame_rate	   = AVRational{(int)round(media_track->GetFrameRate() * 1000), 1000};
					av_stream->sample_aspect_ratio = AVRational{1, 1};
					codecpar->width				   = media_track->GetWidth();
					codecpar->height			   = media_track->GetHeight();
					codecpar->sample_aspect_ratio  = AVRational{1, 1};

					// Compatible with macOS
					if (media_track->GetCodecId() == cmn::MediaCodecId::H265)
					{
						codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
					}
					else if (media_track->GetCodecId() == cmn::MediaCodecId::H264)
					{
						codecpar->codec_tag = MKTAG('a', 'v', 'c', '1');
					}
				}
				break;

				case cmn::MediaType::Audio: {
					av_channel_layout_default(&codecpar->ch_layout, (int)media_track->GetChannel().GetCounts());
					codecpar->sample_rate = media_track->GetSample().GetRateNum();
					codecpar->frame_size  = (media_track->GetAudioSamplesPerFrame() != 0) ? media_track->GetAudioSamplesPerFrame() : 1024;
				}
				break;

				case cmn::MediaType::Data: {
					codecpar->codec_id = AV_CODEC_ID_NONE;
				}
				break;
				default:
					return false;
			}

			return true;
		}

		static ov::String GetCodecName(AVCodecID codec_id)
		{
			return ov::String::FormatString("%s", ::avcodec_get_name(codec_id));
		}

		static ov::String GetFormatByExtension(ov::String extension, ov::String default_format = "mpegts")
		{
			if (extension == "mp4")
			{
				return "mp4";
			}
			else if (extension == "ts")
			{
				return "mpegts";
			}
			else if (extension == "webm")
			{
				return "webm";
			}

			return default_format;
		}

		static ov::String GetFormatByProtocolType(const info::Push::ProtocolType protocol_type, ov::String default_format = "flv")
		{
			switch (protocol_type)
			{
				case info::Push::ProtocolType::RTMP:
					return "flv";
				case info::Push::ProtocolType::MPEGTS:
					return "mpegts";
				case info::Push::ProtocolType::SRT:
					return "mpegts";
				default:
					break;
			}

			return default_format;
		}

		static bool IsSupportCodec(ov::String format, cmn::MediaCodecId codec_id)
		{
			if (format == "mp4")
			{
				if (codec_id == cmn::MediaCodecId::H264 ||
					codec_id == cmn::MediaCodecId::H265 ||
					codec_id == cmn::MediaCodecId::Aac ||
					codec_id == cmn::MediaCodecId::Mp3)
				{
					return true;
				}
			}
			else if (format == "mpegts")
			{
				if (codec_id == cmn::MediaCodecId::H264 ||
					codec_id == cmn::MediaCodecId::H265 ||
					codec_id == cmn::MediaCodecId::Vp8 ||
					codec_id == cmn::MediaCodecId::Vp9 ||
					codec_id == cmn::MediaCodecId::Aac ||
					codec_id == cmn::MediaCodecId::Mp3 ||
					codec_id == cmn::MediaCodecId::Opus)
				{
					return true;
				}
			}
			else if (format == "webm")
			{
				if (
					codec_id == cmn::MediaCodecId::Vp8 ||
					codec_id == cmn::MediaCodecId::Vp9 ||
					codec_id == cmn::MediaCodecId::Opus)
				{
					return true;
				}
			}

			return false;
		}

		static bool SetHwDeviceCtxOfAVCodecContext(AVCodecContext* context, AVBufferRef* hw_device_ctx)
		{
			context->hw_device_ctx = ::av_buffer_ref(hw_device_ctx);
			if (context->hw_device_ctx == nullptr)
			{
				return false;
			}

			return true;
		}

		static bool SetHWFramesCtxOfAVCodecContext(AVCodecContext* context)
		{
			AVBufferRef* hw_frames_ref;
			int err		  = 0;
			hw_frames_ref = ::av_hwframe_ctx_alloc(context->hw_device_ctx);
			if (!hw_frames_ref)
			{
				return false;
			}

			auto constraints = ::av_hwdevice_get_hwframe_constraints(context->hw_device_ctx, nullptr);
			if (constraints == nullptr)
			{
				::av_buffer_unref(&hw_frames_ref);
				return false;
			}

			AVHWFramesContext* frames_ctx = nullptr;
			frames_ctx					  = (AVHWFramesContext*)(hw_frames_ref->data);
			frames_ctx->format			  = *(constraints->valid_hw_formats);
			frames_ctx->sw_format		  = *(constraints->valid_sw_formats);
			frames_ctx->width			  = context->width;
			frames_ctx->height			  = context->height;
			frames_ctx->initial_pool_size = 2;

			::av_hwframe_constraints_free(&constraints);

			if ((err = ::av_hwframe_ctx_init(hw_frames_ref)) < 0)
			{
				::av_buffer_unref(&hw_frames_ref);
				return false;
			}

			context->hw_frames_ctx = hw_frames_ref;

			return true;
		}

		static cmn::VideoPixelFormatId GetHWFramesConstraintsValidSWFormat(std::shared_ptr<const MediaFrame> frame)
		{
			if (frame == nullptr || frame->GetPrivData() == nullptr)
			{
				return cmn::VideoPixelFormatId::None;
			}

			auto av_frame = static_cast<AVFrame*>(frame->GetPrivData());
			return GetHWFramesConstraintsValidSWFormat(av_frame);
		}

		static cmn::VideoPixelFormatId GetHWFramesConstraintsValidHWFormat(std::shared_ptr<const MediaFrame> frame)
		{
			if (frame == nullptr || frame->GetPrivData() == nullptr)
			{
				return cmn::VideoPixelFormatId::None;
			}

			auto av_frame = static_cast<AVFrame*>(frame->GetPrivData());
			return GetHWFramesConstraintsValidHWFormat(av_frame);
		}

		static cmn::VideoPixelFormatId GetHWFramesConstraintsValidSWFormat(AVFrame* frame)
		{
			if (frame == nullptr)
			{
				return cmn::VideoPixelFormatId::None;
			}

			if (frame->hw_frames_ctx == nullptr)
			{
				return cmn::VideoPixelFormatId::None;
			}

			auto constraints = ::av_hwdevice_get_hwframe_constraints(frame->hw_frames_ctx, nullptr);
			if (constraints == nullptr)
			{
				return cmn::VideoPixelFormatId::None;
			}

			auto valid_sw_formats = *(constraints->valid_sw_formats);
			::av_hwframe_constraints_free(&constraints);

			return ToVideoPixelFormat(valid_sw_formats);
		}

		static cmn::VideoPixelFormatId GetHWFramesConstraintsValidHWFormat(AVFrame* frame)
		{
			if (frame == nullptr)
			{
				return cmn::VideoPixelFormatId::None;
			}

			if (frame->hw_frames_ctx == nullptr)
			{
				return cmn::VideoPixelFormatId::None;
			}

			auto constraints = ::av_hwdevice_get_hwframe_constraints(frame->hw_frames_ctx, nullptr);
			if (constraints == nullptr)
			{
				return cmn::VideoPixelFormatId::None;
			}

			auto valid_hw_formats = *(constraints->valid_hw_formats);
			::av_hwframe_constraints_free(&constraints);

			return ToVideoPixelFormat(valid_hw_formats);
		}

		static bool SetHwDeviceCtxOfAVFilterContext(AVFilterContext* context, AVBufferRef* hw_device_ctx)
		{
			context->hw_device_ctx = ::av_buffer_ref(hw_device_ctx);
			if (context->hw_device_ctx == nullptr)
			{
				return false;
			}

			return true;
		}
		
		static bool SetHWFramesCtxOfAVFilterLink(AVFilterLink* context, AVBufferRef* hw_device_ctx, int32_t width, int32_t height)
		{
			AVBufferRef* hw_frames_ref;
			AVHWFramesContext* frames_ctx = NULL;

			if (!(hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx)))
			{
				return false;
			}

			auto constraints = av_hwdevice_get_hwframe_constraints(hw_device_ctx, nullptr);
			if (constraints == nullptr)
			{
				av_buffer_unref(&hw_frames_ref);
				return false;
			}

			frames_ctx					  = (AVHWFramesContext*)(hw_frames_ref->data);
			frames_ctx->format			  = *(constraints->valid_hw_formats);
			frames_ctx->sw_format		  = *(constraints->valid_sw_formats);
			frames_ctx->width			  = width;
			frames_ctx->height			  = height;
			frames_ctx->initial_pool_size = 2;

			::av_hwframe_constraints_free(&constraints);

			if (av_hwframe_ctx_init(hw_frames_ref) < 0)
			{
				av_buffer_unref(&hw_frames_ref);
				return false;
			}

			context->hw_frames_ctx = hw_frames_ref;

			return true;
		}

		class PadedAlignedBuffer
		{
		public:
			PadedAlignedBuffer()  = default;
			~PadedAlignedBuffer() = default;

			bool CopyFrom(std::shared_ptr<const MediaPacket> packet, const std::shared_ptr<const ov::Data> data)
			{
				if (!packet || packet->GetData() == nullptr || packet->GetDataLength() == 0)
				{
					return false;
				}

				_remained_size = packet->GetDataLength();
				_offset		   = 0LL;
				_pts		   = (packet->GetPts() == -1LL) ? AV_NOPTS_VALUE : packet->GetPts();
				_dts		   = (packet->GetDts() == -1LL) ? AV_NOPTS_VALUE : packet->GetDts();
				_duration	   = (packet->GetDuration() == -1LL) ? AV_NOPTS_VALUE : packet->GetDuration();

				return CopyFrom(data->GetDataAs<uint8_t>(), static_cast<uint32_t>(data->GetLength()));
			}

			bool CopyFrom(const uint8_t* src_data, uint32_t src_size)
			{
				const uint32_t required_size = src_size + AV_INPUT_BUFFER_PADDING_SIZE;

				// The starting address of the memory buffer passed to av_parser_parse2 must be aligned to 32 bytes (@av_malloc),
				// and it must include padding of size AV_INPUT_BUFFER_PADDING_SIZE to prevent crashes.
				if (!_buffer || _buffer_size < required_size)
				{
					// Free the existing buffer if it exists
					uint8_t* raw_ptr = static_cast<uint8_t*>(::av_malloc(required_size));
					if (!raw_ptr)
					{
						return false;
					}

					if (reinterpret_cast<uintptr_t>(raw_ptr) % 32 != 0)
					{
						loge("FFmpegCompat", "Buffer pointer is not 32-byte aligned.");
					}

					_buffer		 = BufferPtr(raw_ptr, Deleter());
					_buffer_size = required_size;
				}

				// Copy the data to the aligned buffer and fill the remaining space with zeros.
				std::memset(_buffer.get(), 0, _buffer_size);
				std::memcpy(_buffer.get(), src_data, src_size);
				std::memset(_buffer.get() + src_size, 0, _buffer_size - src_size);
				_last_data_size = src_size;

				return true;
			}

			uint8_t* Data() const
			{
				return _buffer.get();
			}

			uint8_t* DataAt(uint32_t offset) const
			{
				if (offset >= _buffer_size)
				{
					return nullptr;
				}
				return _buffer.get() + offset;
			}

			uint8_t* DataAtCurrentOffset()
			{
				if (_remained_size <= 0)
				{
					return nullptr;
				}

				if (_offset >= _buffer_size)
				{
					return nullptr;
				}

				uint8_t* data = _buffer.get() + _offset;

				return data;
			}

			int64_t GetRemainedSize()
			{
				return _remained_size;
			}

			void Advance(int64_t size)
			{
				if (size < 0 || _offset + size > _buffer_size)
				{
					return;
				}

				_offset += size;
				_remained_size -= size;

				if (_remained_size < 0)
				{
					_remained_size = 0;
				}
			}

			uint32_t Capacity() const
			{
				return _buffer_size;
			}

			uint32_t Size() const
			{
				return _last_data_size;
			}

			bool Empty() const
			{
				return !_buffer;
			}

			int64_t GetPts()
			{
				return _pts;
			}

			int64_t GetDts()
			{
				return _dts;
			}

			int64_t GetDuration()
			{
				return _duration;
			}

		private:
			struct Deleter
			{
				void operator()(uint8_t* ptr) const
				{
					if (ptr)
					{
						::av_free(ptr);
					}
				}
			};

			using BufferPtr			 = std::shared_ptr<uint8_t>;
			BufferPtr _buffer		 = nullptr;
			uint32_t _buffer_size	 = 0;
			uint32_t _last_data_size = 0;

			int64_t _pts			 = 0;
			int64_t _dts			 = 0;
			int64_t _duration		 = 0;

			int64_t _remained_size	 = 0;
			off_t _offset			 = 0;
		};
	};

}  // namespace ffmpeg
