#include "PCH.h"
#include "InputActionsAsset.h"


InputActionsResource::InputActionsResource( const Path& InPath )
    : Resource( InPath )
{
}


bool InputActionsResource::Load()
{
    const bool loaded = Map.LoadFromFile( FilePath.FullPath );
    ++Version;
    return loaded;
}


void InputActionsResource::Reload()
{
    Load();
}


const Reflection::TypeInfo* InputActionsMetadata::GetEditableType()
{
    return &InputActionMap::StaticType();
}


void* InputActionsMetadata::GetEditableData()
{
    if( !m_loaded )
    {
        m_map.LoadFromFile( FilePath.FullPath );
        m_loaded = true;
    }
    return &m_map;
}


void InputActionsMetadata::SaveEditableData()
{
    if( m_loaded )
    {
        m_map.SaveToFile( FilePath.FullPath );
    }
}
