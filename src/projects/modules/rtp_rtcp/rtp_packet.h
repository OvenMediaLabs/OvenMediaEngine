#pragma once

#include <vector>
#include <memory>
#include <base/ovlibrary/ovlibrary.h>
#include "rtp_header_extension/rtp_header_extensions.h"

#define RTP_VERSION					2
#define FIXED_HEADER_SIZE			12
#define RED_HEADER_SIZE				1
#define EXTENSION_HEADER_SIZE		4
#define ONE_BYTE_EXTENSION_ID		0xBEDE
#define RTP_DEFAULT_MAX_PACKET_SIZE	1472

//  0                   1                   2                   3
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |V=2|P|X|  CC   |M|     PT      |       sequence number         |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                           timestamp                           |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |           synchronization source (SSRC) identifier            |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |            Contributing source (CSRC) identifiers             |
// |                             ....                              |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |One-byte eXtensions id = 0xbede|       length in 32bits        |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |                          Extensions                           |
// |                             ....                              |
// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
// |RED(Optional)|            Payload                             |
// |             ....              :  padding...                   |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// |               padding         | Padding size  |
// +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

class RtpPacket
{
public:
	RtpPacket();
	RtpPacket(const std::shared_ptr<const ov::Data> &data);
	RtpPacket(const RtpPacket &src);
	virtual ~RtpPacket();

	// Parse from Data
	bool		Parse(const std::shared_ptr<const ov::Data> &data);

	// Getter
	bool		Marker() const;
	uint8_t		PayloadType() const;
	// For FEC Payload
	bool		IsUlpfec() const;
	uint8_t 	OriginPayloadType() const;
	uint16_t	SequenceNumber() const;
	uint32_t	Timestamp() const;
	uint32_t	Ssrc() const;
	std::vector<uint32_t> Csrcs() const;
	std::map<uint8_t, ov::Data> Extensions() const;
	std::optional<ov::Data>	GetExtension(uint8_t id) const;

	// GetExtension as template
	template <typename T>
	std::optional<T> GetExtension(uint8_t id) const
	{
		auto extension = GetExtension(id);
		if (extension.has_value() == false)
		{
			return std::nullopt;
		}

		auto data = extension.value();
		if (data.GetLength() < sizeof(T))
		{
			return std::nullopt;
		}

		return ByteReader<T>::ReadBigEndian(data.GetDataAs<uint8_t>());
	}

	uint8_t*	Buffer() const;

	// Setter
	void		SetMarker(bool marker_bit);
	void		SetPayloadType(uint8_t payload_type);
	// For FEC Payload
	void 		SetUlpfec(bool is_fec, uint8_t origin_payload_type);
	void		SetSequenceNumber(uint16_t seq_no);
	void		SetTimestamp(uint32_t timestamp);
	void		SetSsrc(uint32_t ssrc);
	
	// must call before setting extentions, payload, padding
	void		SetCsrcs(const std::vector<uint32_t>& csrcs);
	void		SetExtensions(const RtpHeaderExtensions& extensions);

	size_t		HeadersSize() const;
	size_t		PayloadSize() const;
	size_t		PaddingSize() const;
	size_t 		ExtensionSize() const;

	// Payload
	bool 		SetPayload(const uint8_t *payload, size_t payload_size);
	uint8_t*	SetPayloadSize(size_t size_bytes);
	uint8_t*	AllocatePayload(size_t size_bytes);
	uint8_t*	Header() const;
	uint8_t*	Payload() const;
	uint8_t* 	Extension(uint8_t id) const;

	// Data
	std::shared_ptr<ov::Data> GetData() const;
	size_t GetDataLength() const;

	// Created time
	std::chrono::system_clock::time_point GetCreatedTime();

	// Dump print
	ov::String	Dump();

	// Extensions for OME specific
	void		SetTrackId(uint32_t track_id) { _track_id = track_id; }
	uint32_t	GetTrackId() const { return _track_id; }

	void		SetNTPTimestamp(uint64_t nts) {_ntp_timestamp = nts;}
	uint64_t	NTPTimestamp() const {return _ntp_timestamp;}

	void		SetVideoPacket(bool flag) {_is_video_packet = flag;}
	bool		IsVideoPacket() const {return _is_video_packet;}

	void		SetKeyframe(bool flag) {_is_keyframe = flag;}
	bool		IsKeyframe() const {return _is_keyframe;}

	void		SetFirstPacketOfFrame(bool flag) {_is_first_packet_of_frame = flag;}
	bool		IsFirstPacketOfFrame() const {return _is_first_packet_of_frame;}

	void		SetRtspChannel(uint32_t rtsp_channel) {_rtsp_channel = rtsp_channel;}
	uint32_t	GetRtspChannel() const {return _rtsp_channel;}

	// Get Extension Type
	RtpHeaderExtension::HeaderType GetExtensionType() const { return _extension_type; }

protected:
	size_t		_payload_offset = 0;	// Payload Start Point (Header size)
	bool		_has_padding = false;
	bool		_has_extension = false;
	uint8_t		_cc = 0;
	bool		_marker = false;
	uint8_t		_payload_type = 0;
	bool		_is_fec = false;
	uint8_t 	_origin_payload_type = 0;
	uint8_t		_padding_size = 0;
	uint16_t	_sequence_number = 0;
	uint32_t	_timestamp = 0;
	uint32_t	_ssrc = 0;
	size_t		_payload_size = 0;		// Payload Size
	size_t		_extension_size;
	std::map<uint8_t, ov::Data> _extensions;

	// extension ID : data offset
	RtpHeaderExtension::HeaderType _extension_type;
	std::map<uint8_t, off_t> _extension_buffer_offset;

	bool		_is_available = false;

	// std::vector<uint8_t>	_buffer;
	uint8_t *					_buffer = nullptr;
	std::shared_ptr<ov::Data>	_data = nullptr;

	// created time
	std::chrono::system_clock::time_point _created_time;

	// Extensions for OME specific
	uint32_t	_track_id = 0;
	uint64_t	_ntp_timestamp = 0;
	bool		_is_video_packet = false;
	bool		_is_keyframe = false;
	bool		_is_first_packet_of_frame = false;

	uint32_t	_rtsp_channel = 0; // If it is from RTSP, _rtsp_channel is valid
};

