//=============================================================================
//
//	OvenMediaEngine
//
//	Created by Gilhoon Choi
//	Copyright (c) 2025 AirenSoft. All rights reserved.
//
//==============================================================================
#include "alert_rules_updater.h"

#define OV_LOG_TAG "Monitor.Alert"
#define RULES_ITEM_NAME "Rules"

namespace mon::alrt
{
	AlertRulesUpdater::AlertRulesUpdater(cfg::alrt::Alert &alert_config)
		: _alert_config(alert_config)
	{
		bool is_configured;

		auto rules_file = alert_config.GetRulesFile(&is_configured);
		if (is_configured)
		{
			_rules_file		   = ov::PathManager::GetNormalizedPath(rules_file);

			_is_dynamic_update = true;

			// dummy rules to avoid nullptr dereference
			_rules			   = std::make_shared<cfg::alrt::rule::Rules>();
		}
		else
		{
			_is_dynamic_update = false;

			_rules			   = std::make_shared<cfg::alrt::rule::Rules>(alert_config.GetRules());
		}
	}

	std::shared_ptr<const cfg::alrt::rule::Rules> AlertRulesUpdater::GetRules() const
	{
		if (_is_dynamic_update)
		{
			std::shared_lock lock(_dynamic_rules_mutex);

			return _rules;
		}

		return _rules;
	}

	bool AlertRulesUpdater::UpdateIfNeeded()
	{
		if (_is_dynamic_update == false)
		{
			// Dynamic update is not enabled
			return true;
		}

		struct stat file_stat = {0};
		auto last_modified	  = _last_modified.load();

		if (ov::PathManager::IsFile(_rules_file) == false)
		{
			logtt("File not found: %s", _rules_file.CStr());
			return false;
		}

		if (::stat(_rules_file.CStr(), &file_stat) == -1)
		{
			auto error = ov::Error::CreateErrorFromErrno();
			logtw("Could not obtain file information: %s, error: %s", _rules_file.CStr(), error->What());

			return Update();
		}

#if defined(__APPLE__)
		const auto &time = file_stat.st_mtimespec;
#else	// defined(__APPLE__)
		const auto &time = file_stat.st_mtim;
#endif	// defined(__APPLE__)
		auto modified = (static_cast<uint64_t>(time.tv_sec) * 1000000000ULL) + static_cast<uint64_t>(time.tv_nsec);

		if (last_modified != modified)
		{
			logtt("The alert rules file has been changed (prev: %zu => new: %zu): %s, updating...",
				  last_modified, modified, _rules_file.CStr());

			_last_modified = modified;
			return Update();
		}

		// No need to update
		return true;
	}

	bool AlertRulesUpdater::Update()
	{
		auto content = ov::LoadFromFile(_rules_file);

		if (content == nullptr)
		{
			logte("Could not load XML document from: %s", _rules_file.CStr());
			return false;
		}

		auto document = std::make_shared<pugi::xml_document>();

		auto result	  = document->load_buffer(content->GetData(), content->GetLength());
		if (result == false)
		{
			logte("Could not parse XML document from: %s, error: %s", _rules_file.CStr(), result.description());
			return false;
		}

		auto rules_node = document->child(RULES_ITEM_NAME);

		if (rules_node.empty())
		{
			logte("Could not find <%s> node in %s", RULES_ITEM_NAME, _rules_file.CStr());
			return false;
		}

		try
		{
			cfg::DataSource data_source("", "", document, rules_node);

			auto rules = std::make_shared<cfg::alrt::rule::Rules>();

			cfg::ItemName item_name(RULES_ITEM_NAME);
			rules->FromDataSource(item_name.GetName(data_source.GetType()), item_name, data_source);

			{
				std::lock_guard lock_guard(_dynamic_rules_mutex);
				_rules = rules;
			}

			logti("The alert rules file has been updated: %s", _rules_file.CStr());
		}
		catch (const cfg::ConfigError &e)
		{
			logte("Could not parse rules from %s, error: %s", _rules_file.CStr(), e.What());
			return false;
		}

		return true;
	}
}  // namespace mon::alrt
