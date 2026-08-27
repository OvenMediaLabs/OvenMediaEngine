//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "hls_options/drm.h"
#include "hls_publisher.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct LLHlsPublisher : public HlsPublisher
				{
				protected:
					double _chunk_duration	  = 0.5;
					double _part_hold_back	  = 0;	// it will be set to 3 * chunk_duration automatically
					bool _enable_preload_hint = true;
					Drm _drm;
					double _subtitle_hold_back_ms = 0;

					ov::String _segmentation_mode;
					LLHlsSegmentationMode _segmentation_mode_type = LLHlsSegmentationMode::Duration;

					ov::String _cue_out_cut_mode;
					LLHlsCueOutCutMode _cue_out_cut_mode_type = LLHlsCueOutCutMode::Keyframe;

				public:
					PublisherType GetType() const override
					{
						return PublisherType::LLHls;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(GetChunkDuration, _chunk_duration)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetPartHoldBack, _part_hold_back)
					CFG_DECLARE_CONST_REF_GETTER_OF(IsPreloadHintEnabled, _enable_preload_hint)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDrm, _drm)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSubtitleHoldBackMs, _subtitle_hold_back_ms)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSegmentationMode, _segmentation_mode_type)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetCueOutCutMode, _cue_out_cut_mode_type)

				protected:
					void MakeList() override
					{
						HlsPublisher::MakeList();

						Register<Optional>("ChunkDuration", &_chunk_duration);
						Register<Optional>("PartHoldBack", &_part_hold_back);
						Register<Optional>("EnablePreloadHint", &_enable_preload_hint);
						Register<Optional>({"DRM", "drm"}, &_drm);
						Register<Optional>("SubtitleHoldBack", &_subtitle_hold_back_ms);

						Register<Optional>("SegmentationMode", &_segmentation_mode, nullptr, [=]() -> std::shared_ptr<ConfigError> {
								if (_segmentation_mode.LowerCaseString() == "synced")
								{
									_segmentation_mode_type = LLHlsSegmentationMode::Synced;
								}
								else if (_segmentation_mode.LowerCaseString() == "duration" || _segmentation_mode.IsEmpty())
								{
									_segmentation_mode_type = LLHlsSegmentationMode::Duration;
								}
								else
								{
									return CreateConfigErrorPtr("Invalid value for SegmentationMode. Valid values are 'duration' or 'synced'");
								}

								return nullptr;
							});

						// Whether a CUE-OUT cuts right at its position (an ad insertor
						// replaces the break anyway) or at the next keyframe (the
						// original content keeps playing, client-side ad insertion)
						Register<Optional>("CueOutCutMode", &_cue_out_cut_mode, nullptr, [=]() -> std::shared_ptr<ConfigError> {
								if (_cue_out_cut_mode.LowerCaseString() == "keyframe" || _cue_out_cut_mode.IsEmpty())
								{
									_cue_out_cut_mode_type = LLHlsCueOutCutMode::Keyframe;
								}
								else if (_cue_out_cut_mode.LowerCaseString() == "immediate")
								{
									_cue_out_cut_mode_type = LLHlsCueOutCutMode::Immediate;
								}
								else
								{
									return CreateConfigErrorPtr("Invalid value for CueOutCutMode. Valid values are 'immediate' or 'keyframe'");
								}

								return nullptr;
							});
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
