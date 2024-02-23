#include "MetaFile.h"
#include "File.h"
#include "Dementia.h"

MetaBase::MetaBase( const Path& filePath )
    : FilePath( filePath )
{
#if USING( ME_PLATFORM_WINDOWS )
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
    char str[26];
    ctime_s( str, sizeof str, &fileInfo.st_mtime );
    LastModifiedDebug = std::string( str );// std::ctime(&fileInfo.st_mtime);
#endif
}

void MetaBase::Serialize( json& outJson )
{
    outJson["FileType"] = FilePath.GetExtension();
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
    Path metaPath = Path( FilePath.FullPath + ".meta" );
    File metaFile = File( metaPath );
    json j;
    Serialize( j );
    metaFile.Write( j.dump( 4 ) );
    FlaggedForExport = false;
}
