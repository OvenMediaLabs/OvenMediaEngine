//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/ovlibrary/ovlibrary.h>
#include <base/info/media_track.h>
#include <base/mediarouter/media_buffer.h>
#include <base/ovcrypto/aes.h>
#include <base/ovlibrary/hex.h>

#include "sample.h"

namespace bmff
{
    constexpr uint8_t AES_BLOCK_SIZE = 16;

    enum class CencProtectScheme : uint8_t
    {
        None,
        Cenc,
        Cbcs
    };

    enum class CencEncryptMode : uint8_t
    {
        None,
        Ctr,
        Cbc
    };

    enum class DRMSystem : uint8_t
    {
        None,
        Widevine,
        FairPlay,
        All = Widevine | FairPlay
    };

    static const char* CencProtectSchemeToString(CencProtectScheme scheme)
    {
        switch (scheme)
        {
        case CencProtectScheme::Cenc:
            return "cenc";
        case CencProtectScheme::Cbcs:
            return "cbcs";
        case CencProtectScheme::None:
        default:
            return "none";
        }
    }

    struct PsshBox
    {
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

			// Parse pssh box
            ov::ByteStream stream(data);

            [[maybe_unused]] auto box_size = stream.ReadBE32();
            [[maybe_unused]] auto box_name = stream.GetRemainData(4);
            stream.Skip<uint8_t>(4);
            [[maybe_unused]] auto version = stream.Read8();
            [[maybe_unused]] auto flags = stream.ReadBE24();

            system_id = stream.GetRemainData(16)->Clone();

            logt("DEBUG", "System ID : %s", system_id->ToHexString().LowerCaseString().CStr());

            if (system_id->ToHexString().LowerCaseString() == "edef8ba979d64acea3c827dcd51d21ed")
            {
                drm_system = DRMSystem::Widevine;
            }
            else if (system_id->ToHexString().LowerCaseString() == "94ce86fb07ff4f43adb893d2fa968ca2")
            {
                drm_system = DRMSystem::FairPlay;
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

            if (system_id_hex.LowerCaseString() == "edef8ba979d64acea3c827dcd51d21ed")
            {
                drm_system = DRMSystem::Widevine;
            }
            else if (system_id_hex.LowerCaseString() == "94ce86fb07ff4f43adb893d2fa968ca2")
            {
                drm_system = DRMSystem::FairPlay;
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
            stream.WriteBE32(box_size); // box size
            stream.WriteText("pssh"); // box name
            stream.Write8(1); // version
            stream.WriteBE24(0); // flags

            stream.Write(system_id); // system id

            if (kid_hex_list.size() > 0)
            {
                stream.WriteBE32(kid_hex_list.size()); // kid count

                for (const auto &kid_hex : kid_hex_list)
                {
                    stream.Write(kid_hex); // kid
                }
            }

            if (user_data != nullptr)
            {
                stream.WriteBE32(user_data->GetLength()); // data size
                stream.Write(user_data); // data
            }
            else
            {
                stream.WriteBE32(0); // data size
            }

            pssh_box_data = stream.GetDataPointer();
        }

        DRMSystem drm_system = DRMSystem::None;
        std::shared_ptr<ov::Data> system_id = nullptr;
        std::shared_ptr<ov::Data> pssh_box_data = nullptr;
    };

    struct CencProperty
    {
        // = operator
        CencProperty& operator=(const CencProperty &other)
        {
            if (this != &other)
            {
                scheme = other.scheme;
                key_id = other.key_id!=nullptr?other.key_id->Clone():nullptr;
                key = other.key!=nullptr?other.key->Clone():nullptr;
                iv = other.iv!=nullptr?other.iv->Clone():nullptr;
                fairplay_key_uri = other.fairplay_key_uri;
                keyformat = other.keyformat;
                pssh_box_list = other.pssh_box_list;
                crypt_bytes_block = other.crypt_bytes_block;
                skip_bytes_block = other.skip_bytes_block;
                per_sample_iv_size = other.per_sample_iv_size;
            }
            return *this;
        }

        // set by user or drm provider
        CencProtectScheme scheme = CencProtectScheme::None;

        std::shared_ptr<ov::Data> key_id = nullptr; // 16 bytes 
        std::shared_ptr<ov::Data> key = nullptr; // 16 bytes
        std::shared_ptr<ov::Data> iv = nullptr; // 16 bytes

        ov::String fairplay_key_uri; // fairplay only
        ov::String keyformat;
        
        std::vector<PsshBox> pssh_box_list;

        // will be set by stream
        uint8_t crypt_bytes_block = 1; // number of encrypted blocks in pattern based encryption
        uint8_t skip_bytes_block = 9; // number of unencrypted blocks in pattern based encryption
        uint8_t per_sample_iv_size = 0; // 0 or 16
    };

    class Encryptor
    {
    public:
        Encryptor(const std::shared_ptr<const MediaTrack> &media_track, const CencProperty &cenc_property);
        bool Encrypt(const Sample &clear_sample, Sample &cipher_sample);

    private:
        bool GenerateSubSamples(const std::shared_ptr<const MediaPacket> &media_packet, std::vector<Sample::SubSample> &sub_samples);
        bool GenerateSubSamplesFromH264(const std::shared_ptr<const MediaPacket> &media_packet, std::vector<Sample::SubSample> &sub_samples);
        
        // If sub_samples is empty, it means that the full sample encryption is performed.
        bool EncryptInternal(const std::shared_ptr<const ov::Data> &clear_data, std::shared_ptr<ov::Data> &encrypted_data, const std::vector<Sample::SubSample> &sub_samples);

        // dest must be allocated with the same size as source.
        bool EncryptPattern(const uint8_t *source, size_t source_size, uint8_t *dest, bool last_block, std::function<bool(const uint8_t*, size_t, uint8_t*, bool)>(encrypt_func));
        bool EncryptCbc(const uint8_t *source, size_t source_size, uint8_t *dest, bool last_block);
        bool EncryptCtr(const uint8_t *source, size_t source_size, uint8_t *dest, bool last_block);

        bool UpdateIv();
        bool SetCounter();
        bool IncrementCounter();

        CencProperty _cenc_property;
        std::shared_ptr<const MediaTrack> _media_track = nullptr;

        std::function<bool(const uint8_t*, size_t, uint8_t*, bool)> _encrypt_func = nullptr;

        ov::AES _aes;

        // For CTR mode
        uint32_t _block_offset = 0;
        uint32_t _sample_cipher_block_count = 0;
        uint8_t _counter[AES_BLOCK_SIZE] = { 0, };
        uint8_t _encrypted_counter[AES_BLOCK_SIZE] = { 0, };
    };
}