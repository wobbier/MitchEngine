#pragma once

#include <JSON.h>
#include <Math/Vector3.h>
#include <Singleton.h>
#include <Path.h>
#include <Utils/PlatformUtils.h>
#include <Utils/StringUtils.h>

class EditorConfig
{
	static constexpr const char* kConfigPath = ".tmp/Havana.cfg";

public:
	Vector3 CameraPosition;
	Vector3 CameraRotation;

	EditorConfig() = default;

	struct EditorWidgetProperties
	{
		bool IsVisible = false;
	};

	std::map<std::string, EditorWidgetProperties> PanelVisibility;

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

		if (configFile.Data.length() > 0)
		{
			json inJson;
			inJson = json::parse(configFile.Data);

			if (inJson.contains("CameraPosition"))
			{
				CameraPosition = Vector3((float)inJson["CameraPosition"][0], (float)inJson["CameraPosition"][1], (float)inJson["CameraPosition"][2]);
			}
			if (inJson.contains("CameraRotation"))
			{
				CameraRotation = Vector3((float)inJson["CameraRotation"][0], (float)inJson["CameraRotation"][1], (float)inJson["CameraRotation"][2]);
			}

			if (inJson.contains("WidgetProperties"))
			{
				for (auto& i : inJson["WidgetProperties"].items())
				{
					//if (i.contains("Name"))
					{
						PanelVisibility[i.key()] = { i.value()["IsVisible"] };
					}
				}
			}
		}
	}

	void Save()
	{
		Path configPath(kConfigPath);

		json config;
		{
			config["CameraPosition"] = { CameraPosition.x, CameraPosition.y, CameraPosition.z };
			config["CameraRotation"] = { CameraRotation.x, CameraRotation.y, CameraRotation.z };
		}
		json& son = config["WidgetProperties"] = json();
		for (auto thing : PanelVisibility)
		{
			json thing2;
			thing2["Name"] = thing.first;
			thing2["IsVisible"] = thing.second.IsVisible;
			son[thing.first] = thing2;
		}

		File thingg(configPath);
		thingg.Write(config.dump(4));
	}

	ME_SINGLETON_DEFINITION(EditorConfig)
};