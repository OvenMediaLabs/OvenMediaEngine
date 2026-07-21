//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2026 OvenMediaLabs. All rights reserved.
//
//==============================================================================
#include "llhls_cenc_key.h"

#include <base/ovcrypto/base_64.h>

#include "llhls_private.h"

namespace pub::llhls
{
	ov::String MakeCencKeyTag(const bmff::CencProperty &cenc_property, PlaylistType playlist_type)
	{
		// Master playlist uses `#EXT-X-SESSION-KEY`; media playlist (chunklist) uses `#EXT-X-KEY`.
		const char *tag_name = (playlist_type == PlaylistType::Master) ? "#EXT-X-SESSION-KEY" : "#EXT-X-KEY";

		ov::String xkey;

		for (const auto &pssh : cenc_property.pssh_box_list)
		{
			if (pssh.drm_system == bmff::DRMSystem::None)
			{
				// Unknown or unsupported systemId. Keep the "old" behavior (emit an empty line), but log it.
				logte("Unknown DRM system in pssh box, skipping the %s tag.", tag_name);
				xkey.Append("\n");
				continue;
			}

			const auto drm_value = ov::ToUnderlyingType(pssh.drm_system);
			if ((drm_value & (drm_value - 1)) != 0)
			{
				// A pssh box maps to exactly one `systemId`, so this should never be a combination.
				logte("Multiple DRM systems in pssh box, skipping the %s tag.", tag_name);
				OV_ASSERT2(false);

				// Keep the "old" behavior (emit an empty line)
				xkey.Append("\n");
				continue;
			}

			// PlayReady needs its Object (PRO) to build a valid key tag.
			// Without it we cannot emit a proper URI, so skip the entry instead of writing a broken tag.
			if ((pssh.drm_system == bmff::DRMSystem::PlayReady) && (pssh.data == nullptr))
			{
				logte("PlayReady pssh is missing its PlayReady Object (PRO), skipping the %s tag.", tag_name);
				OV_ASSERT2(false);

				// Keep the "old" behavior (emit an empty line)
				xkey.Append("\n");
				continue;
			}

			// Determine the effective protection scheme for this pssh.
			// FairPlay is always cbcs (SAMPLE-AES) regardless of the configured scheme.
			const auto scheme  = (pssh.drm_system != bmff::DRMSystem::FairPlay)
									 ? cenc_property.scheme
									 : bmff::CencProtectScheme::Cbcs;

			// `METHOD` attribute derived from the (effective) scheme.
			const char *method = "";

			switch (scheme)
			{
				case bmff::CencProtectScheme::None:
					break;

				case bmff::CencProtectScheme::Cbcs:
					method = "METHOD=SAMPLE-AES";
					break;

				case bmff::CencProtectScheme::Cenc:
					method = "METHOD=SAMPLE-AES-CTR";
					break;
			}

			if (scheme == bmff::CencProtectScheme::None)
			{
				// Without a CENC scheme we cannot form a valid METHOD; skip instead of emitting a malformed tag.
				logte("Missing CENC scheme for pssh, skipping the %s tag.", tag_name);
				xkey.Append("\n");
				continue;
			}

			xkey.AppendFormat("%s:%s", tag_name, method);

			switch (pssh.drm_system)
			{
				case bmff::DRMSystem::None:
					[[fallthrough]];
				case bmff::DRMSystem::All:
					// Handled above, so this should never happen.
					OV_ASSERT2(false);
					break;

				case bmff::DRMSystem::Widevine:
					xkey.AppendFormat(",URI=\"data:text/plain;base64,%s\"", ov::Base64::Encode(pssh.pssh_box_data, false).CStr());
					xkey.AppendFormat(",KEYID=0x%s", cenc_property.key_id->ToHexString().CStr());
					xkey.AppendFormat(",KEYFORMAT=\"urn:uuid:%s\"", ov::ToUUIDString(pssh.system_id->GetData(), pssh.system_id->GetLength()).LowerCaseString().CStr());
					xkey.AppendFormat(",KEYFORMATVERSIONS=\"1\"");
					break;

				case bmff::DRMSystem::FairPlay:
					xkey.AppendFormat(",URI=\"%s\"", cenc_property.fairplay_key_uri.CStr());

					if (cenc_property.keyformat.LowerCaseString() == "identity")
					{
						xkey.AppendFormat(",KEYFORMAT=\"identity\"");
						xkey.AppendFormat(",IV=0x%s", cenc_property.iv->ToHexString().CStr());
					}
					else
					{
						xkey.AppendFormat(",KEYFORMAT=\"com.apple.streamingkeydelivery\"");
						xkey.AppendFormat(",KEYFORMATVERSIONS=\"1\"");
					}
					break;

				case bmff::DRMSystem::PlayReady:
					// PlayReady carries the PlayReady Object (PRO) in the URI (UTF-16, base64), not the whole pssh box.
					// `pssh.data` is guaranteed non-null here (checked at the top of the loop).
					xkey.AppendFormat(",URI=\"data:text/plain;charset=UTF-16;base64,%s\"", ov::Base64::Encode(pssh.data, false).CStr());
					xkey.AppendFormat(",KEYFORMAT=\"com.microsoft.playready\"");
					xkey.AppendFormat(",KEYFORMATVERSIONS=\"1\"");
					break;
			}

			xkey.Append("\n");
		}

		return xkey;
	}
}  // namespace pub::llhls
