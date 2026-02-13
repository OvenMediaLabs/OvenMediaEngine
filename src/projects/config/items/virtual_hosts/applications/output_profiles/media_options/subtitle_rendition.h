//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "transcription/transcription.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct SubtitleRendition : public Item
				{
				protected:
					ov::String _label;
					ov::String _language;
					bool _auto_select = false;
					bool _default = false;
					bool _forced = false;
					Transcription _transcription;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetLabel, _label)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetLanguage, _language)
					CFG_DECLARE_CONST_REF_GETTER_OF(IsAutoSelect, _auto_select)
					CFG_DECLARE_CONST_REF_GETTER_OF(IsDefault, _default)
					CFG_DECLARE_CONST_REF_GETTER_OF(IsForced, _forced)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetTranscription, _transcription)

					void SetDefault(bool is_default)
					{
						_default = is_default;
					}

					void SetAutoSelect(bool auto_select)
					{
						_auto_select = auto_select;
					}

				protected:
					void MakeList() override
					{
						Register("Label", &_label);
						Register("Language", &_language);
						Register<Optional>("AutoSelect", &_auto_select);
						Register<Optional>("Forced", &_forced);
						Register<Optional>("Transcription", &_transcription);
					}
				};
			}  // namespace oprf
		} // namespace app
	} // namespace vhost
}  // namespace cfg
