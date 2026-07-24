//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/ovcrypto/aes.h>
#include <base/ovlibrary/constexpr_utilities.h>
#include <base/ovlibrary/hex.h>
#include <base/ovlibrary/ovlibrary.h>

#include "sample.h"

namespace bmff
{
	constexpr uint8_t AES_BLOCK_SIZE				 = 16;

	// Canonical DRM System IDs as published in the DASH-IF Content Protection registry (dashed UUID form).
	// The undashed hex form embedded in the pssh box binary is derived from these at compile time,
	// so each identifier is declared exactly once.
	inline constexpr char SYSTEM_ID_WIDEVINE_UUID[]	 = "edef8ba9-79d6-4ace-a3c8-27dcd51d21ed";
	inline constexpr char SYSTEM_ID_FAIRPLAY_UUID[]	 = "94ce86fb-07ff-4f43-adb8-93d2fa968ca2";
	inline constexpr char SYSTEM_ID_PLAYREADY_UUID[] = "9a04f079-9840-4286-ab92-e65be0885f95";

	// Codecs the CENC implementation can encrypt
	inline bool IsCencSupportedCodec(cmn::MediaCodecId codec_id)
	{
		switch(codec_id)
		{
			case cmn::MediaCodecId::H264:
				return true;
			case cmn::MediaCodecId::Aac:
				return true;
			default:
				return false;
		}
	}

	namespace internal
	{
		inline constexpr auto SYSTEM_ID_WIDEVINE_HEX_BUF  = ov::cexpr::StrRemove(SYSTEM_ID_WIDEVINE_UUID, '-');
		inline constexpr auto SYSTEM_ID_FAIRPLAY_HEX_BUF  = ov::cexpr::StrRemove(SYSTEM_ID_FAIRPLAY_UUID, '-');
		inline constexpr auto SYSTEM_ID_PLAYREADY_HEX_BUF = ov::cexpr::StrRemove(SYSTEM_ID_PLAYREADY_UUID, '-');
	}  // namespace internal

	inline constexpr const char *SYSTEM_ID_WIDEVINE_HEX	 = internal::SYSTEM_ID_WIDEVINE_HEX_BUF.CStr();
	inline constexpr const char *SYSTEM_ID_FAIRPLAY_HEX	 = internal::SYSTEM_ID_FAIRPLAY_HEX_BUF.CStr();
	inline constexpr const char *SYSTEM_ID_PLAYREADY_HEX = internal::SYSTEM_ID_PLAYREADY_HEX_BUF.CStr();

	enum class CencProtectScheme : uint8_t
	{
		None,
		Cenc,
		Cbcs
	};

	constexpr const char *CencProtectSchemeToString(CencProtectScheme scheme)
	{
		switch (scheme)
		{
			OV_CASE_RETURN(CencProtectScheme::Cenc, "cenc");
			OV_CASE_RETURN(CencProtectScheme::Cbcs, "cbcs");
			OV_CASE_RETURN(CencProtectScheme::None, "none");
		}

		OV_ASSERT(false, "Invalid CencProtectScheme: %d", ov::ToUnderlyingType(scheme));
		return "none";
	}

	enum class CencEncryptMode : uint8_t
	{
		None,
		Ctr,
		Cbc
	};

	constexpr const char *CencEncryptModeToString(CencEncryptMode mode)
	{
		switch (mode)
		{
			OV_CASE_RETURN(CencEncryptMode::Ctr, "ctr");
			OV_CASE_RETURN(CencEncryptMode::Cbc, "cbc");
			OV_CASE_RETURN(CencEncryptMode::None, "none");
		}

		OV_ASSERT(false, "Invalid CencEncryptMode: %d", ov::ToUnderlyingType(mode));
		return "none";
	}

	enum class DRMSystem : uint8_t
	{
		None	  = 0,
		Widevine  = 1,
		FairPlay  = 2,
		PlayReady = 4,

		All		  = Widevine | FairPlay | PlayReady
	};

	// `DRMSystem` is used as a bit flag so that multiple systems can be combined.
	constexpr inline DRMSystem operator|(DRMSystem a, DRMSystem b)
	{
		return static_cast<DRMSystem>(ov::ToUnderlyingType(a) | ov::ToUnderlyingType(b));
	}

	constexpr inline bool HasDRMSystem(DRMSystem set, DRMSystem flag)
	{
		const auto flag_value = ov::ToUnderlyingType(flag);

		return (flag_value != 0) && ((ov::ToUnderlyingType(set) & flag_value) == flag_value);
	}

	struct PsshBox
	{
	public:
		PsshBox(const std::shared_ptr<ov::Data> &data)
			: pssh_box_data(data)
		{
			// ISO/IEC 23001-7 8.1
			// aligned(8) class ProtectionSystemSpecificHeaderBox extends FullBox('pssh', version, flags = 0)
			// {
			// 	unsigned int(8)[16] SystemID;
			// 	if (version > 0)
			// 	{
			// 		unsigned int(32) KID_count;
			// 		{
			// 			unsigned int(8)[16] KID;
			// 		}
			// 		[KID_count];
			// 	}
			// 	unsigned int(32) DataSize;
			// 	unsigned int(8)[DataSize] Data;
			// }

			// A valid pssh box needs at least size(4) + 'pssh'(4) + version/flags(4) + SystemID(16) = 28 bytes.
			// Bail out on a malformed/truncated box so the header reads below cannot crash.
			if ((data == nullptr) || (data->GetLength() < 28))
			{
				loge("BMFF.CENC", "Malformed pssh box: too small to hold the header and SystemID");
				return;
			}

			// Parse pssh box
			ov::ByteStream stream(data);

			[[maybe_unused]] auto box_size = stream.ReadBE32();
			[[maybe_unused]] auto box_name = stream.GetRemainData(4);
			stream.Skip<uint8_t>(4);
			[[maybe_unused]] auto version = stream.Read8();
			[[maybe_unused]] auto flags	  = stream.ReadBE24();

			system_id					  = stream.GetRemainData(16)->Clone();

			auto system_id_hex			  = system_id->ToHexString().LowerCaseString();

			logt("DEBUG", "System ID : %s", system_id_hex.CStr());

			if (system_id_hex == SYSTEM_ID_WIDEVINE_HEX)
			{
				drm_system = DRMSystem::Widevine;
			}
			else if (system_id_hex == SYSTEM_ID_FAIRPLAY_HEX)
			{
				drm_system = DRMSystem::FairPlay;
			}
			else if (system_id_hex == SYSTEM_ID_PLAYREADY_HEX)
			{
				drm_system = DRMSystem::PlayReady;

				// Extract the PlayReady Object (PRO) from the pssh Data field.
				// HLS signaling carries the PRO (not the whole pssh box) in the `EXT-X-KEY` URI.
				// Every read below is bounds-checked so a malformed/truncated box cannot crash the parser.
				stream.Skip<uint8_t>(16);  // advance past the 16-byte SystemID

				bool valid = true;

				if (version > 0)
				{
					// For `version > 0` (only version 1 is defined), `KID_count` (4 bytes) is followed by `KID_count * 16` bytes of KIDs.
					if (stream.IsRemained(sizeof(uint32_t)))
					{
						const auto kid_count	 = stream.ReadBE32();
						const auto kid_list_size = static_cast<size_t>(kid_count) * 16;

						valid					 = stream.IsRemained(kid_list_size);
						if (valid)
						{
							stream.Skip<uint8_t>(kid_list_size);
						}
					}
					else
					{
						valid = false;
					}
				}

				// DataSize (4 bytes) followed by the PRO payload.
				if (valid && stream.IsRemained(sizeof(uint32_t)))
				{
					const auto data_size = stream.ReadBE32();

					if (stream.IsRemained(data_size))
					{
						// Note: the constructor parameter is also named 'data', so qualify the member with this->
						this->data = stream.GetRemainData(data_size)->Clone();
					}
				}

				if ((this->data == nullptr) || (this->data->GetLength() == 0))
				{
					// Malformed PlayReady `pssh` box: treat it as an unknown/unusable system.
					// An empty PRO is as unusable as a missing one, so reject a zero-length payload too.
					loge("BMFF.CENC", "Malformed PlayReady pssh box, could not extract the PlayReady Object");
					drm_system = DRMSystem::None;
				}
			}
		}

		PsshBox(ov::String system_id_hex, std::vector<std::shared_ptr<ov::Data>> kid_hex_list, const std::shared_ptr<ov::Data> &user_data)
		{
			// ISO/IEC 23001-7 8.1
			// aligned(8) class ProtectionSystemSpecificHeaderBox extends FullBox('pssh', version, flags = 0)
			// {
			// 	unsigned int(8)[16] SystemID;
			// 	if (version > 0)
			// 	{
			// 		unsigned int(32) KID_count;
			// 		{
			// 			unsigned int(8)[16] KID;
			// 		}
			// 		[KID_count];
			// 	}
			// 	unsigned int(32) DataSize;
			// 	unsigned int(8)[DataSize] Data;
			// }

			// remove dashes from system id hex string
			system_id_hex = system_id_hex.Replace("-", "");

			if (system_id_hex.GetLength() != 32)
			{
				loge("ERROR", "Invalid system id hex string : %s", system_id_hex.CStr());
				return;
			}

			auto system_id_hex_lower = system_id_hex.LowerCaseString();

			if (system_id_hex_lower == SYSTEM_ID_WIDEVINE_HEX)
			{
				drm_system = DRMSystem::Widevine;
			}
			else if (system_id_hex_lower == SYSTEM_ID_FAIRPLAY_HEX)
			{
				drm_system = DRMSystem::FairPlay;
			}
			else if (system_id_hex_lower == SYSTEM_ID_PLAYREADY_HEX)
			{
				drm_system = DRMSystem::PlayReady;

				// The PRO is carried in the pssh Data field; keep it for HLS signaling.
				data	   = user_data;
			}
			else
			{
				loge("ERROR", "Invalid system id hex string : %s", system_id_hex.CStr());
				return;
			}

			system_id = ov::Hex::Decode(system_id_hex);

			// Create pssh box
			ov::ByteStream stream(512);

			// FullBox header size : 12 bytes
			// System ID : 16 bytes
			// KID count : 4 bytes
			// KID : 16 bytes * kid count
			// Data size : 4 bytes
			// Data : data size bytes
			uint32_t box_size = 12 + 16 + 4 + (16 * kid_hex_list.size()) + 4;
			box_size += user_data ? user_data->GetLength() : 0;

			// Header
			stream.WriteBE32(box_size);	 // box size
			stream.WriteText("pssh");	 // box name
			stream.Write8(1);			 // version
			stream.WriteBE24(0);		 // flags

			stream.Write(system_id);  // system id

			// `KID_count` is mandatory for version 1, even when the list is empty.
			stream.WriteBE32(kid_hex_list.size());	// kid count

			for (const auto &kid_hex : kid_hex_list)
			{
				stream.Write(kid_hex);	// kid
			}

			if (user_data != nullptr)
			{
				stream.WriteBE32(user_data->GetLength());  // data size
				stream.Write(user_data);				   // data
			}
			else
			{
				stream.WriteBE32(0);  // data size
			}

			pssh_box_data = stream.GetDataPointer();
		}

		// A pssh box maps to exactly one systemId,
		// so this always holds a single system (never a combination such as `DRMSystem::All`).
		DRMSystem drm_system					= DRMSystem::None;
		std::shared_ptr<ov::Data> system_id		= nullptr;
		std::shared_ptr<ov::Data> pssh_box_data = nullptr;

		// PlayReady Object (PRO) extracted from the pssh Data field, used for HLS signaling.
		std::shared_ptr<ov::Data> data			= nullptr;
	};

	struct CencProperty
	{
		// = operator
		CencProperty &operator=(const CencProperty &other)
		{
			if (this != &other)
			{
				scheme			   = other.scheme;
				key_id			   = other.key_id != nullptr ? other.key_id->Clone() : nullptr;
				key				   = other.key != nullptr ? other.key->Clone() : nullptr;
				iv				   = other.iv != nullptr ? other.iv->Clone() : nullptr;
				fairplay_key_uri   = other.fairplay_key_uri;
				keyformat		   = other.keyformat;
				pssh_box_list	   = other.pssh_box_list;
				crypt_bytes_block  = other.crypt_bytes_block;
				skip_bytes_block   = other.skip_bytes_block;
				per_sample_iv_size = other.per_sample_iv_size;
			}
			return *this;
		}

		// set by user or drm provider
		CencProtectScheme scheme		 = CencProtectScheme::None;

		std::shared_ptr<ov::Data> key_id = nullptr;	 // 16 bytes
		std::shared_ptr<ov::Data> key	 = nullptr;	 // 16 bytes
		std::shared_ptr<ov::Data> iv	 = nullptr;	 // 16 bytes

		ov::String fairplay_key_uri;  // fairplay only
		ov::String keyformat;

		std::vector<PsshBox> pssh_box_list;

		// will be set by stream
		uint8_t crypt_bytes_block  = 1;	 // number of encrypted blocks in pattern based encryption
		uint8_t skip_bytes_block   = 9;	 // number of unencrypted blocks in pattern based encryption
		uint8_t per_sample_iv_size = 0;	 // 0 or 16
	};

	class Encryptor
	{
	public:
		Encryptor(const std::shared_ptr<const MediaTrack> &media_track, const CencProperty &cenc_property);
		bool Encrypt(const Sample &clear_sample, Sample &cipher_sample);

	private:
		bool GenerateSubSamples(const std::shared_ptr<const MediaPacket> &media_packet, std::vector<Sample::SubSample> &sub_samples);
		bool GenerateSubSamplesFromH264(const std::shared_ptr<const MediaPacket> &media_packet, std::vector<Sample::SubSample> &sub_samples);
		bool GenerateSubSamplesFromH265(const std::shared_ptr<const MediaPacket> &media_packet, std::vector<Sample::SubSample> &sub_samples);

		// If sub_samples is empty, it means that the full sample encryption is performed.
		bool EncryptInternal(const std::shared_ptr<const ov::Data> &clear_data, std::shared_ptr<ov::Data> &encrypted_data, const std::vector<Sample::SubSample> &sub_samples);

		// dest must be allocated with the same size as source.
		bool EncryptPattern(const uint8_t *source, size_t source_size, uint8_t *dest, bool last_block, std::function<bool(const uint8_t *, size_t, uint8_t *, bool)>(encrypt_func));
		bool EncryptCbc(const uint8_t *source, size_t source_size, uint8_t *dest, bool last_block);
		bool EncryptCtr(const uint8_t *source, size_t source_size, uint8_t *dest, bool last_block);

		bool UpdateIv();
		bool SetCounter();
		bool IncrementCounter();

		CencProperty _cenc_property;
		std::shared_ptr<const MediaTrack> _media_track								= nullptr;

		std::function<bool(const uint8_t *, size_t, uint8_t *, bool)> _encrypt_func = nullptr;

		ov::AES _aes;

		// For CTR mode
		uint32_t _block_offset				= 0;
		uint32_t _sample_cipher_block_count = 0;
		uint8_t _counter[AES_BLOCK_SIZE]	= {
			0,
		};
		uint8_t _encrypted_counter[AES_BLOCK_SIZE] = {
			0,
		};
	};
}  // namespace bmff