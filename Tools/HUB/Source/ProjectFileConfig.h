#pragma once
#include <JSON.h>
#include "File.h"
#include "Core\Assert.h"

class ProjectFileConfig 
{
public:
    ProjectFileConfig() = default;

    ProjectFileConfig( const Path& inPath )
        : ConfigFile( inPath )
    {
        File j( inPath );
        ME_ASSERT_MSG( inPath.Exists, "Failed to load project config.");
        if( !inPath.Exists )
        {
            YIKES("Project file doesn't exist");
            return;
        }

        OnLoad( json::parse( j.Read() ) );
    }

    void OnSave( json& outJson )
    {

        throw std::logic_error( "The method or operation is not implemented." );
    }


    void OnLoad( const json& inJson )
    {
        if( inJson.contains("ProjectName") )
        {
            Name = inJson["ProjectName"];
        }
        else
        {
            Name = ConfigFile.GetLocalPath();
        }
    }

    std::string Name;
    Path ConfigFile;
};