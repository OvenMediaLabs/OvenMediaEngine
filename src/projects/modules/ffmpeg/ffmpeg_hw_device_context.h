//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================

#pragma once

#include <memory>

extern "C"
{
#include <libavutil/buffer.h>
#include <libavutil/hwcontext.h>
}

#include <base/ovlibrary/ovlibrary.h>
#include <transcoder/hw_device_context.h>

namespace ffmpeg
{
	class FFmpegHwDeviceContext : public HwDeviceContext
	{
	public:
		static std::shared_ptr<HwDeviceContext> Create(cmn::MediaCodecModuleId module_id, const ov::String &device = "")
		{
			AVHWDeviceType device_type = AV_HWDEVICE_TYPE_NONE;
			int flags				   = 0;

			switch (module_id)
			{
				case cmn::MediaCodecModuleId::NVENC:
					device_type = AV_HWDEVICE_TYPE_CUDA;
					flags		= 1;  // Use the primary CUDA context
					break;
				case cmn::MediaCodecModuleId::QSV:
					device_type = AV_HWDEVICE_TYPE_QSV;
					break;
#ifdef HWACCELS_NILOGAN_ENABLED
				case cmn::MediaCodecModuleId::NILOGAN:
					device_type = AV_HWDEVICE_TYPE_NI_LOGAN;
					break;
#endif
				default:
					return nullptr;
			}

			AVBufferRef *device_ctx = nullptr;
			int ret = ::av_hwdevice_ctx_create(&device_ctx, device_type, device.IsEmpty() ? nullptr : device.CStr(), nullptr, flags);
			if (ret < 0)
			{
				::av_buffer_unref(&device_ctx);

				return nullptr;
			}

			return std::make_shared<ffmpeg::FFmpegHwDeviceContext>(module_id, device_ctx);
		}

		// Takes ownership of the given device context buffer.
		FFmpegHwDeviceContext(cmn::MediaCodecModuleId module_id, AVBufferRef *device_ctx)
			: _module_id(module_id),
			  _device_ctx(device_ctx)
		{
		}

		~FFmpegHwDeviceContext() override
		{
			if (_device_ctx != nullptr)
			{
				::av_buffer_unref(&_device_ctx);
			}
		}

		cmn::MediaCodecModuleId GetModuleId() const override
		{
			return _module_id;
		}

		void *GetNativeHandle() const override
		{
			return _device_ctx;
		}

	private:
		cmn::MediaCodecModuleId _module_id;
		AVBufferRef *_device_ctx = nullptr;
	};
}  // namespace ffmpeg
