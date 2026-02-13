//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Gilhoon Choi
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

namespace cfg
{
	namespace alrt
	{
		namespace rule
		{
			struct Egress : public Item
			{
			protected:
				bool _stream_status = false;
				bool _llhls_ready = false;
				bool _hls_ready = false;
				bool _transcode_status = false;

			public:
				CFG_DECLARE_CONST_REF_GETTER_OF(IsStreamStatus, _stream_status)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsLLHLSReady, _llhls_ready)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsHLSReady, _hls_ready)
				CFG_DECLARE_CONST_REF_GETTER_OF(IsTranscodeStatus, _transcode_status)

			protected:
				void MakeList() override
				{
					Register<Optional>("StreamStatus", &_stream_status, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						_stream_status = true;
						return nullptr;
					});
					Register<Optional>("LLHLSReady", &_llhls_ready, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						_llhls_ready = true;
						return nullptr;
					});
					Register<Optional>("HLSReady", &_hls_ready, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						_hls_ready = true;
						return nullptr;
					});
					Register<Optional>("TranscodeStatus", &_transcode_status, nullptr, [=]() -> std::shared_ptr<ConfigError> {
						_transcode_status = true;
						return nullptr;
					});
				}
			};
		}  // namespace rule
	}  // namespace alrt
}  // namespace cfg
