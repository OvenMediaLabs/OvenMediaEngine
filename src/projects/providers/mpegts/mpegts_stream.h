//==============================================================================
//
//  MpegTs Stream
//
//  Created by Hyunjun Jang
//  Moved by Getroot
//
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//==============================================================================

#include "base/common_types.h"
#include "base/provider/push_provider/stream.h"
#include "modules/containers/mpegts/mpegts_depacketizer.h"

namespace pvd
{
	class MpegTsStream : public PushStream
	{
	public:
		static std::shared_ptr<MpegTsStream> Create(StreamSourceType source_type, uint32_t channel_id, const info::VHostAppName &vhost_app_name, const ov::String &stream_name, const std::shared_ptr<ov::Socket> &client_socket, const ov::SocketAddress &remote_address, uint64_t lifetime_epoch_msec, const std::shared_ptr<PushProvider> &provider);
		
		explicit MpegTsStream(StreamSourceType source_type, uint32_t channel_id, const info::VHostAppName &vhost_app_name, const ov::String &stream_name, std::shared_ptr<ov::Socket> client_socket, const ov::SocketAddress &remote_address, uint64_t lifetime_epoch_msec, const std::shared_ptr<PushProvider> &provider);
		~MpegTsStream() final;

		bool Stop() override;

		const std::shared_ptr<ov::Socket>&	GetClientSock();

		// ------------------------------------------
		// Implementation of PushStream
		// ------------------------------------------
		PushStreamType GetPushStreamType() override
		{
			return PushStream::PushStreamType::INTERLEAVED;
		}
		bool OnDataReceived(const std::shared_ptr<const ov::Data> &data) override;

	private:
		bool Start() override;	
		bool Publish();

		// Client socket
		std::shared_ptr<ov::Socket> _remote = nullptr;

		std::shared_mutex _depacketizer_lock;
		mpegts::MpegTsDepacketizer	_depacketizer;

		info::VHostAppName _vhost_app_name;

		uint64_t _lifetime_epoch_msec;

		bool _first_frame = true;
		int64_t _dts_offset = 0;
		int64_t _prev_dts = -1;
		uint32_t _wrap_count = 0;
	};
}