#include "ProjectCache.h"
#include <JSON.h>
#include <File.h>

void ProjectCache::Load()
{
	Path configPath(kCachePath);
	if (configPath.Exists)
	{
		File configFile(configPath);
		json j = json::parse(configFile.Read());
		if (j.contains("Projects"))
		{
			json& p = j["Projects"];
			for (auto& i : p)
			{
				if (i.contains("Location"))
				{
					ProjectEntry e;
					e.ProjectPath = Path(i["Location"]);
					Projects.push_back(e);
				}
			}
		}
	}
}

void ProjectCache::Save()
{
	json j;
	json& n = j["Projects"];
	for (auto i : Projects)
	{
		if (i.ProjectPath.Exists)
		{
			json wtf;
			wtf["Location"] = i.ProjectPath.FullPath;
			n.push_back(wtf);
		}
	}
	File(Path(kCachePath)).Write(j.dump());
}
