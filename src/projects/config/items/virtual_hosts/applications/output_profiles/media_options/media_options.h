//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "subtitle.h"
#include "./overlays/overlays.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace oprf
			{
				struct MediaOptions : public Item
				{
				protected:
					Subtitle _subtitle; 
					Overlays _overlays;

				public:
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSubtitle, _subtitle)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetOverlays, _overlays);

				protected:
					void MakeList() override
					{
						Register<Optional>("Subtitles", &_subtitle, nullptr,
							[=]() -> std::shared_ptr<ConfigError> {
								if (_subtitle.IsEnabled() && _subtitle.GetRenditions().empty())
								{
									return CreateConfigErrorPtr("Subtitle is enabled but no renditions are defined");
								}
								
								// Check default label
								auto default_label = _subtitle.GetDefaultLabel();
								if (default_label.IsEmpty() == false)
								{
									bool found = false;
									for (auto &rendition : _subtitle.GetRenditions())
									{
										if (rendition.GetLabel() == default_label)
										{
											rendition.SetDefault(true);
											if (rendition.IsAutoSelect() == false)
											{
												logw("Config", "Default subtitle rendition '%s' must have AutoSelect enabled. Enabling it automatically.", default_label.CStr());
												rendition.SetAutoSelect(true);
											}
											found = true;
										}
									}

									if (found == false)
									{
										return CreateConfigErrorPtr("Default label '%s' not found in subtitle renditions", default_label.CStr());
									}
								}

								return nullptr;
							}
						);
						Register<Optional>({"Overlays", "overlays"}, &_overlays);

					}
				};
			}  // namespace oprf
		} // namespace app
	} // namespace vhost
}  // namespace cfg
