#include "ProjectCache.h"
#include <JSON.h>
#include <File.h>
#include "Resource\ResourceCache.h"

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


SharedPtr<Moonlight::Texture> ProjectCache::GetActiveBackgroundTexture( size_t SelectedProjectIndex )
{
    if ( !Projects[SelectedProjectIndex].BackgroundImage )
    {
        Path bgPath( Projects[SelectedProjectIndex].ProjectPath.FullPath + "/Project/Background.png" );
        Projects[SelectedProjectIndex].BackgroundImage = ResourceCache::GetInstance().Get<Moonlight::Texture>( bgPath );
    }
    return Projects[SelectedProjectIndex].BackgroundImage;
}


SharedPtr<Moonlight::Texture> ProjectCache::GetActiveTitleTexture( size_t SelectedProjectIndex )
{
    if ( !Projects[SelectedProjectIndex].TitleImage )
    {
        Path titlePath( Projects[SelectedProjectIndex].ProjectPath.FullPath + "/Project/Title.png" );
        Projects[SelectedProjectIndex].TitleImage = ResourceCache::GetInstance().Get<Moonlight::Texture>( titlePath );
    }
    return Projects[SelectedProjectIndex].TitleImage;
}
