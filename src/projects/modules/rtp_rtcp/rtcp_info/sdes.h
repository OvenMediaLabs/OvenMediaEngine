#pragma once
#include "base/ovlibrary/ovlibrary.h"
#include "rtcp_info.h"
#include "../rtcp_packet.h"
#include "sdes_chunk.h"


//         0                   1                   2                   3
//         0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// header |V=2|P|    SC   |  PT=SDES=202  |             length            |
//        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// chunk  |                          SSRC/CSRC_1                          |
//   1    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//        |                           SDES items                          |
//        |                              ...                              |
//        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// chunk  |                          SSRC/CSRC_2                          |
//   2    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//        |                           SDES items                          |
//        |                              ...                              |
//        +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

class Sdes : public RtcpInfo
{
public:
	///////////////////////////////////////////
	// Implement RtcpInfo virtual functions
	///////////////////////////////////////////
	bool Parse(const RtcpPacket &packet) override;
	// RtcpInfo must provide raw data
	std::shared_ptr<ov::Data> GetData() const override;
	void DebugPrint() override;

	// RtcpInfo must provide packet type
	RtcpPacketType GetPacketType() const override
	{
		return RtcpPacketType::SDES;
	}

	uint8_t GetCountOrFmt() const override
	{
		return static_cast<uint8_t>(GetChunkCount());
	}

	bool HasPadding() const override
	{
		return false;
	}

	size_t GetChunkCount() const
	{
		return _sdes_chunk_list.size();
	}

	bool AddChunk(const std::shared_ptr<SdesChunk> &chunk)
	{
		_sdes_chunk_list.push_back(chunk);
		return true;
	}

	// Get Chunk list
	const std::vector<std::shared_ptr<SdesChunk>> &GetChunks() const
	{
		return _sdes_chunk_list;
	}

	// Get Chunk by type
	std::shared_ptr<SdesChunk> GetChunk(SdesChunk::Type type) const
	{
		for (auto &chunk : _sdes_chunk_list)
		{
			if (chunk->GetType() == type)
			{
				return chunk;
			}
		}

		return nullptr;
	}

private:
	std::vector<std::shared_ptr<SdesChunk>>	_sdes_chunk_list;
};