#include "VFS/Mount.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace
{
    std::string NormalizeDirectory( const std::string& InDirectory )
    {
        // Paths hand out absolute FullPaths, so roots must be absolute too.
        std::error_code ec;
        std::string root = std::filesystem::absolute( InDirectory, ec ).lexically_normal().generic_string();
        if( ec )
        {
            root = InDirectory;
        }
        std::replace( root.begin(), root.end(), '\\', '/' );
        while( root.size() > 1 && root.back() == '/' )
        {
            root.pop_back();
        }
        return root;
    }
}

bool IMountSource::GetStreamWindow( const std::string& InRelativePath, VfsStreamWindow& OutWindow ) const
{
    const std::string physical = GetPhysicalPath( InRelativePath );
    if( physical.empty() )
    {
        return false;
    }
    std::error_code ec;
    const uint64_t size = std::filesystem::file_size( physical, ec );
    if( ec )
    {
        return false;
    }
    OutWindow.PhysicalFile = physical;
    OutWindow.Offset = 0;
    OutWindow.Size = size;
    return true;
}


DirectoryMount::DirectoryMount( const std::string& InRootDirectory )
    : m_root( NormalizeDirectory( InRootDirectory ) )
{
}


VfsStat DirectoryMount::Stat( const std::string& InRelativePath ) const
{
    VfsStat stat;
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::status( GetPhysicalPath( InRelativePath ), ec );
    stat.Exists = !ec && std::filesystem::exists( status );
    stat.IsFile = stat.Exists && std::filesystem::is_regular_file( status );
    return stat;
}


std::string DirectoryMount::GetPhysicalPath( const std::string& InRelativePath ) const
{
    if( InRelativePath.empty() )
    {
        return m_root;
    }
    return m_root + "/" + InRelativePath;
}


bool DirectoryMount::ReadText( const std::string& InRelativePath, std::string& OutText ) const
{
    // Text mode on purpose: this is File::Read's historical behavior.
    std::ifstream stream( GetPhysicalPath( InRelativePath ), std::ios::in );
    if( !stream )
    {
        return false;
    }
    OutText.assign( ( std::istreambuf_iterator<char>( stream ) ), ( std::istreambuf_iterator<char>() ) );
    return true;
}


bool DirectoryMount::ReadBytes( const std::string& InRelativePath, std::vector<uint8_t>& OutBytes ) const
{
    std::ifstream stream( GetPhysicalPath( InRelativePath ), std::ios::in | std::ios::binary );
    if( !stream )
    {
        return false;
    }
    stream.seekg( 0, std::ios::end );
    const std::streamoff size = stream.tellg();
    stream.seekg( 0, std::ios::beg );
    OutBytes.resize( size > 0 ? static_cast<size_t>( size ) : 0 );
    if( !OutBytes.empty() )
    {
        stream.read( reinterpret_cast<char*>( OutBytes.data() ), OutBytes.size() );
    }
    return stream.good() || stream.eof();
}


bool DirectoryMount::WriteText( const std::string& InRelativePath, const std::string& InText ) const
{
    const std::string physical = GetPhysicalPath( InRelativePath );
    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path( physical ).parent_path();
    if( !parent.empty() )
    {
        std::filesystem::create_directories( parent, ec );
    }
    std::ofstream stream( physical );
    if( !stream )
    {
        return false;
    }
    stream << InText;
    return stream.good();
}


void DirectoryMount::Enumerate( const std::string& InRelativeDirectory, bool InRecursive,
                                const std::function<void( const std::string& InRelativePath, bool InIsDirectory )>& InVisitor ) const
{
    const std::string physical = GetPhysicalPath( InRelativeDirectory );
    std::error_code ec;
    if( !std::filesystem::is_directory( physical, ec ) )
    {
        return;
    }

    const std::string prefix = InRelativeDirectory.empty() ? std::string() : InRelativeDirectory + "/";
    auto visit = [&]( const std::filesystem::directory_entry& InEntry )
    {
        std::error_code entryEc;
        std::string relative = std::filesystem::relative( InEntry.path(), physical, entryEc ).generic_string();
        if( entryEc || relative.empty() || relative == "." )
        {
            return;
        }
        InVisitor( prefix + relative, InEntry.is_directory( entryEc ) );
    };

    if( InRecursive )
    {
        for( auto it = std::filesystem::recursive_directory_iterator( physical, ec ); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment( ec ) )
        {
            visit( *it );
        }
    }
    else
    {
        for( auto it = std::filesystem::directory_iterator( physical, ec ); !ec && it != std::filesystem::directory_iterator(); it.increment( ec ) )
        {
            visit( *it );
        }
    }
}
