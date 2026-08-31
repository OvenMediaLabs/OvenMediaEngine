//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Getroot
//  Copyright (c) 2023 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "provider.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pvd
			{
				struct ScheduledProvider : public Provider
				{
				protected:
					ov::String _media_root_dir;
					ov::String _schedule_files_dir;
					// Rename removed schedule files with a timestamp instead of deleting them
					bool _preserve_removed_schedule_file = false;

				public:
					ProviderType GetType() const override
					{
						return ProviderType::Scheduled;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(GetMediaRootDir, _media_root_dir)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetScheduleFilesDir, _schedule_files_dir)
					CFG_DECLARE_CONST_REF_GETTER_OF(IsPreserveRemovedScheduleFile, _preserve_removed_schedule_file)

				protected:
					void MakeList() override
					{
						Provider::MakeList();

						Register("MediaRootDir", &_media_root_dir);
						Register("ScheduleFilesDir", &_schedule_files_dir);
						Register<Optional>("PreserveRemovedScheduleFile", &_preserve_removed_schedule_file);
					}
				};
			}  // namespace pvd
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg