#include "PCH.h"
#include "Log.bindings.h"

#include <mono/metadata/object.h>

static void Log( MonoString* inString )
{
    char* str = mono_string_to_utf8( inString );

    BRUH( str );

    mono_free( str );
}

static void LogError( MonoString* inString )
{
    char* str = mono_string_to_utf8( inString );

    YIKES( str );

    mono_free( str );
}

static void NativeLog( MonoString* inString, int number )
{
    char* str = mono_string_to_utf8( inString );

    std::cout << str << std::to_string( number ) << std::endl;

    mono_free( str );
}

static void NativeLog_Vector( Vector3* inVector, Vector3* outVec )
{
    Vector3 vec = *inVector;
    std::cout << "CPP: " << std::to_string( vec.x ) << ", " << std::to_string( vec.y ) << ", " << std::to_string( vec.z ) << std::endl;
    inVector->Normalize();
    *outVec = *inVector;
}

static float Native_VectorLength( Vector3* inVector )
{
    return inVector->Length();
}


void Register_LogBindings()
{
    mono_add_internal_call( "Debug::Log", (void*)Log );
    mono_add_internal_call( "Debug::Error", (void*)LogError );
    mono_add_internal_call( "TestScript::NativeLog", (void*)NativeLog );
    mono_add_internal_call( "TestScript::NativeLog_Vector", (void*)NativeLog_Vector );
    mono_add_internal_call( "TestScript::Native_VectorLength", (void*)Native_VectorLength );
}
