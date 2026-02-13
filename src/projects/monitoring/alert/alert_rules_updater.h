//=============================================================================
//
//  OvenMediaEngine
//
//  Created by Gilhoon Choi
//  Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include <base/info/info.h>

namespace mon::alrt
{
	class AlertRulesUpdater
	{
	public:
		AlertRulesUpdater(cfg::alrt::Alert &alert_config);

		std::shared_ptr<const cfg::alrt::rule::Rules> GetRules() const;

		/// @return true if the auth file is updated successfully or no need to update, otherwise false
		bool UpdateIfNeeded();

	private:
		bool Update();

		const cfg::alrt::Alert _alert_config;
		bool _is_dynamic_update				 = false;

		std::atomic<uint64_t> _last_modified = 0;
		ov::String _rules_file;

		mutable std::shared_mutex _dynamic_rules_mutex;
		std::shared_ptr<const cfg::alrt::rule::Rules> _rules = nullptr;
	};
}  // namespace mon::alrt
