#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// One stat's worth of answers about a mount-relative path.
struct VfsStat
{
    bool Exists = false;
    bool IsFile = false;
};

// Where a mount's bytes come from. VFS-1 ships DirectoryMount; Pak and Memory sources follow
// (VFS-3) behind this same interface, which is why reads go through the source instead of
// callers opening the physical path themselves.
class IMountSource
{
public:
    virtual ~IMountSource() = default;

    // Existence and kind in one filesystem query.
    virtual VfsStat Stat( const std::string& InRelativePath ) const = 0;

    // Where InRelativePath lives for direct filesystem access. Sources that aren't
    // file-backed (paks) will return an empty string and serve bytes instead.
    virtual std::string GetPhysicalPath( const std::string& InRelativePath ) const = 0;

    // Text mode, matching File::Read's historical behavior per platform.
    virtual bool ReadText( const std::string& InRelativePath, std::string& OutText ) const = 0;

    virtual bool ReadBytes( const std::string& InRelativePath, std::vector<uint8_t>& OutBytes ) const = 0;

    // Creates missing parent directories. Only meaningful on writable mounts.
    virtual bool WriteText( const std::string& InRelativePath, const std::string& InText ) const = 0;

    // Visits every entry under InRelativeDirectory with mount-relative paths ("" = the root).
    virtual void Enumerate( const std::string& InRelativeDirectory, bool InRecursive,
                            const std::function<void( const std::string& InRelativePath, bool InIsDirectory )>& InVisitor ) const = 0;
};

// A plain directory on disk.
class DirectoryMount final
    : public IMountSource
{
public:
    explicit DirectoryMount( const std::string& InRootDirectory );

    VfsStat Stat( const std::string& InRelativePath ) const final;
    std::string GetPhysicalPath( const std::string& InRelativePath ) const final;
    bool ReadText( const std::string& InRelativePath, std::string& OutText ) const final;
    bool ReadBytes( const std::string& InRelativePath, std::vector<uint8_t>& OutBytes ) const final;
    bool WriteText( const std::string& InRelativePath, const std::string& InText ) const final;
    void Enumerate( const std::string& InRelativeDirectory, bool InRecursive,
                    const std::function<void( const std::string& InRelativePath, bool InIsDirectory )>& InVisitor ) const final;

    const std::string& GetRoot() const { return m_root; }

private:
    // Forward slashes, no trailing slash.
    std::string m_root;
};
