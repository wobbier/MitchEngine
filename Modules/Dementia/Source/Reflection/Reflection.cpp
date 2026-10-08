#include "Reflection.h"
#include "CLog.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

namespace Reflection
{
    namespace
    {
        struct RegistryData
        {
            std::mutex Mutex;
            std::vector<const TypeInfo*> Types;
            std::unordered_map<std::string, const TypeInfo*> TypesByName;
            std::unordered_map<std::type_index, const EnumInfo*> Enums;
        };

        RegistryData& GetRegistryData()
        {
            static RegistryData data;
            return data;
        }


        int64_t ReadInt( const ValueOps& ops, const void* value )
        {
            // Each branch is widened separately: a ternary over int32/uint32 would convert to unsigned.
            const bool isSigned = ops.Type == PropertyType::Int;
            switch( ops.IntBytes )
            {
            case 1: return isSigned ? static_cast<int64_t>( *static_cast<const int8_t*>( value ) ) : static_cast<int64_t>( *static_cast<const uint8_t*>( value ) );
            case 2: return isSigned ? static_cast<int64_t>( *static_cast<const int16_t*>( value ) ) : static_cast<int64_t>( *static_cast<const uint16_t*>( value ) );
            case 4: return isSigned ? static_cast<int64_t>( *static_cast<const int32_t*>( value ) ) : static_cast<int64_t>( *static_cast<const uint32_t*>( value ) );
            case 8: return isSigned ? *static_cast<const int64_t*>( value ) : static_cast<int64_t>( *static_cast<const uint64_t*>( value ) );
            default: return 0;
            }
        }


        void WriteInt( const ValueOps& ops, void* value, int64_t intValue )
        {
            switch( ops.IntBytes )
            {
            case 1: *static_cast<uint8_t*>( value ) = static_cast<uint8_t>( intValue ); break;
            case 2: *static_cast<uint16_t*>( value ) = static_cast<uint16_t>( intValue ); break;
            case 4: *static_cast<uint32_t*>( value ) = static_cast<uint32_t>( intValue ); break;
            case 8: *static_cast<uint64_t*>( value ) = static_cast<uint64_t>( intValue ); break;
            default: break;
            }
        }


        bool ReadFloats( const json& in, float* out, size_t count )
        {
            if( !in.is_array() || in.size() < count )
            {
                return false;
            }
            for( size_t i = 0; i < count; ++i )
            {
                if( !in[i].is_number() )
                {
                    return false;
                }
                out[i] = in[i].get<float>();
            }
            return true;
        }
    }


    const char* ToString( PropertyType type )
    {
        switch( type )
        {
        case PropertyType::Bool: return "Bool";
        case PropertyType::Int: return "Int";
        case PropertyType::UInt: return "UInt";
        case PropertyType::Float: return "Float";
        case PropertyType::Double: return "Double";
        case PropertyType::String: return "String";
        case PropertyType::Vector2: return "Vector2";
        case PropertyType::Vector3: return "Vector3";
        case PropertyType::Vector4: return "Vector4";
        case PropertyType::Quaternion: return "Quaternion";
        case PropertyType::Enum: return "Enum";
        case PropertyType::Struct: return "Struct";
        case PropertyType::Array: return "Array";
        case PropertyType::Custom: return "Custom";
        case PropertyType::Unknown:
        default: return "Unknown";
        }
    }


    const EnumInfo* ValueOps::GetEnumInfo() const
    {
        return Registry::FindEnum( EnumType );
    }


    const FieldInfo* TypeInfo::FindField( std::string_view name ) const
    {
        for( const FieldInfo& field : Fields )
        {
            if( field.Name == name )
            {
                return &field;
            }
        }
        return GetBase ? GetBase().FindField( name ) : nullptr;
    }


    const FieldInfo* TypeInfo::FindField( std::string_view name, void* instance, void** outOwner ) const
    {
        for( const FieldInfo& field : Fields )
        {
            if( field.Name == name )
            {
                if( outOwner )
                {
                    *outOwner = instance;
                }
                return &field;
            }
        }
        return GetBase ? GetBase().FindField( name, CastToBase( instance ), outOwner ) : nullptr;
    }


    const char* EnumInfo::ToName( int64_t value ) const
    {
        for( const auto& entry : Values )
        {
            if( entry.second == value )
            {
                return entry.first.c_str();
            }
        }
        return nullptr;
    }


    bool EnumInfo::FromName( std::string_view name, int64_t& outValue ) const
    {
        for( const auto& entry : Values )
        {
            if( entry.first == name )
            {
                outValue = entry.second;
                return true;
            }
        }
        return false;
    }


    const TypeInfo* Registry::FindType( std::string_view name )
    {
        RegistryData& data = GetRegistryData();
        std::lock_guard<std::mutex> lock( data.Mutex );
        auto it = data.TypesByName.find( std::string( name ) );
        return it != data.TypesByName.end() ? it->second : nullptr;
    }


    const std::vector<const TypeInfo*>& Registry::GetAllTypes()
    {
        return GetRegistryData().Types;
    }


    void Registry::RegisterType( TypeInfo* type )
    {
        RegistryData& data = GetRegistryData();
        std::lock_guard<std::mutex> lock( data.Mutex );
        if( data.TypesByName.find( type->Name ) == data.TypesByName.end() )
        {
            data.Types.push_back( type );
            data.TypesByName[type->Name] = type;
        }
    }


    void Registry::RegisterEnum( std::type_index type, EnumInfo* info )
    {
        RegistryData& data = GetRegistryData();
        std::lock_guard<std::mutex> lock( data.Mutex );
        data.Enums[type] = info;
    }


    const EnumInfo* Registry::FindEnum( std::type_index type )
    {
        RegistryData& data = GetRegistryData();
        std::lock_guard<std::mutex> lock( data.Mutex );
        auto it = data.Enums.find( type );
        return it != data.Enums.end() ? it->second : nullptr;
    }


    namespace Detail
    {
        std::string PrettifyName( std::string_view name )
        {
            // Strip common member prefixes: m_Foo, mFoo, _foo.
            if( name.size() > 2 && name[0] == 'm' && name[1] == '_' )
            {
                name.remove_prefix( 2 );
            }
            else if( name.size() > 1 && name[0] == 'm' && std::isupper( static_cast<unsigned char>( name[1] ) ) )
            {
                name.remove_prefix( 1 );
            }
            while( !name.empty() && name.front() == '_' )
            {
                name.remove_prefix( 1 );
            }

            std::string result;
            result.reserve( name.size() + 8 );
            for( size_t i = 0; i < name.size(); ++i )
            {
                const char c = name[i];
                if( c == '_' )
                {
                    if( !result.empty() && result.back() != ' ' )
                    {
                        result.push_back( ' ' );
                    }
                    continue;
                }

                const bool isUpper = std::isupper( static_cast<unsigned char>( c ) ) != 0;
                const bool isDigit = std::isdigit( static_cast<unsigned char>( c ) ) != 0;
                if( i > 0 && !result.empty() && result.back() != ' ' )
                {
                    const char prev = name[i - 1];
                    const bool prevLower = std::islower( static_cast<unsigned char>( prev ) ) != 0;
                    const bool prevUpper = std::isupper( static_cast<unsigned char>( prev ) ) != 0;
                    const bool nextLower = i + 1 < name.size() && std::islower( static_cast<unsigned char>( name[i + 1] ) );
                    const bool prevDigit = std::isdigit( static_cast<unsigned char>( prev ) ) != 0;
                    // camelCase -> "camel Case", HTTPServer -> "HTTP Server", Value2 -> "Value 2"
                    if( ( isUpper && ( prevLower || prevDigit || ( prevUpper && nextLower ) ) ) || ( isDigit && !prevDigit ) )
                    {
                        result.push_back( ' ' );
                    }
                }
                result.push_back( result.empty() ? static_cast<char>( std::toupper( static_cast<unsigned char>( c ) ) ) : c );
            }
            return result;
        }
    }


    void ValueToJson( const ValueOps& ops, const void* value, json& out )
    {
        switch( ops.Type )
        {
        case PropertyType::Bool:
            out = *static_cast<const bool*>( value );
            break;
        case PropertyType::Int:
            out = ReadInt( ops, value );
            break;
        case PropertyType::UInt:
            out = static_cast<uint64_t>( ReadInt( ops, value ) );
            break;
        case PropertyType::Float:
            out = *static_cast<const float*>( value );
            break;
        case PropertyType::Double:
            out = *static_cast<const double*>( value );
            break;
        case PropertyType::String:
            out = *static_cast<const std::string*>( value );
            break;
        case PropertyType::Vector2:
        {
            const ::Vector2& v = *static_cast<const ::Vector2*>( value );
            out = json::array( { v.x, v.y } );
            break;
        }
        case PropertyType::Vector3:
        {
            const ::Vector3& v = *static_cast<const ::Vector3*>( value );
            out = json::array( { v.x, v.y, v.z } );
            break;
        }
        case PropertyType::Vector4:
        {
            const ::Vector4& v = *static_cast<const ::Vector4*>( value );
            out = json::array( { v.x, v.y, v.z, v.w } );
            break;
        }
        case PropertyType::Quaternion:
        {
            const ::Quaternion& q = *static_cast<const ::Quaternion*>( value );
            out = json::array( { q.x, q.y, q.z, q.w } );
            break;
        }
        case PropertyType::Enum:
        {
            const int64_t enumValue = ops.GetEnum( value );
            const EnumInfo* info = ops.GetEnumInfo();
            const char* name = info ? info->ToName( enumValue ) : nullptr;
            if( name )
            {
                out = name;
            }
            else
            {
                out = enumValue;
            }
            break;
        }
        case PropertyType::Struct:
        {
            out = json::object();
            ToJson( ops.GetStructType(), value, out );
            break;
        }
        case PropertyType::Array:
        {
            out = json::array();
            const size_t count = ops.ArraySize( value );
            for( size_t i = 0; i < count; ++i )
            {
                json element;
                ValueToJson( *ops.Element, ops.ArrayAt( const_cast<void*>( value ), i ), element );
                out.push_back( std::move( element ) );
            }
            break;
        }
        case PropertyType::Custom:
            ops.CustomToJson( value, out );
            break;
        case PropertyType::Unknown:
        default:
            out = nullptr;
            break;
        }
    }


    bool ValueFromJson( const ValueOps& ops, void* value, const json& in )
    {
        switch( ops.Type )
        {
        case PropertyType::Bool:
            if( in.is_boolean() )
            {
                *static_cast<bool*>( value ) = in.get<bool>();
                return true;
            }
            if( in.is_number() )
            {
                *static_cast<bool*>( value ) = in.get<double>() != 0.0;
                return true;
            }
            return false;
        case PropertyType::Int:
        case PropertyType::UInt:
            if( in.is_number() )
            {
                WriteInt( ops, value, in.is_number_float() ? static_cast<int64_t>( in.get<double>() ) : in.get<int64_t>() );
                return true;
            }
            if( in.is_boolean() )
            {
                WriteInt( ops, value, in.get<bool>() ? 1 : 0 );
                return true;
            }
            return false;
        case PropertyType::Float:
            if( in.is_number() )
            {
                *static_cast<float*>( value ) = in.get<float>();
                return true;
            }
            return false;
        case PropertyType::Double:
            if( in.is_number() )
            {
                *static_cast<double*>( value ) = in.get<double>();
                return true;
            }
            return false;
        case PropertyType::String:
            if( in.is_string() )
            {
                *static_cast<std::string*>( value ) = in.get<std::string>();
                return true;
            }
            return false;
        case PropertyType::Vector2:
            return ReadFloats( in, &static_cast<::Vector2*>( value )->x, 2 );
        case PropertyType::Vector3:
            return ReadFloats( in, &static_cast<::Vector3*>( value )->x, 3 );
        case PropertyType::Vector4:
            return ReadFloats( in, &static_cast<::Vector4*>( value )->x, 4 );
        case PropertyType::Quaternion:
        {
            float q[4];
            if( !ReadFloats( in, q, 4 ) )
            {
                return false;
            }
            *static_cast<::Quaternion*>( value ) = ::Quaternion( q[0], q[1], q[2], q[3] );
            return true;
        }
        case PropertyType::Enum:
        {
            if( in.is_string() )
            {
                const EnumInfo* info = ops.GetEnumInfo();
                int64_t enumValue = 0;
                if( info && info->FromName( in.get<std::string>(), enumValue ) )
                {
                    ops.SetEnum( value, enumValue );
                    return true;
                }
                return false;
            }
            if( in.is_number_integer() )
            {
                ops.SetEnum( value, in.get<int64_t>() );
                return true;
            }
            return false;
        }
        case PropertyType::Struct:
            if( in.is_object() )
            {
                FromJson( ops.GetStructType(), value, in );
                return true;
            }
            return false;
        case PropertyType::Array:
        {
            if( !in.is_array() )
            {
                return false;
            }
            ops.ArrayResize( value, in.size() );
            for( size_t i = 0; i < in.size(); ++i )
            {
                ValueFromJson( *ops.Element, ops.ArrayAt( value, i ), in[i] );
            }
            return true;
        }
        case PropertyType::Custom:
            return ops.CustomFromJson( value, in );
        case PropertyType::Unknown:
        default:
            return false;
        }
    }


    void ToJson( const TypeInfo& type, const void* instance, json& out )
    {
        if( !out.is_object() )
        {
            out = json::object();
        }
        type.ForEachField( instance, [&out]( const FieldInfo& field, const void* owner ) {
            if( field.Has( FieldFlags::NoSerialize ) )
            {
                return;
            }
            ValueToJson( *field.Ops, field.Get( owner ), out[field.Name] );
        } );
    }


    void FromJson( const TypeInfo& type, void* instance, const json& in )
    {
        if( !in.is_object() )
        {
            return;
        }
        type.ForEachField( instance, [&in, &type]( const FieldInfo& field, void* owner ) {
            if( field.Has( FieldFlags::NoSerialize ) )
            {
                return;
            }
            auto it = in.find( field.Name );
            if( it == in.end() || it->is_null() )
            {
                return;
            }
            if( !ValueFromJson( *field.Ops, field.Get( owner ), *it ) )
            {
                CLog::Log( CLog::LogType::Warning, "[Reflection] " + type.Name + "." + field.Name + ": expected " + ToString( field.GetType() ) + ", got " + it->type_name() );
            }
        } );
    }


    PropertyRef ResolvePath( const TypeInfo& rootType, void* instance, std::string_view path )
    {
        PropertyRef result;
        const TypeInfo* currentType = &rootType;
        void* current = instance;

        while( !path.empty() )
        {
            // Field name segment.
            size_t end = path.find_first_of( ".[" );
            const std::string_view name = path.substr( 0, end );
            path = end == std::string_view::npos ? std::string_view() : path.substr( end );

            if( !currentType )
            {
                return {};
            }
            void* owner = nullptr;
            const FieldInfo* field = currentType->FindField( name, current, &owner );
            if( !field )
            {
                return {};
            }
            result.Field = field;
            result.Ops = field->Ops;
            result.Value = field->Get( owner );

            // Any number of [index] segments.
            while( !path.empty() && path.front() == '[' )
            {
                const size_t close = path.find( ']' );
                if( close == std::string_view::npos || result.Ops->Type != PropertyType::Array )
                {
                    return {};
                }
                const size_t index = static_cast<size_t>( std::strtoull( std::string( path.substr( 1, close - 1 ) ).c_str(), nullptr, 10 ) );
                if( index >= result.Ops->ArraySize( result.Value ) )
                {
                    return {};
                }
                result.Value = result.Ops->ArrayAt( result.Value, index );
                result.Ops = result.Ops->Element;
                path.remove_prefix( close + 1 );
            }

            if( !path.empty() && path.front() == '.' )
            {
                path.remove_prefix( 1 );
                if( result.Ops->Type != PropertyType::Struct )
                {
                    return {};
                }
                currentType = &result.Ops->GetStructType();
                current = result.Value;
            }
        }
        return result;
    }


    json GetPathJson( const TypeInfo& type, const void* instance, std::string_view path )
    {
        PropertyRef ref = ResolvePath( type, const_cast<void*>( instance ), path );
        json out;
        if( ref )
        {
            ValueToJson( *ref.Ops, ref.Value, out );
        }
        return out;
    }


    bool SetPathJson( const TypeInfo& type, void* instance, std::string_view path, const json& value )
    {
        PropertyRef ref = ResolvePath( type, instance, path );
        return ref && ValueFromJson( *ref.Ops, ref.Value, value );
    }


    std::vector<std::string> DiffFields( const TypeInfo& type, const void* a, const void* b )
    {
        json jsonA;
        json jsonB;
        ToJson( type, a, jsonA );
        ToJson( type, b, jsonB );

        std::vector<std::string> differing;
        for( auto it = jsonA.begin(); it != jsonA.end(); ++it )
        {
            auto other = jsonB.find( it.key() );
            if( other == jsonB.end() || *other != it.value() )
            {
                differing.push_back( it.key() );
            }
        }
        return differing;
    }


    void CopyFields( const TypeInfo& type, const void* src, void* dst )
    {
        json values;
        ToJson( type, src, values );
        FromJson( type, dst, values );
    }
}
