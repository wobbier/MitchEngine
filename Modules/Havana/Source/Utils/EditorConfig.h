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
// scenes and a free-form preference table used by the Preferences window.
class EditorConfig
{
	static constexpr const char* kConfigPath = ".tmp/Havana.cfg";
	static constexpr size_t kMaxRecentScenes = 10;

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
		if (scenePath.empty())
		{
			return;
		}
		RecentScenes.erase(std::remove(RecentScenes.begin(), RecentScenes.end(), scenePath), RecentScenes.end());
		RecentScenes.insert(RecentScenes.begin(), scenePath);
		if (RecentScenes.size() > kMaxRecentScenes)
		{
			RecentScenes.resize(kMaxRecentScenes);
		}
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
		if (inJson.contains("RecentScenes") && inJson["RecentScenes"].is_array())
		{
			RecentScenes.clear();
			for (const json& entry : inJson["RecentScenes"])
			{
				if (entry.is_string())
				{
					RecentScenes.push_back(entry.get<std::string>());
				}
			}
		}
		if (inJson.contains("Preferences") && inJson["Preferences"].is_object())
		{
			Preferences = inJson["Preferences"];
		}
	}

	void Save()
	{
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
		config["Preferences"] = Preferences;

		File configFile(configPath);
		configFile.Write(config.dump(4));
	}

	ME_SINGLETON_DEFINITION(EditorConfig)
};
