#include "MetaFile.h"
#include "File.h"
#include "Dementia.h"
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include "AssetDatabase.h"
#include "Utils/GUID.h"

MetaBase::MetaBase( const Path& filePath )
    : FilePath( filePath )
{
    struct stat fileInfo;

    if( stat( filePath.FullPath.c_str(), &fileInfo ) != 0 ) {  // Use stat() to get the info
        //std::cerr << "Error: " << strerror(errno) << '\n';
    }

    //std::cout << "Type:         : ";
    //if ((fileInfo.st_mode & S_IFMT) == S_IFDIR) { // From sys/types.h
    //	std::cout << "Directory\n";
    //}
    //else {
    //	std::cout << "File\n";
    //}

    //std::cout << "Size          : " <<
    //	fileInfo.st_size << '\n';               // Size in bytes
    //std::cout << "Device        : " <<
    //	(char)(fileInfo.st_dev + 'A') << '\n';  // Device number
    //std::cout << "Created       : " <<
    //	std::ctime(&fileInfo.st_ctime);         // Creation time
    //std::cout << "Modified      : " <<
    //	std::ctime(&fileInfo.st_mtime);         // Last mod time

    LastModified = static_cast<long>( fileInfo.st_mtime );
#if USING( ME_PLATFORM_WINDOWS )
    char str[26];
    ctime_s( str, sizeof str, &fileInfo.st_mtime );
    LastModifiedDebug = std::string( str );// std::ctime(&fileInfo.st_mtime);
#endif
}

void MetaBase::Serialize( json& outJson )
{
    outJson["FileType"] = FilePath.GetExtension();
    if( GUID == 0 )
    {
        GUID = ::GUID::Generate();
    }
    char guidText[17];
    std::snprintf( guidText, sizeof( guidText ), "%016llx", static_cast<unsigned long long>( GUID ) );
    outJson["GUID"] = guidText;
    AssetDatabase::Get().Register( FilePath.GetLocalPathString(), GUID );
    //outJson["LastModified"] = LastModified;
    //outJson["LastModifiedDebug"] = LastModifiedDebug;
    //outJson["LastModified"] = buffer;
    OnSerialize( outJson );
}

void MetaBase::Deserialize( const json& inJson )
{
    if( inJson.contains( "FileType" ) )
    {
        FileType = inJson["FileType"];
    }
    if( inJson.contains( "GUID" ) && inJson["GUID"].is_string() )
    {
        GUID = std::strtoull( inJson["GUID"].get<std::string>().c_str(), nullptr, 16 );
        AssetDatabase::Get().Register( FilePath.GetLocalPathString(), GUID );
    }

    /*bool wasModified = true;
    if (inJson.contains("LastModified"))
    {
        long CachedLastModified = inJson["LastModified"];
        wasModified = (LastModified != CachedLastModified);
    }*/

    //FlaggedForExport = wasModified;
    /*
    if (inJson.contains("LastModifiedDebug"))
    {
        LastModifiedDebug = inJson["LastModifiedDebug"];
    }*/
    OnDeserialize( inJson );
}

void MetaBase::Save()
{
    // Raw: the .meta always lives next to its asset (engine assets included), never redirected.
    Path metaPath = Path( FilePath.FullPath + ".meta", true );
    File metaFile = File( metaPath );
    json j;
    Serialize( j );
    const std::string contents = j.dump( 4 );
    // Rewriting an unchanged .meta would look like an edit to the file watcher and reimport the
    // asset again (an endless reload loop for hot reloaded shaders).
    if( !metaPath.Exists || metaFile.Read() != contents )
    {
        metaFile.Write( contents );
    }
    FlaggedForExport = false;
}
