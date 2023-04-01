#include "PCH.h"
#include "MonoUtils.h"

#if USING( ME_SCRIPTING )

#include <mono\metadata\class.h>
#include <mono\metadata\attrdefs.h>
#include <mono\metadata\object.h>
#include "Utils\PlatformUtils.h"
#include <mono\metadata\mono-debug.h>
#include <mono\metadata\assembly.h>
#include <Core\Buffer.h>

namespace MonoUtils
{
    static std::unordered_map<std::string, ScriptFieldType> s_scriptFieldTypeMap =
    {
        { "System.Single" , ScriptFieldType::Float },
        { "System.Double" , ScriptFieldType::Double },
        { "System.Boolean" , ScriptFieldType::Bool },
        { "System.Char" , ScriptFieldType::Char },
        { "System.Int16" , ScriptFieldType::Short },
        { "System.Int32" , ScriptFieldType::Int },
        { "System.Int64" , ScriptFieldType::Long },
        { "System.Byte" , ScriptFieldType::Byte },
        { "System.UInt16" , ScriptFieldType::UShort },
        { "System.UInt32" , ScriptFieldType::UInt },
        { "System.UInt64" , ScriptFieldType::ULong },
        { "Vector2" , ScriptFieldType::Vector2 },
        { "Vector3" , ScriptFieldType::Vector3 },
        { "Vector4" , ScriptFieldType::Vector4 },
        { "Entity" , ScriptFieldType::Entity },
    };


    ScriptFieldType MonoTypeToScriptFieldType( MonoType* type )
    {
        std::string typeName = mono_type_get_name( type );
        if ( s_scriptFieldTypeMap.find( typeName ) == s_scriptFieldTypeMap.end() )
            return ScriptFieldType::None;
        return s_scriptFieldTypeMap.at( typeName );
    }


    std::string ScriptFieldTypeToString( ScriptFieldType type )
    {
        switch ( type )
        {
        case MonoUtils::ScriptFieldType::None: return "None";
        case MonoUtils::ScriptFieldType::Float: return "Float";
        case MonoUtils::ScriptFieldType::Double: return "Double";
        case MonoUtils::ScriptFieldType::Bool: return "Bool";
        case MonoUtils::ScriptFieldType::Char: return "Char";
        case MonoUtils::ScriptFieldType::Byte: return "Byte";
        case MonoUtils::ScriptFieldType::Short: return "Short";
        case MonoUtils::ScriptFieldType::Int: return "Int";
        case MonoUtils::ScriptFieldType::Long: return "Long";
        case MonoUtils::ScriptFieldType::UByte: return "UByte";
        case MonoUtils::ScriptFieldType::UShort: return "UShort";
        case MonoUtils::ScriptFieldType::UInt: return "UInt";
        case MonoUtils::ScriptFieldType::ULong: return "ULong";
        case MonoUtils::ScriptFieldType::Vector2: return "Vector2";
        case MonoUtils::ScriptFieldType::Vector3: return "Vector3";
        case MonoUtils::ScriptFieldType::Vector4: return "Vector4";
        case MonoUtils::ScriptFieldType::Entity: return "Entity";
        default:
            break;
        }
    }

    uint8_t GetFieldAccessibility( MonoClassField* field )
    {
        uint8_t accessibility = Accessibility::None;
        uint32_t accessFlag = mono_field_get_flags( field ) & MONO_FIELD_ATTR_FIELD_ACCESS_MASK;

        switch ( accessFlag )
        {
        case MONO_FIELD_ATTR_PRIVATE:
        {
            accessibility = Accessibility::Private;
            break;
        }
        case MONO_FIELD_ATTR_FAM_AND_ASSEM:
        {
            accessibility |= Accessibility::Protected;
            accessibility |= Accessibility::Internal;
            break;
        }
        case MONO_FIELD_ATTR_ASSEMBLY:
        {
            accessibility = Accessibility::Internal;
            break;
        }
        case MONO_FIELD_ATTR_FAMILY:
        {
            accessibility = Accessibility::Protected;
            break;
        }
        case MONO_FIELD_ATTR_FAM_OR_ASSEM:
        {
            accessibility |= Accessibility::Private;
            accessibility |= Accessibility::Protected;
            break;
        }
        case MONO_FIELD_ATTR_PUBLIC:
        {
            accessibility = Accessibility::Public;
            break;
        }
        default:
            YIKES( "Unhandled MonoClassField*" );
            break;
        }

        return accessibility;
    }


    uint8_t GetPropertyAccessibility( MonoProperty* property )
    {
        uint8_t accessibility = Accessibility::None;

        // Get a reference to the property's getter method
        MonoMethod* propertyGetter = mono_property_get_get_method( property );
        if ( propertyGetter != nullptr )
        {
            // Extract the access flags from the getters flags
            uint32_t accessFlag = mono_method_get_flags( propertyGetter, nullptr ) & MONO_METHOD_ATTR_ACCESS_MASK;

            switch ( accessFlag )
            {
            case MONO_FIELD_ATTR_PRIVATE:
            {
                accessibility = Accessibility::Private;
                break;
            }
            case MONO_FIELD_ATTR_FAM_AND_ASSEM:
            {
                accessibility |= Accessibility::Protected;
                accessibility |= Accessibility::Internal;
                break;
            }
            case MONO_FIELD_ATTR_ASSEMBLY:
            {
                accessibility = Accessibility::Internal;
                break;
            }
            case MONO_FIELD_ATTR_FAMILY:
            {
                accessibility = Accessibility::Protected;
                break;
            }
            case MONO_FIELD_ATTR_FAM_OR_ASSEM:
            {
                accessibility |= Accessibility::Private;
                accessibility |= Accessibility::Protected;
                break;
            }
            case MONO_FIELD_ATTR_PUBLIC:
            {
                accessibility = Accessibility::Public;
                break;
            }
            default:
                YIKES( "Unhandled MonoProperty*" );
                break;
            }
        }

        // Get a reference to the property's setter method
        MonoMethod* propertySetter = mono_property_get_set_method( property );
        if ( propertySetter != nullptr )
        {
            // Extract the access flags from the setters flags
            uint32_t accessFlag = mono_method_get_flags( propertySetter, nullptr ) & MONO_METHOD_ATTR_ACCESS_MASK;
            if ( accessFlag != MONO_FIELD_ATTR_PUBLIC )
                accessibility = Accessibility::Private;
        }
        else
        {
            accessibility = Accessibility::Private;
        }

        return accessibility;
    }


    bool CheckMonoError( MonoError& error )
    {
        bool hasError = !mono_error_ok( &error );
        if ( hasError )
        {
            unsigned short errorCode = mono_error_get_error_code( &error );
            const char* errorMessage = mono_error_get_message( &error );
            printf( "Mono Error!\n" );
            printf( "\tError Code: %hu\n", errorCode );
            printf( "\tError Message: %s\n", errorMessage );
            mono_error_cleanup( &error );
        }
        return hasError;
    }

    std::string MonoStringToUTF8( MonoString* monoString )
    {
        if ( monoString == nullptr || mono_string_length( monoString ) == 0 )
            return "";

        MonoError error;
        char* utf8 = mono_string_to_utf8_checked( monoString, &error );
        if ( CheckMonoError( error ) )
            return "";
        std::string result( utf8 );
        mono_free( utf8 );
        return result;
    }

    MonoAssembly* LoadMonoAssembly( const Path& path, bool loadPDB )
    {
        Buffer fileData = PlatformUtils::ReadBytes( path );

        MonoImageOpenStatus status;
        MonoImage* image = mono_image_open_from_data_full( (char*)fileData.Data, fileData.Size, 1, &status, 0 );

        fileData.Release();

        if ( status != MONO_IMAGE_OK )
        {
            const char* errorMessage = mono_image_strerror( status );
            YIKES( errorMessage );
            return nullptr;
        }

        // TODO: Write a file extension replacement function.
#if !USING( ME_PLATFORM_UWP )
        if ( loadPDB )
        {
            std::filesystem::path pdbPath = path.FullPath;
            pdbPath.replace_extension( ".pdb" );
            
            if ( std::filesystem::exists( pdbPath ) )
            {
                Buffer pdbBuff = PlatformUtils::ReadBytes( Path((char*)pdbPath.c_str()) );
                mono_debug_open_image_from_memory( image, (const mono_byte*)pdbBuff.Data, pdbBuff.Size );
            }
        }
#endif

        MonoAssembly* assembly = mono_assembly_load_from_full( image, path.FullPath.c_str(), &status, 0 );

        mono_image_close( image );

        if ( !assembly )
        {
            YIKES( "mono_assembly_load_from_full failed" );
            return nullptr;
        }

        return assembly;
    }
}

#endif
