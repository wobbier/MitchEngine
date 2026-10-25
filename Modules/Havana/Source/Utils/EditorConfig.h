#pragma once

#include <JSON.h>
#include <Math/Vector3.h>
#include <Singleton.h>
#include <Path.h>
#include <File.h>
#include <Utils/PlatformUtils.h>
#include <Utils/StringUtils.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

// Per-user editor state and preferences (.tmp/Havana.cfg): camera, panel visibility, recent
// scenes and trees, and a free-form preference table used by the Preferences window.
class EditorConfig
{
	static constexpr const char* kConfigPath = ".tmp/Havana.cfg";
	static constexpr size_t kMaxRecent = 10;

public:
	Vector3 CameraPosition;
	Vector3 CameraRotation;

	EditorConfig() = default;

	struct EditorWidgetProperties
	{
		bool IsVisible = false;
	};

	std::map<std::string, EditorWidgetProperties> PanelVisibility;
	std::vector<std::string> RecentScenes;
	// MitchTree recipes, most recently opened first.
	std::vector<std::string> RecentTrees;
	json Preferences = json::object();

	template<typename T>
	T GetPreference(const std::string& key, const T& fallback) const
	{
		auto it = Preferences.find(key);
		if (it == Preferences.end())
		{
			return fallback;
		}
		try
		{
			return it->get<T>();
		}
		catch (...)
		{
			return fallback;
		}
	}

	template<typename T>
	void SetPreference(const std::string& key, const T& value)
	{
		Preferences[key] = value;
	}

	void AddRecentScene(const std::string& scenePath)
	{
		AddRecent(RecentScenes, scenePath);
	}

	void AddRecentTree(const std::string& treePath)
	{
		AddRecent(RecentTrees, treePath);
	}

	void Init()
	{
		Path configPath(kConfigPath);

		if (!configPath.Exists)
		{
			Save();
		}
	}

	void Load()
	{
		Path configPath(kConfigPath);
		if (!configPath.Exists)
		{
			return;	// a project's first open (unattended runs never write one)
		}

		File configFile(configPath);
		configFile.Read();

		if (configFile.Data.empty())
		{
			return;
		}
		json inJson = json::parse(configFile.Data, nullptr, false);
		if (inJson.is_discarded() || !inJson.is_object())
		{
			return;
		}

		auto readVector = [&inJson](const char* key, Vector3& out) {
			auto it = inJson.find(key);
			if (it != inJson.end() && it->is_array() && it->size() == 3)
			{
				out = Vector3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
			}
		};
		readVector("CameraPosition", CameraPosition);
		readVector("CameraRotation", CameraRotation);

		if (inJson.contains("WidgetProperties") && inJson["WidgetProperties"].is_object())
		{
			for (auto& i : inJson["WidgetProperties"].items())
			{
				PanelVisibility[i.key()] = { i.value().value("IsVisible", false) };
			}
		}
		ReadRecent(inJson, "RecentScenes", RecentScenes);
		ReadRecent(inJson, "RecentTrees", RecentTrees);
		if (inJson.contains("Preferences") && inJson["Preferences"].is_object())
		{
			Preferences = inJson["Preferences"];
		}
	}

	// Unattended runs (tests, scripts) read the user's settings but never write them.
	bool ReadOnly = false;

	void Save()
	{
		if (ReadOnly)
		{
			return;
		}
		Path configPath(kConfigPath);

		json config;
		config["CameraPosition"] = { CameraPosition.x, CameraPosition.y, CameraPosition.z };
		config["CameraRotation"] = { CameraRotation.x, CameraRotation.y, CameraRotation.z };

		json& widgets = config["WidgetProperties"] = json::object();
		for (auto& panel : PanelVisibility)
		{
			widgets[panel.first] = { { "Name", panel.first }, { "IsVisible", panel.second.IsVisible } };
		}
		config["RecentScenes"] = RecentScenes;
		config["RecentTrees"] = RecentTrees;
		config["Preferences"] = Preferences;

		File configFile(configPath);
		configFile.Write(config.dump(4));
	}

	ME_SINGLETON_DEFINITION(EditorConfig)

private:
	static void AddRecent(std::vector<std::string>& recent, const std::string& path)
	{
		if (path.empty())
		{
			return;
		}
		recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
		recent.insert(recent.begin(), path);
		if (recent.size() > kMaxRecent)
		{
			recent.resize(kMaxRecent);
		}
	}

	static void ReadRecent(const json& inJson, const char* key, std::vector<std::string>& out)
	{
		auto it = inJson.find(key);
		if (it == inJson.end() || !it->is_array())
		{
			return;
		}
		out.clear();
		for (const json& entry : *it)
		{
			if (entry.is_string())
			{
				out.push_back(entry.get<std::string>());
			}
		}
	}
};
