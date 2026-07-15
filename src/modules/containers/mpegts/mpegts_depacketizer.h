//==============================================================================
//
//  MPEGTS Depacketizer
//
//  Created by Getroot
//  Copyright (c) 2020 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/mediarouter/media_type.h>
#include <base/info/media_track.h>

#include <set>

#include "mpegts_common.h"
#include "mpegts_packet.h"
#include "mpegts_section.h"
#include "mpegts_pes.h"
#include "mpegts_datagram_reorder_buffer.h"

/*  PES Depacketization Process

	Packet 1: [TS Header][Adaptation field][PES Header |    Payload     ] : uint_starting_indicator = 1
	Packet 2: [TS Header][                    Payload                   ]
	Packet 3: [TS Header][Adaptation field : stuffing[    Payload       ]

	Packet 1: [TS Header][Adaptation field][PES Header |    Payload     ] : uint_starting_indicator = 1
	(Assemble packet 1,2,3)
	(Create New ES 1)
*/

namespace mpegts
{
	enum class PacketType : uint8_t
	{
		UNKNOWN = 0,
		SUPPORTED_SECTION = 1,
		UNSUPPORTED_SECTION = 2,
		PES = 3,
		SECTION = 4
	};

	class MpegTsDepacketizer
	{
	public:
		MpegTsDepacketizer();
		~MpegTsDepacketizer();

		bool AddPacket(const std::shared_ptr<const ov::Data> &packet);
		bool AddPacket(const std::shared_ptr<Packet> &packet);

		// Enable UDP datagram reordering. Must be called before feeding data.
		// Only meaningful for datagram-framed (UDP) input.
		void EnablePacketReordering();

		// Drains any datagrams the reorder buffer is still holding behind a gap into the byte parser,
		// so their frames become available. Used on stream teardown so buffered, already-received
		// datagrams are not dropped. No-op when reordering is disabled.
		void FlushReorderBuffer();

		// Sets the owning stream's name path; it is prefixed on every log line from this depacketizer.
		void SetNamePath(const ov::String &name_path)
		{
			_name_path = name_path;
		}

		bool IsTrackInfoAvailable();
		bool IsESAvailable();
		bool IsSectionAvailable();
		
		const std::shared_ptr<PAT> GetFirstPAT();
		const std::shared_ptr<PAT> GetPAT(uint8_t program_number);
		bool GetPMTList(uint16_t program_num, std::vector<std::shared_ptr<Section>> *pmt_list);
		bool GetTrackList(std::map<uint16_t, std::shared_ptr<MediaTrack>> *track_list);

		const std::shared_ptr<Pes> PopES();
		const std::shared_ptr<Section> PopSection();
	private:
		// Feeds one contiguous run of TS packets into the byte parser (with sync-byte resync).
		bool ProcessDatagram(const std::shared_ptr<const ov::Data> &datagram);
		// Finds the next confirmed packet boundary in `_buffer`, or 0 if none can be confirmed yet.
		size_t FindResyncOffset() const;

		// Emits a rate-limited, count-aggregated continuity-break warning (see `_cc_break_*`).
		void LogContinuityBreak();

		PacketType GetPacketType(const std::shared_ptr<Packet> &packet);

		bool ParseSection(const std::shared_ptr<Packet> &packet);
		bool ParsePes(const std::shared_ptr<Packet> &packet);

		// Drops any in-progress assembly for a PID after a continuity discontinuity.
		void DiscardPesDraft(uint16_t pid);
		void DiscardSectionDraft(uint16_t pid);

		const std::shared_ptr<Section> GetSectionDraft(uint16_t pid);
		// incompleted section will be inserted
		bool SaveSectionDraft(const std::shared_ptr<Section> &section);
		// process completed section and remove, extract a table
		bool CompleteSection(const std::shared_ptr<Section> &section);

		const std::shared_ptr<Pes> GetPesDraft(uint16_t pid);	
		// incompleted section will be inserted
		bool SavePesDraft(const std::shared_ptr<Pes> &pes);
		// process completed section and remove, extract a elementary stream (es)
		bool CompletePes(const std::shared_ptr<Pes> &pes);

		bool CreateTracks();
		bool ExtractH264TrackInfo(const std::shared_ptr<Pes> &pes);
		bool ExtractAACTrackInfo(const std::shared_ptr<Pes> &pes);
		
		// PID : Section
		std::shared_mutex _section_draft_map_lock;
		std::map<uint16_t, std::shared_ptr<Section>> _section_draft_map;
		// PID : PES
		// there is only one pes saved per pid
		std::shared_mutex _pes_draft_map_lock;
		std::map<uint16_t, std::shared_ptr<Pes>> _pes_draft_map;

		// NOTE: unlike the draft maps above (each self-locked), the members below have no internal lock.
		// They are protected by the owner's lock: the `MpegTsDepacketizer` instance is
		// `OV_GUARDED_BY(_depacketizer_lock)` in `MpegTsStream`, and every method that touches them
		// (`AddPacket`/`ProcessDatagram`/`EnablePacketReordering`/`SetNamePath`) runs with that lock held.
		// Any new accessor must also hold it.

		// True once the byte parser is locked to the 188-byte packet grid
		// (a packet parsed or a boundary was confirmed by resync);
		// makes an aligned-but-corrupt packet drop one packet instead of triggering a resync scan.
		bool _synced = false;

		// PID : Last continuity counter
		std::map<uint16_t, uint8_t> _last_continuity_counter_map;
		// PIDs for which a legal single duplicate has already been consumed (a second
		// consecutive same-counter packet is then treated as a continuity error).
		std::set<uint16_t> _cc_duplicate_seen;

		// Owning stream's name path, prefixed on every log line (set via `SetNamePath()`).
		ov::String _name_path{"?"};
		// Cached at construction: whether debug logging is enabled for this tag, so the per-packet
		// continuity-break detail is skipped entirely (no formatting) when debug is off.
		bool _debug_enabled											= false;
		// Continuity-break warnings are rate-limited to one line per this interval, with an aggregated count.
		static constexpr int64_t MPEGTS_CC_BREAK_WARN_INTERVAL_MSEC = 5000;
		int64_t _cc_break_last_warn_msec							= 0;
		uint64_t _cc_break_count									= 0;

		// PAT
		bool _pat_list_completed = false;
		// program number + packet identifier list
		// Program number : PAT Section
		std::map<uint8_t, std::shared_ptr<Section>> _pat_map;

		// PMT
		bool _pmt_list_completed = false;
		// Program Num : PMT Section
		std::vector<uint16_t> _completed_pmt_list;
		// PID, ES INFO
		std::multimap<uint16_t, std::shared_ptr<Section>> _pmt_map;

		// ES Info : from PMT
		std::map<uint16_t, std::shared_ptr<ESInfo>> _es_info_map;

		// extract from es and combine with es_info to make MediaTrack
		bool _track_list_completed = false;
		std::map<uint16_t, std::shared_ptr<MediaTrack>> _media_tracks;
		
		std::shared_mutex _es_list_lock;
		std::queue<std::shared_ptr<Pes>> _es_list;
		
		std::shared_mutex _section_list_lock;
		std::queue<std::shared_ptr<Section>> _section_list;

		// PMT, PES quickly search PMT, PES
		// PID : PacketType 
		// PMT's PID comes from PAT
		// PES's PID comes from PMT/ES_INFO
		std::map<uint16_t, PacketType>	_packet_type_table;

		std::shared_ptr<ov::Data> _buffer = std::make_shared<ov::Data>();

		// UDP datagram reordering (disabled by default; enabled via `EnablePacketReordering()`).
		std::unique_ptr<DatagramReorderBuffer> _reorder_buffer;
	};
}