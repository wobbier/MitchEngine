#include "TreeAsset.h"
#include "CLog.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace MitchTree
{
    bool LoadRecipe( const std::string& InFullPath, TreeRecipe& OutRecipe, std::string& OutError )
    {
        std::ifstream file( InFullPath, std::ios::binary );
        if( !file )
        {
            OutError = "Can't open " + InFullPath;
            return false;
        }
        std::stringstream text;
        text << file.rdbuf();
        const json parsed = json::parse( text.str(), nullptr, false );
        if( parsed.is_discarded() )
        {
            OutError = InFullPath + " is not valid JSON.";
            return false;
        }
        return OutRecipe.FromJson( parsed, OutError );
    }


    bool SaveRecipe( const std::string& InFullPath, const TreeRecipe& InRecipe, std::string& OutError )
    {
        namespace fs = std::filesystem;
        const fs::path destination( InFullPath );
        const fs::path staged = destination.string() + ".saving";
        {
            std::ofstream file( staged, std::ios::binary | std::ios::trunc );
            if( !file )
            {
                OutError = "Can't write " + staged.string();
                return false;
            }
            file << InRecipe.ToJson().dump( 4 ) << '\n';
            if( !file.good() )
            {
                OutError = "Writing " + staged.string() + " failed.";
                return false;
            }
        }
        std::error_code error;
        fs::rename( staged, destination, error );
        if( error )
        {
            fs::remove( staged, error );
            OutError = "Can't replace " + destination.string() + ".";
            return false;
        }
        return true;
    }
}


const Reflection::TypeInfo* TreeMetadata::GetEditableType()
{
    return &MitchTree::TreeRecipe::StaticType();
}


void* TreeMetadata::GetEditableData()
{
    if( !m_loaded )
    {
        std::string error;
        if( !MitchTree::LoadRecipe( FilePath.FullPath, m_recipe, error ) ) CLog::Log( CLog::LogType::Warning, error );
        m_loaded = true;
    }
    return &m_recipe;
}


void TreeMetadata::SaveEditableData()
{
    m_recipe.Sanitize();
    std::string error;
    if( !MitchTree::SaveRecipe( FilePath.FullPath, m_recipe, error ) ) CLog::Log( CLog::LogType::Error, error );
}
