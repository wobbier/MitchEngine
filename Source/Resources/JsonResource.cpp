#include "PCH.h"

#include "JsonResource.h"
#include "File.h"

JsonResource::JsonResource( const Path& InFilePath )
    : Resource( InFilePath )
{
    Data = json::parse( File( InFilePath ).Read() );
}

const json& JsonResource::GetJson() const
{
    return Data;
}

