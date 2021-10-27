#pragma once

#include <JSON.h>
#include <Math/Vector3.h>
#include <Singleton.h>
#include <Path.h>
#include <Utils/PlatformUtils.h>
#include <Utils/StringUtils.h>

class EditorConfig
{
	static constexpr char* kConfigPath = ".tmp/Havana.cfg";

public:
	Vector3 CameraPosition;
	Vector3 CameraRotation;

	EditorConfig() = default;

	void Init()
	{
		Path gameSpecificCnfigPath(kConfigPath);

		if (gameSpecificCnfigPath.FullPath.rfind("Engine") != -1)
		{
			Path editorCfgPath(kConfigPath, true);
			if (!editorCfgPath.Exists)
			{
				PlatformUtils::CreateDirectory(editorCfgPath);
				File gameEngineCfg = File(gameSpecificCnfigPath);
				File newGameConfig(editorCfgPath);
				newGameConfig.Write(gameEngineCfg.Read());
			}
		}
		else if (!gameSpecificCnfigPath.Exists)
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

		File thingg(configPath);
		thingg.Write(config.dump(4));
	}

	ME_SINGLETON_DEFINITION(EditorConfig)
};