#pragma once
#include <string>
#include "Math/Vector3.h"
#include "Math/Vector2.h"

class HavanaUtils
{
    HavanaUtils() = delete;

public:
    static float Label( const std::string& Name, float customWidth = -1 );

    static void Text( const std::string& Name, const Vector3& Vector );
    static void Text( const std::string& Name, const Vector2& Vector );

    static bool EditableVector3( const std::string& Name, Vector3& Vector, float ResetValue = 0.f, float customWidth = -1 );
    static bool EditableVector3Spring( const std::string& Name, Vector3& Vector, float ResetValue = 0.f, float customWidth = -1 );
    static bool EditableVector( const std::string& Name, Vector2& Vector, float ResetValue = 0.f, float customWidth = -1 );

    static bool Float( const std::string& Name, float& value );
    static bool Double( const std::string& Name, double& value );

    static bool Int( const std::string& Name, int16_t& value );
    static bool Int( const std::string& Name, int32_t& value );
    static bool Int( const std::string& Name, int64_t& value );

    static bool UInt( const std::string& Name, uint16_t& value );
    static bool UInt( const std::string& Name, uint32_t& value );
    static bool UInt( const std::string& Name, uint64_t& value );

    static void ColorButton( const std::string& Name, Vector3& arr );
};