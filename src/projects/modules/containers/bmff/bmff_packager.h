//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2022 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>

#include "sample.h"
#include "sample_buffer.h"
#include "cenc.h"

#define BMFF_BOX_HEADER_SIZE (8)		// size(4) + type(4)
#define BMFF_FULL_BOX_HEADER_SIZE (12)	// size(4) + type(4) + version(1) + flag(3)

namespace bmff
{
	class Packager
	{
	public:
		Packager(const std::shared_ptr<const MediaTrack> &media_track, const std::shared_ptr<const MediaTrack> &data_track, const CencProperty &cenc_property);

	protected:
		// Get track 
		const std::shared_ptr<const MediaTrack> &GetMediaTrack() const;
		const std::shared_ptr<const MediaTrack> &GetDataTrack() const;

		// Fytp Box
		virtual bool WriteFtypBox(ov::ByteStream &container_stream);
		// Moov Box Group
		virtual bool WriteMoovBox(ov::ByteStream &container_stream);
		virtual bool WriteMvhdBox(ov::ByteStream &container_stream);
		virtual bool WriteTrakBox(ov::ByteStream &container_stream);
		virtual bool WriteTkhdBox(ov::ByteStream &container_stream);
		virtual bool WriteMdiaBox(ov::ByteStream &container_stream);
		virtual bool WriteMdhdBox(ov::ByteStream &container_stream);
		virtual bool WriteHdlrBox(ov::ByteStream &container_stream);
		virtual bool WriteMinfBox(ov::ByteStream &container_stream);
		virtual bool WriteVmhdBox(ov::ByteStream &container_stream);
		virtual bool WriteSmhdBox(ov::ByteStream &container_stream);
		virtual bool WriteDinfBox(ov::ByteStream &container_stream);
		virtual bool WriteDrefBox(ov::ByteStream &container_stream);
		virtual bool  WriteUrlBox(ov::ByteStream &container_stream);
		virtual bool WriteStblBox(ov::ByteStream &container_stream);
		virtual bool WriteStsdBox(ov::ByteStream &container_stream);

		virtual bool WriteSampleEntry(ov::ByteStream &container_stream);
		virtual bool WriteVisualSampleEntry(ov::ByteStream &container_stream);
		virtual bool WriteAvc1Box(ov::ByteStream &container_stream);
		virtual bool WriteAvccBox(ov::ByteStream &container_stream);
		virtual bool WriteHvc1Box(ov::ByteStream &container_stream);
		virtual bool WriteHvccBox(ov::ByteStream &container_stream);

		virtual bool WriteMp4aBox(ov::ByteStream &container_stream);
		virtual bool WriteEsdsBox(ov::ByteStream &container_stream);
		
		virtual bool WriteESDescriptor(ov::ByteStream &container_stream);
		virtual bool WriteDecoderConfigDescriptor(ov::ByteStream &container_stream);
		virtual bool WriteAudioSpecificInfo(ov::ByteStream &container_stream);

		virtual bool WriteSLConfigDescriptor(ov::ByteStream &container_stream);

		virtual bool WriteSttsBox(ov::ByteStream &container_stream);
		virtual bool WriteStscBox(ov::ByteStream &container_stream);
		virtual bool WriteStszBox(ov::ByteStream &container_stream);
		virtual bool WriteStcoBox(ov::ByteStream &container_stream);

		virtual bool WriteMvexBox(ov::ByteStream &container_stream);
		virtual bool WriteTrexBox(ov::ByteStream &container_stream);

		// CENC
		virtual bool WriteSinfBox(ov::ByteStream &container_stream, const ov::String &oroginal_format);
		virtual bool WriteFrmaBox(ov::ByteStream &container_stream, const ov::String &oroginal_format);
		virtual bool WriteSchmBox(ov::ByteStream &container_stream);
		virtual bool WriteSchiBox(ov::ByteStream &container_stream);
		virtual bool WriteTencBox(ov::ByteStream &container_stream);

		virtual bool WriteSaizBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);
		virtual bool WriteSaioBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);
		virtual bool WriteSencBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);

		// Emsg Box
		virtual bool WriteEmsgBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);

		// Moof Box Group
		virtual bool WriteMoofBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);
		virtual bool WriteMfhdBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);
		virtual bool WriteTrafBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);
		virtual bool WriteTfhdBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);
		virtual bool WriteTfdtBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);
		virtual bool WriteTrunBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);
		virtual bool GetSampleFlags(const std::shared_ptr<const MediaPacket> &sample, uint32_t &flags);

		virtual bool WriteMdatBox(ov::ByteStream &container_stream, const std::shared_ptr<const Samples> &samples);

		// Write BaseDescriptor
		bool WriteBaseDescriptor(ov::ByteStream &stream, uint8_t tag, const ov::Data &data);
		// Write Box
		bool WriteBox(ov::ByteStream &stream, const ov::String &box_name, const ov::Data &box_data);
		// Write Full Box
		bool WriteFullBox(ov::ByteStream &stream, const ov::String &box_name, const ov::Data &box_data, uint8_t version, uint32_t flags);

		SampleBuffer _sample_buffer;
		
	private:
		std::shared_ptr<const MediaTrack> _media_track = nullptr;
		std::shared_ptr<const MediaTrack> _data_track = nullptr;
		CencProperty _cenc_property;

		uint32_t _sequence_number = 1; // For Mfhd Box

		size_t _traf_box_offset_in_moof = 0;

		size_t _trun_box_offset_in_traf = 0;
		size_t _offset_field_offset_in_trun = 0;

		size_t _saio_box_offset_in_traf = 0;
		size_t _offset_field_offset_in_saio = 0;

		size_t _senc_box_offset_in_traf = 0;
		size_t _senc_data_offset_in_senc = 0;
	};
}