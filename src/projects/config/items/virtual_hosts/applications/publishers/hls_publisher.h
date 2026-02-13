//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2019 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../../../common/cross_domain_support.h"
#include "dumps/dumps.h"
#include "hls_options/cache_control.h"
#include "hls_options/default_query_string.h"
#include "hls_options/dvr.h"
#include "publisher.h"

namespace cfg
{
	namespace vhost
	{
		namespace app
		{
			namespace pub
			{
				struct HlsPublisher : public Publisher, public cmn::CrossDomainSupport
				{
				protected:
					bool _origin_mode		 = false;

					int _segment_count		 = 5;
					double _segment_duration = 10;

					Dvr _dvr;
					DefaultQueryString _default_query_string;
					bool _create_default_playlist = true;

					CacheControl _cache_control;
					Dumps _dumps;
					bool _server_time_based_segment_numbering = false;

				public:
					virtual PublisherType GetType() const override
					{
						return PublisherType::Hls;
					}

					CFG_DECLARE_CONST_REF_GETTER_OF(IsOriginMode, _origin_mode)
					CFG_DECLARE_CONST_REF_GETTER_OF(IsServerTimeBasedSegmentNumbering, _server_time_based_segment_numbering)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSegmentCount, _segment_count)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetSegmentDuration, _segment_duration)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDumps, _dumps)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetCacheControl, _cache_control)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDvr, _dvr)
					CFG_DECLARE_CONST_REF_GETTER_OF(GetDefaultQueryString, _default_query_string)
					CFG_DECLARE_CONST_REF_GETTER_OF(ShouldCreateDefaultPlaylist, _create_default_playlist)

				protected:
					virtual void MakeList() override
					{
						Publisher::MakeList();

						Register<Optional>("OriginMode", &_origin_mode);
						Register<Optional>("ServerTimeBasedSegmentNumbering", &_server_time_based_segment_numbering);
						Register<Optional>("SegmentCount", &_segment_count);
						Register<Optional>("SegmentDuration", &_segment_duration);
						Register<Optional>("CrossDomains", &_cross_domains);
						Register<Optional>("DVR", &_dvr);
						Register<Optional>("Dumps", &_dumps);
						Register<Optional>("CacheControl", &_cache_control);
						Register<Optional>("DefaultQueryString", &_default_query_string);
						Register<Optional>("CreateDefaultPlaylist", &_create_default_playlist);
					}
				};
			}  // namespace pub
		}  // namespace app
	}  // namespace vhost
}  // namespace cfg
