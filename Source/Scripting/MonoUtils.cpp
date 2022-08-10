#include "PCH.h"
#include "MonoUtils.h"

#include <mono\metadata\class.h>
#include <mono\metadata\attrdefs.h>
#include <mono\metadata\object.h>

namespace MonoUtils
{
    uint8_t GetFieldAccessibility( MonoClassField* field )
    {
        uint8_t accessibility = (uint8_t)Accessibility::None;
        uint32_t accessFlag = mono_field_get_flags( field ) & MONO_FIELD_ATTR_FIELD_ACCESS_MASK;

        switch ( accessFlag )
        {
        case MONO_FIELD_ATTR_PRIVATE:
        {
            accessibility = (uint8_t)Accessibility::Private;
            break;
        }
        case MONO_FIELD_ATTR_FAM_AND_ASSEM:
        {
            accessibility |= (uint8_t)Accessibility::Protected;
            accessibility |= (uint8_t)Accessibility::Internal;
            break;
        }
        case MONO_FIELD_ATTR_ASSEMBLY:
        {
            accessibility = (uint8_t)Accessibility::Internal;
            break;
        }
        case MONO_FIELD_ATTR_FAMILY:
        {
            accessibility = (uint8_t)Accessibility::Protected;
            break;
        }
        case MONO_FIELD_ATTR_FAM_OR_ASSEM:
        {
            accessibility |= (uint8_t)Accessibility::Private;
            accessibility |= (uint8_t)Accessibility::Protected;
            break;
        }
        case MONO_FIELD_ATTR_PUBLIC:
        {
            accessibility = (uint8_t)Accessibility::Public;
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
        uint8_t accessibility = (uint8_t)Accessibility::None;

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
                accessibility = (uint8_t)Accessibility::Private;
                break;
            }
            case MONO_FIELD_ATTR_FAM_AND_ASSEM:
            {
                accessibility |= (uint8_t)Accessibility::Protected;
                accessibility |= (uint8_t)Accessibility::Internal;
                break;
            }
            case MONO_FIELD_ATTR_ASSEMBLY:
            {
                accessibility = (uint8_t)Accessibility::Internal;
                break;
            }
            case MONO_FIELD_ATTR_FAMILY:
            {
                accessibility = (uint8_t)Accessibility::Protected;
                break;
            }
            case MONO_FIELD_ATTR_FAM_OR_ASSEM:
            {
                accessibility |= (uint8_t)Accessibility::Private;
                accessibility |= (uint8_t)Accessibility::Protected;
                break;
            }
            case MONO_FIELD_ATTR_PUBLIC:
            {
                accessibility = (uint8_t)Accessibility::Public;
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
                accessibility = (uint8_t)Accessibility::Private;
        }
        else
        {
            accessibility = (uint8_t)Accessibility::Private;
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

}