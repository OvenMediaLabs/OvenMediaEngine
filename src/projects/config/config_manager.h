//==============================================================================
//
//  OvenMediaEngine
//
//  Created by Hyunjun Jang
//  Copyright (c) 2018 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

#include "items/items.h"

#define CFG_LOG_FILE_NAME "Logger.xml"
#define CFG_MAIN_FILE_NAME "Server.xml"
#define CFG_LAST_CONFIG_FILE_NAME_LEGACY "LastConfig.json"
#define CFG_LAST_CONFIG_FILE_NAME "LastConfig.xml"
#define SERVER_ID_STORAGE_FILE "Server.id"
#define LICENSE_STORAGE_FILE "License"

namespace cfg
{
	class ConfigManager : public ov::Singleton<ConfigManager>
	{
	public:
		friend class ov::Singleton<ConfigManager>;
		~ConfigManager() override;

		MAY_THROWS(cfg::ConfigError)
		void LoadConfigs(ov::String config_path);

		MAY_THROWS(cfg::ConfigError)
		void ReloadConfigs();

		std::shared_ptr<const Server> GetServer() const noexcept
		{
			std::shared_lock lock(_server_mutex);
			return _server;
		}

		ov::String GetConfigPath()
		{
			return _config_path;
		}

	protected:
		ConfigManager();

		MAY_THROWS(cfg::ConfigError)
		void CheckLegacyConfigs(ov::String config_path);

		MAY_THROWS(cfg::ConfigError)
		void LoadLoggerConfig(const ov::String &config_path);

		MAY_THROWS(cfg::ConfigError)
		void LoadServerConfig(const ov::String &config_path);

		MAY_THROWS(cfg::ConfigError)
		void LoadLicenseKey(const ov::String &config_path);

		MAY_THROWS(cfg::ConfigError)
		void LoadServerID(const ov::String &config_path);

		MAY_THROWS(cfg::ConfigError)
		void CheckValidVersion(const ov::String &name, int version);

		ov::String _config_path;

		mutable std::shared_mutex _server_mutex;
		std::shared_ptr<Server> _server;
		ov::String _server_id;
		ov::String _license_key;
		std::map<ov::String, ov::String> _macros;

		timespec _last_modified;

		// key: XML file name
		// value: compatible version numbers
		std::map<ov::String, std::vector<int>> _supported_versions_map;

		mutable std::mutex _config_mutex;

	private:
		std::tuple<bool, ov::String> LoadServerIDFromStorage(const ov::String &config_path) const;
		bool StoreServerID(const ov::String &config_path, ov::String server_id);
		std::tuple<bool, ov::String> GenerateServerID() const;
	};
}  // namespace cfg
