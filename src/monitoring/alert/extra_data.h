//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Keukhan
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "../queue_metrics.h"
#include "../stream_metrics.h"
#include "base/info/codec.h"

namespace mon::alrt

{
	class ExtraData
	{
	public:
		class Builder
		{
		public:
			Builder() = default;

			Builder &CodecModule(std::shared_ptr<info::CodecModule> codec_module)
			{
				_codec_module = std::move(codec_module);
				return *this;
			}

			Builder &OutputProfile(std::shared_ptr<cfg::vhost::app::oprf::OutputProfile> output_profile)
			{
				_output_profile = std::move(output_profile);
				return *this;
			}

			Builder &SourceTrackId(int32_t track_id)
			{
				_source_track_id = track_id;
				return *this;
			}

			Builder &ParentSourceTrackId(int32_t parent_track_id)
			{
				_parent_source_track_id = parent_track_id;
				return *this;
			}

			Builder &ErrorCount(int32_t error_count)
			{
				_error_count = error_count;
				return *this;
			}

			Builder &ErrorEvaluationInterval(int32_t evaluation_interval)
			{
				_error_evaluation_interval = evaluation_interval;
				return *this;
			}

			[[nodiscard]]
			std::shared_ptr<ExtraData> Build() const
			{
				return std::shared_ptr<ExtraData>(new ExtraData(*this));
			}

		private:
			friend class ExtraData;

			std::shared_ptr<info::CodecModule> _codec_module;
			std::shared_ptr<cfg::vhost::app::oprf::OutputProfile> _output_profile;

			std::optional<int32_t> _source_track_id;
			std::optional<int32_t> _parent_source_track_id;
			std::optional<int32_t> _error_count;
			std::optional<int32_t> _error_evaluation_interval;
		};

	public:
		Json::Value ToJsonValue() const;

		// Getters (immutable)
		const std::shared_ptr<info::CodecModule> &CodecModule() const noexcept
		{
			return _codec_module;
		}

		const std::shared_ptr<cfg::vhost::app::oprf::OutputProfile> &OutputProfile() const noexcept
		{
			return _output_profile;
		}

		const std::optional<int32_t> &SourceTrackId() const noexcept
		{
			return _source_track_id;
		}

		const std::optional<int32_t> &ParentSourceTrackId() const noexcept
		{
			return _parent_source_track_id;
		}

		const std::optional<int32_t> &ErrorCount() const noexcept
		{
			return _error_count;
		}

		const std::optional<int32_t> &ErrorEvaluationInterval() const noexcept
		{
			return _error_evaluation_interval;
		}

	private:
		explicit ExtraData(const Builder &builder)
			: _codec_module(builder._codec_module), _output_profile(builder._output_profile), _source_track_id(builder._source_track_id), _parent_source_track_id(builder._parent_source_track_id), _error_count(builder._error_count), _error_evaluation_interval(builder._error_evaluation_interval)
		{
		}

	private:
		// Extra data for only transcoder
		std::shared_ptr<info::CodecModule> _codec_module;
		std::shared_ptr<cfg::vhost::app::oprf::OutputProfile> _output_profile;

		std::optional<int32_t> _source_track_id;
		std::optional<int32_t> _parent_source_track_id;
		std::optional<int32_t> _error_count;
		std::optional<int32_t> _error_evaluation_interval;
	};
}  // namespace mon::alrt
