#pragma once

#include <string>

#include "base/common_types.h"
#include "base/ovlibrary/enable_shared_from_this.h"

namespace info
{
	class CodecModuleMetrics
	{
	public:
		CodecModuleMetrics()
		{
			Reset();
		}
		~CodecModuleMetrics() = default;

		void Reset()
		{
			_active_decoder = 0;
			_active_encoder = 0;
			_load_decoder	= 0;
			_load_encoder	= 0;
		}

		void IncreaseActiveDecoder()
		{
			_active_decoder++;
		}
		void DecreaseActiveDecoder()
		{
			if (_active_decoder > 0)
				_active_decoder--;
		}

		void IncreaseActiveEncoder()
		{
			_active_encoder++;
		}
		void DecreaseActiveEncoder()
		{
			if (_active_encoder > 0)
				_active_encoder--;
		}

		void SetLoadDecoder(double load)
		{
			_load_decoder = load;
		}

		void SetLoadEncoder(double load)
		{
			_load_encoder = load;
		}

	public:
		int32_t _active_decoder;
		int32_t _active_encoder;
		double _load_decoder;
		double _load_encoder;
	};

	class CodecModule
	{
	public:
		CodecModule() = delete;
		CodecModule(const ov::String &display_name, cmn::MediaType media_type, cmn::MediaCodecModuleId module_id, cmn::DeviceId device_id, const ov::String &bus_id, const std::vector<cmn::MediaCodecId> &supported_codecs, bool is_default, bool is_decoder, bool is_encoder, bool is_hwaccel)
			: _display_name(display_name),
			  _media_type(media_type),
			  _module_id(module_id),
			  _device_id(device_id),
			  _bus_id(bus_id),
			  _supported_codecs(supported_codecs),
			  _is_default(is_default),
			  _is_encoder(is_encoder),
			  _is_decoder(is_decoder),
			  _is_hwaccel(is_hwaccel)
		{
		}
		~CodecModule() = default;

		ov::String GetName() const
		{
			return ov::String::FormatString("%s:%d", cmn::GetCodecModuleIdString(_module_id), _device_id);
		}

		ov::String GetBusId() const
		{
			return _bus_id;
		}

		ov::String GetDisplayName() const
		{
			return _display_name;
		}

		cmn::MediaCodecModuleId GetModuleId() const
		{
			return _module_id;
		}

		cmn::MediaType GetMediaType() const
		{
			return _media_type;
		}

		cmn::DeviceId GetDeviceId() const
		{
			return _device_id;
		}

		CodecModuleMetrics &GetMetrics()
		{
			return _metrics;
		}

		std::vector<cmn::MediaCodecId> GetSupportedCodecs() const
		{
			return _supported_codecs;
		}

		bool isDefault() const
		{
			return _is_default;
		}

		bool isEncoder() const
		{
			return _is_encoder;
		}

		bool isDecoder() const
		{
			return _is_decoder;
		}

		bool isHwAccel() const
		{
			return _is_hwaccel;
		}

		ov::String GetInfoString() const
		{
			ov::String supported_codecs;
			for (const auto &codec : _supported_codecs)
			{
				if (supported_codecs.IsEmpty() == false)
				{
					supported_codecs.Append(",");
				}
				supported_codecs.Append(cmn::GetCodecIdString(codec));
			}

			ov::String metrics = ov::String::FormatString("dec%2d,enc%2d",
														  _metrics._active_decoder,
														  _metrics._active_encoder);

			return ov::String::FormatString("Name:%11s, DisplayName: %36s, Type: %s, Module: %8s, Id: %d, Default: %3s, Decoder: %3s, Encoder: %3s, HWAccel: %3s, BusID: %16s, Supported: [%13s], Metrics: [%s]",
											GetName().CStr(),
											_display_name.CStr(),
											cmn::GetMediaTypeString(_media_type),
											cmn::GetCodecModuleIdString(_module_id),
											_device_id,
											_is_default ? "Yes" : "No",
											_is_decoder ? "Yes" : "No",
											_is_encoder ? "Yes" : "No",
											_is_hwaccel ? "Yes" : "No",
											_bus_id.CStr(),
											supported_codecs.CStr(),
											metrics.CStr());
		}

	private:
		ov::String _display_name;
		cmn::MediaType _media_type;
		cmn::MediaCodecModuleId _module_id;
		cmn::DeviceId _device_id;
		ov::String _bus_id;
		std::vector<cmn::MediaCodecId> _supported_codecs;
		CodecModuleMetrics _metrics;

		bool _is_default;
		bool _is_encoder;
		bool _is_decoder;
		bool _is_hwaccel;
	};

	class CodecCandidate
	{
	public:
		CodecCandidate(cmn::MediaCodecId codec_id, cmn::MediaCodecModuleId module_id, cmn::DeviceId device_id)
			: _codec_id(codec_id),
			  _module_id(module_id),
			  _device_id(device_id)
		{
		}

		cmn::MediaCodecModuleId GetModuleId() const
		{
			return _module_id;
		}

		cmn::DeviceId GetDeviceId() const
		{
			return _device_id;
		}

		cmn::MediaCodecId GetCodecId() const
		{
			return _codec_id;
		}

	private:
		cmn::MediaCodecId _codec_id;
		cmn::MediaCodecModuleId _module_id;
		cmn::DeviceId _device_id;
	};

}  // namespace info