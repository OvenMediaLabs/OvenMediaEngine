#pragma once

#include <base/common_types.h>
#include <base/publisher/stream.h>
#include <modules/ovt_packetizer/ovt_packetizer.h>

#include "monitoring/monitoring.h"

class OvtStream final : public pub::Stream, public OvtPacketizerInterface
{
public:
	static std::shared_ptr<OvtStream> Create(const std::shared_ptr<pub::Application> application,
											 const info::Stream &info,
											 uint32_t worker_count);
	explicit OvtStream(const std::shared_ptr<pub::Application> application,
					   const info::Stream &info,
					   uint32_t worker_count);
	~OvtStream() final;

	void SendVideoFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void SendAudioFrame(const std::shared_ptr<MediaPacket> &media_packet) override;
	void SendDataFrame(const std::shared_ptr<MediaPacket> &media_packet) override {} // Not supported yet
	

	bool OnOvtPacketized(std::shared_ptr<OvtPacket> &packet) override;

	bool RemoveSessionByConnectorId(int connector_id);

	bool GetDescription(Json::Value &description);

private:
	bool Start() override;
	bool Stop() override;

	bool GenerateDescription();

	uint32_t							_worker_count = 0;

	Json::Value							_description;
	std::shared_mutex					_packetizer_lock;
	std::shared_ptr<OvtPacketizer>		_packetizer;
};
