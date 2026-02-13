//==============================================================================
//
//  MPEGTS Packet
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================

#pragma once

#include <vector>
#include <memory>
#include <base/ovlibrary/ovlibrary.h>
#include <base/ovlibrary/bit_reader.h>

//
// TS (Transport Stream) header structure (ISO13818-1)
//
// sync byte: 8 bits
// transport error indicator: 1 bit
// payload unit start indicator: 1 bit
// transport priority: 1 bit
// PID (packet identifier): 13 bits
// transport scrambling control: 2 bits
// adaptation field control: 2 bits
// continuity counter: 4 bits
// adaptation field: (variable)
//     adaptation field length: 8 bits
//     discontinuity indicator: 1 bit
//     random access indicator: 1 bit
//     elementary stream priority indicator: 1 bit
//     5 flags (*): 5 bits
//     optional fields (*)
//         PCR: 42 bits (actually, it's 48 (33 + 6 + 9) bytes)
//         OPCR: 42 bits (actually, it's 48 (33 + 6 + 9) bytes)
//         splice countdown: 8 bits
//         transport private data length: 8 bits
//         transport private data: (variable)
//         adaptation field extension length: 8 bits
//         3 flags: 3 bits (**)
//         optional fields (**)
//             ltw_valid flag: 1 bit
//             ltw offset: 15 bits
//             -: 2 bits
//             piecewise rate: 22 bits
//             splice type: 4 bits
//             DTS_next_au: 33 bits
// ...
//
// (Total: 188 byte)

namespace mpegts
{
	
	// MPEGTS Packet's length must be 188, 192 or 204
	constexpr uint8_t MPEGTS_MIN_PACKET_SIZE = 188;
	constexpr uint8_t MPEGTS_HEADER_SIZE = 4;
	constexpr uint8_t MPEGTS_SYNC_BYTE = 0x47;

	enum class WellKnownPacketId : uint16_t
	{
		PAT = 0x0000,
		CAT = 0x0001,
		TSDT = 0x0002,
		NIT = 0x0010,
		SDT = 0x0011, 
		// ... 
		NULL_PACKET = 0x1FFF,	 // used for fixed bandwidth padding
	};

	struct AdaptationField
	{
		// adaptation field length
		uint8_t _length = 0U;

		// indicators
		bool _discontinuity_indicator = false;
		bool _random_access_indicator = false;
		bool _elementary_stream_priority_indicator = false;

		// 5 flags
		bool _pcr_flag = false;
		bool _opcr_flag = false;
		bool _splicing_point_flag = false;
		bool _transport_private_data_flag = false;
		bool _adaptation_field_extension_flag = false;

		// Program Clock Reference
		struct PCR
		{
			uint64_t _base = 0U;  // 33 bits, 90kHz 
			uint8_t _reserved = 0U;	// 6 bits
			uint16_t _extension = 0U;  // 9 bits, 27MHz 
		} _pcr;

		// Original Program Clock Reference
		struct OPCR
		{
			uint64_t _base = 0U;  // 33 bits
			// uint8_t reserved = 0U;	// 6 bits
			uint16_t _extension = 0U;  // 9 bits
		} _opcr;

		// Splice countdown
		uint8_t _splice_countdown = 0U;  // 8 bits

		// Transport private data
		struct TransportPrivateData
		{
			uint8_t _length = 0U;  	// 8 bits
			uint8_t* _data = nullptr;
		} _transport_private_data;

		// Adaptation field extension
		struct Extension
		{
			uint8_t _length = 0U;  // 8 bits
			bool _ltw_flag = false;
			bool _piecewise_rate_flag = false;
			bool _seamless_splice_flag = false;
			// uint8_t reserved = 0U;  // 5 bits

			struct LTW
			{
				bool _valid_flag = false;
				uint16_t _offset = 0U;  // 15 bits
			} _ltw;

			struct PiecewiseRate
			{
				// uint8_t reserved = 0U;  // 2 bits
				uint32_t _piecewise_rate;  // 22 bits
			} _piecewise_rate;

			struct SeamlessSplice
			{
				uint8_t _splice_type = 0U;  // 4 bits
				// DTS_next_AU[32..30]
				uint8_t _dts_next_au0 = 0U;  // 3 bits
				uint8_t _marker_bit0 = 0U;   // 1 bit
				// DTS_next_AU[29..15]
				uint8_t _dts_next_au1 = 0U;  // 15 bits
				uint8_t _marker_bit1 = 0U;   // 1 bit
				// DTS_next_AU[14..0]
				uint8_t _dts_next_au2 = 0U;  // 15 bits
				uint8_t _marker_bit2 = 0U;   // 1 bit
			} _seamless_splice;
		} _extension;

		uint32_t _stuffing_bytes = 0U;
	};

	class Section;
	class Pes;
	class Packet
	{
	public:
		Packet();
		Packet(const std::shared_ptr<ov::Data> &data);
		virtual ~Packet();

		//Note: Now, it only supports 188 bytes of mpegts packet
		// It returns parsed data length
		// If parsing is failed, it returns 0
		uint32_t Parse();

		static std::shared_ptr<Packet> Build(const std::shared_ptr<Section> &section, uint8_t continuity_counter);
		static std::vector<std::shared_ptr<Packet>> Build(const std::shared_ptr<Pes> &pes, bool has_pcr, uint8_t continuity_counter);

		// Getter
		uint8_t SyncByte();
		bool TransportErrorIndicator();
		bool PayloadUnitStartIndicator();
		uint16_t PacketIdentifier();
		uint8_t TransportScramblingControl();
		uint8_t AdaptationFieldControl();
		bool HasAdaptationField();
		bool HasPayload();
		uint8_t ContinuityCounter();

		// Setter
		void SetContinuityCounter(uint8_t continuity_counter);

		const AdaptationField &GetAdaptationField();

		const uint8_t* Payload();
		size_t PayloadLength();

		std::shared_ptr<const ov::Data> GetData();
		size_t GetDataLength();

		ov::String ToDebugString() const;

	private:
		bool ParseAdaptationHeader();
		bool ParsePayload();
		void UpdateData();

		uint8_t _packet_size = MPEGTS_MIN_PACKET_SIZE;	// at this time, it only supports for 188 bytes packet

		uint8_t _sync_byte = 0x47;					 // 8 bits
		bool _transport_error_indicator = false;	 // 1 bit
		bool _payload_unit_start_indicator = false;	 // 1 bit
		uint8_t _transport_priority = 0U;			 // 1 bit

		// Value				Description
		// 0x0000				Program Association Table
		// 0x0001				Conditional Access Table
		// 0x0002				Transport Stream Description Table
		// 0x0003-0x000F		Reserved
		// 0x00010...0x1FFE		May be assigned as network_PID, Program_map_PID, elementary_PID, or for other purposes
		// 0x1FFF				Null packet
		//
		// NOTE - The transport packets with PID values 0x0000, 0x0001, and 0x0010-0x1FFE are allowed to carry a PCR.
		uint16_t _packet_identifier = 0U;  // 13 bits
		// 00	Not scrambled
		// 01	User-defined
		// 10	User-defined
		// 11	User-defined
		uint8_t _transport_scrambling_control = 0U;  // 2 bits
		// 00	Reserved for future use by ISO/IEC
		// 01	No adaptation_field, payload only
		// 10	Adaptation_field only, no payload
		// 11	Adaptation_field followed by payload
		uint8_t _adaptation_field_control = 0U;  // 2 bits
		uint8_t _continuity_counter = 0U;		// 4 bits

		AdaptationField	_adaptation_field;
		size_t			_adaptation_field_size = 0U;

		std::shared_ptr<BitReader>	_ts_parser = nullptr;
		const uint8_t *				_buffer = nullptr;
		
		// Before UpdateData(), it will be used in UpdateData()
		std::shared_ptr<ov::Data>	_payload_data = nullptr;

		std::shared_ptr<ov::Data>	_data = nullptr;
		const uint8_t *				_payload = nullptr;
		size_t						_payload_length = 0;

		bool _need_to_update_data = false;
	};
}
