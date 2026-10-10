#pragma once
// Runtime reflection for engine/game types.
//
// A reflected type exposes its fields (name, type, metadata, accessor) through a TypeInfo, which
// drives generic JSON serialization, the editor inspector, undo/redo of property edits, prefab
// overrides, copy/paste and script access without per-type boilerplate.
//
// Usage:
//   // Header
//   class Light : public Component<Light>
//   {
//       ME_REFLECTABLE( Light )
//       float Intensity = 1.f;
//       Vector3 Color;
//   };
//
//   // Source
//   ME_REFLECT_BEGIN( Light )
//       ME_FIELD( Intensity ).Range( 0.f, 100.f ).Tooltip( "Luminous intensity" );
//       ME_FIELD( Color ).Color();
//   ME_REFLECT_END()
//
// Enums are reflected by value list and serialize as their names:
//   ME_REFLECT_ENUM( LightType, { { "Directional", LightType::Directional }, { "Point", LightType::Point } } )
//
// Types the reflection system doesn't know natively (handles, asset references...) specialize
// Reflection::CustomTypeTraits<T> next to the type's declaration.

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include "JSON.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Math/Quaternion.h"

namespace Reflection
{
    enum class PropertyType : uint8_t
    {
        Unknown = 0,
        Bool,
        Int,
        UInt,
        Float,
        Double,
        String,
        Vector2,
        Vector3,
        Vector4,
        Quaternion,
        Enum,
        Struct,
        Array,
        Custom
    };

    const char* ToString( PropertyType type );

    namespace FieldFlags
    {
        enum : uint32_t
        {
            None = 0,
            Hidden = 1 << 0,        // Not drawn by the inspector (still serialized).
            ReadOnly = 1 << 1,      // Drawn disabled.
            NoSerialize = 1 << 2,   // Runtime-only state; never written to or read from JSON.
            Color = 1 << 3,         // Vector3/Vector4 edited with a colour picker.
            HDR = 1 << 4,           // Colour allows values above 1.
            Angle = 1 << 5,         // Float stored in radians, edited in degrees.
            Multiline = 1 << 6,     // String edited in a multi-line box.
            Asset = 1 << 7,         // String holds an asset path; AssetFilter names the asset kind.
        };
    }

    struct TypeInfo;
    struct EnumInfo;

    // Describes how to handle one C++ value type (a field's type or an array's element type).
    struct ValueOps
    {
        PropertyType Type = PropertyType::Unknown;
        size_t Size = 0;
        const char* CppName = "";

        // Int / UInt
        uint8_t IntBytes = 0;

        // Enum
        std::type_index EnumType = std::type_index( typeid( void ) );
        int64_t ( *GetEnum )( const void* value ) = nullptr;
        void ( *SetEnum )( void* value, int64_t enumValue ) = nullptr;

        // Struct
        const TypeInfo& ( *GetStructType )() = nullptr;

        // Array (std::vector)
        const ValueOps* Element = nullptr;
        size_t ( *ArraySize )( const void* value ) = nullptr;
        void ( *ArrayResize )( void* value, size_t size ) = nullptr;
        void* ( *ArrayAt )( void* value, size_t index ) = nullptr;

        // Custom
        void ( *CustomToJson )( const void* value, json& out ) = nullptr;
        bool ( *CustomFromJson )( void* value, const json& in ) = nullptr;

        const EnumInfo* GetEnumInfo() const;
    };

    struct FieldInfo
    {
        std::string Name;           // Serialized key and path segment.
        std::string DisplayName;    // Inspector label.
        std::string Tooltip;
        std::string Category;
        std::string AssetFilter;
        uint32_t Flags = FieldFlags::None;
        bool HasRange = false;
        float Min = 0.f;
        float Max = 0.f;
        float Speed = 0.1f;
        // Integer fields picked from named choices (e.g. navigation areas): a combo, or a bit mask
        // of the choices when ChoiceMask is set.
        std::string ( *ChoiceName )( int index ) = nullptr;
        int ChoiceCount = 0;
        bool ChoiceMask = false;
        // Keys this field was saved under before it was renamed (read when Name is missing).
        std::vector<std::string> FormerNames;

        const ValueOps* Ops = nullptr;
        void* ( *Access )( void* instance ) = nullptr;

        bool Has( uint32_t flag ) const { return ( Flags & flag ) != 0; }
        PropertyType GetType() const { return Ops ? Ops->Type : PropertyType::Unknown; }

        void* Get( void* instance ) const { return Access( instance ); }
        const void* Get( const void* instance ) const { return Access( const_cast<void*>( instance ) ); }
    };

    struct TypeInfo
    {
        std::string Name;
        size_t Size = 0;
        std::vector<FieldInfo> Fields;

        const TypeInfo& ( *GetBase )() = nullptr;
        void* ( *CastToBase )( void* instance ) = nullptr;

        const TypeInfo* GetBaseType() const { return GetBase ? &GetBase() : nullptr; }

        // Finds a field on this type or any base type. outOwner receives the instance pointer the
        // field's accessor expects (adjusted to the base subobject when the field is inherited).
        const FieldInfo* FindField( std::string_view name ) const;
        const FieldInfo* FindField( std::string_view name, void* instance, void** outOwner ) const;

        // Visits base-type fields first, then this type's own. fn( const FieldInfo&, void* owner ).
        template<typename Fn>
        void ForEachField( void* instance, Fn&& fn ) const
        {
            if( GetBase )
            {
                GetBase().ForEachField( CastToBase( instance ), fn );
            }
            for( const FieldInfo& field : Fields )
            {
                fn( field, instance );
            }
        }

        template<typename Fn>
        void ForEachField( const void* instance, Fn&& fn ) const
        {
            ForEachField( const_cast<void*>( instance ), [&fn]( const FieldInfo& field, void* owner ) { fn( field, static_cast<const void*>( owner ) ); } );
        }
    };

    struct EnumInfo
    {
        std::string Name;
        std::vector<std::pair<std::string, int64_t>> Values;

        const char* ToName( int64_t value ) const;
        bool FromName( std::string_view name, int64_t& outValue ) const;
    };

    // ---------------------------------------------------------------------------------------
    // Registry
    // ---------------------------------------------------------------------------------------
    class Registry
    {
    public:
        static const TypeInfo* FindType( std::string_view name );
        static const std::vector<const TypeInfo*>& GetAllTypes();

        static void RegisterType( TypeInfo* type );
        static void RegisterEnum( std::type_index type, EnumInfo* info );
        static const EnumInfo* FindEnum( std::type_index type );
    };

    // ---------------------------------------------------------------------------------------
    // Type traits: C++ type -> ValueOps
    // ---------------------------------------------------------------------------------------

    // Specialize for engine-specific value types:
    //   template<> struct CustomTypeTraits<EntityHandle> {
    //       static constexpr bool IsCustom = true;
    //       static constexpr const char* Name = "EntityHandle";
    //       static void ToJson( const EntityHandle&, json& );
    //       static bool FromJson( EntityHandle&, const json& );
    //   };
    template<typename T>
    struct CustomTypeTraits
    {
        static constexpr bool IsCustom = false;
    };

    template<typename T>
    concept HasStaticType = requires { { T::StaticType() } -> std::convertible_to<const TypeInfo&>; };

    template<typename T>
    struct IsStdVector : std::false_type {};

    template<typename T, typename A>
    struct IsStdVector<std::vector<T, A>> : std::true_type {};

    template<typename T>
    const ValueOps& GetValueOps();

    namespace Detail
    {
        template<typename T>
        ValueOps MakeValueOps()
        {
            ValueOps ops;
            ops.Size = sizeof( T );

            if constexpr( CustomTypeTraits<T>::IsCustom )
            {
                ops.Type = PropertyType::Custom;
                ops.CppName = CustomTypeTraits<T>::Name;
                ops.CustomToJson = []( const void* value, json& out ) { CustomTypeTraits<T>::ToJson( *static_cast<const T*>( value ), out ); };
                ops.CustomFromJson = []( void* value, const json& in ) { return CustomTypeTraits<T>::FromJson( *static_cast<T*>( value ), in ); };
            }
            else if constexpr( std::is_same_v<T, bool> )
            {
                ops.Type = PropertyType::Bool;
                ops.CppName = "bool";
            }
            else if constexpr( std::is_enum_v<T> )
            {
                ops.Type = PropertyType::Enum;
                ops.CppName = "enum";
                ops.EnumType = std::type_index( typeid( T ) );
                ops.GetEnum = []( const void* value ) { return static_cast<int64_t>( *static_cast<const T*>( value ) ); };
                ops.SetEnum = []( void* value, int64_t enumValue ) { *static_cast<T*>( value ) = static_cast<T>( enumValue ); };
            }
            else if constexpr( std::is_integral_v<T> )
            {
                ops.Type = std::is_signed_v<T> ? PropertyType::Int : PropertyType::UInt;
                ops.CppName = std::is_signed_v<T> ? "int" : "uint";
                ops.IntBytes = static_cast<uint8_t>( sizeof( T ) );
            }
            else if constexpr( std::is_same_v<T, float> )
            {
                ops.Type = PropertyType::Float;
                ops.CppName = "float";
            }
            else if constexpr( std::is_same_v<T, double> )
            {
                ops.Type = PropertyType::Double;
                ops.CppName = "double";
            }
            else if constexpr( std::is_same_v<T, std::string> )
            {
                ops.Type = PropertyType::String;
                ops.CppName = "string";
            }
            else if constexpr( std::is_same_v<T, ::Vector2> )
            {
                ops.Type = PropertyType::Vector2;
                ops.CppName = "Vector2";
            }
            else if constexpr( std::is_same_v<T, ::Vector3> )
            {
                ops.Type = PropertyType::Vector3;
                ops.CppName = "Vector3";
            }
            else if constexpr( std::is_same_v<T, ::Vector4> )
            {
                ops.Type = PropertyType::Vector4;
                ops.CppName = "Vector4";
            }
            else if constexpr( std::is_same_v<T, ::Quaternion> )
            {
                ops.Type = PropertyType::Quaternion;
                ops.CppName = "Quaternion";
            }
            else if constexpr( IsStdVector<T>::value )
            {
                using Element = typename T::value_type;
                ops.Type = PropertyType::Array;
                ops.CppName = "vector";
                ops.Element = &GetValueOps<Element>();
                ops.ArraySize = []( const void* value ) { return static_cast<const T*>( value )->size(); };
                ops.ArrayResize = []( void* value, size_t size ) { static_cast<T*>( value )->resize( size ); };
                ops.ArrayAt = []( void* value, size_t index ) -> void* { return &( *static_cast<T*>( value ) )[index]; };
            }
            else if constexpr( HasStaticType<T> )
            {
                ops.Type = PropertyType::Struct;
                ops.CppName = "struct";
                ops.GetStructType = []() -> const TypeInfo& { return T::StaticType(); };
            }
            else
            {
                static_assert( sizeof( T ) == 0, "Type is not reflectable: give it ME_REFLECTABLE or specialize Reflection::CustomTypeTraits." );
            }
            return ops;
        }

        std::string PrettifyName( std::string_view name );
    }

    template<typename T>
    const ValueOps& GetValueOps()
    {
        static const ValueOps ops = Detail::MakeValueOps<T>();
        return ops;
    }

    template<typename T>
    struct MemberPointerTraits;

    template<typename Class, typename Member>
    struct MemberPointerTraits<Member Class::*>
    {
        using ClassType = Class;
        using MemberType = Member;
    };

    // ---------------------------------------------------------------------------------------
    // Builders
    // ---------------------------------------------------------------------------------------
    class FieldBuilder
    {
    public:
        FieldBuilder( TypeInfo& type, size_t index ) : m_type( type ), m_index( index ) {}

        FieldBuilder& Display( const char* name ) { Field().DisplayName = name; return *this; }
        FieldBuilder& Tooltip( const char* text ) { Field().Tooltip = text; return *this; }
        FieldBuilder& Category( const char* category ) { Field().Category = category; return *this; }
        FieldBuilder& Range( float min, float max ) { Field().HasRange = true; Field().Min = min; Field().Max = max; return *this; }
        FieldBuilder& Speed( float speed ) { Field().Speed = speed; return *this; }
        FieldBuilder& Hidden() { Field().Flags |= FieldFlags::Hidden; return *this; }
        FieldBuilder& ReadOnly() { Field().Flags |= FieldFlags::ReadOnly; return *this; }
        FieldBuilder& NoSerialize() { Field().Flags |= FieldFlags::NoSerialize; return *this; }
        FieldBuilder& Color() { Field().Flags |= FieldFlags::Color; return *this; }
        FieldBuilder& HDR() { Field().Flags |= FieldFlags::Color | FieldFlags::HDR; return *this; }
        FieldBuilder& Angle() { Field().Flags |= FieldFlags::Angle; return *this; }
        FieldBuilder& Multiline() { Field().Flags |= FieldFlags::Multiline; return *this; }
        FieldBuilder& Asset( const char* filter = "" ) { Field().Flags |= FieldFlags::Asset; Field().AssetFilter = filter; return *this; }
        FieldBuilder& Choices( int count, std::string ( *name )( int ) ) { Field().ChoiceCount = count; Field().ChoiceName = name; Field().ChoiceMask = false; return *this; }
        FieldBuilder& MaskChoices( int count, std::string ( *name )( int ) ) { Field().ChoiceCount = count; Field().ChoiceName = name; Field().ChoiceMask = true; return *this; }
        // The field was renamed: data saved under the old key still loads (saving writes the new one).
        FieldBuilder& FormerName( const char* name ) { Field().FormerNames.emplace_back( name ); return *this; }

    private:
        FieldInfo& Field() { return m_type.Fields[m_index]; }

        TypeInfo& m_type;
        size_t m_index;
    };

    template<typename T>
    class TypeBuilder
    {
    public:
        explicit TypeBuilder( TypeInfo& type ) : m_type( type ) {}

        template<auto MemberPtr>
        FieldBuilder Field( const char* name )
        {
            using MemberType = typename MemberPointerTraits<decltype( MemberPtr )>::MemberType;

            FieldInfo field;
            field.Name = name;
            field.DisplayName = Detail::PrettifyName( name );
            field.Ops = &GetValueOps<MemberType>();
            field.Access = []( void* instance ) -> void* { return &( static_cast<T*>( instance )->*MemberPtr ); };
            m_type.Fields.push_back( std::move( field ) );
            return FieldBuilder( m_type, m_type.Fields.size() - 1 );
        }

        template<typename BaseType>
        void Base()
        {
            static_assert( std::is_base_of_v<BaseType, T>, "Base() must name a base class." );
            m_type.GetBase = []() -> const TypeInfo& { return BaseType::StaticType(); };
            m_type.CastToBase = []( void* instance ) -> void* { return static_cast<BaseType*>( static_cast<T*>( instance ) ); };
        }

    private:
        TypeInfo& m_type;
    };

    template<typename T, typename BuildFn>
    const TypeInfo& BuildType( const char* name, BuildFn&& build )
    {
        TypeInfo* type = new TypeInfo();
        type->Name = name;
        type->Size = sizeof( T );
        TypeBuilder<T> builder( *type );
        build( builder );
        Registry::RegisterType( type );
        return *type;
    }

    template<typename E>
    struct EnumRegistrar
    {
        EnumRegistrar( const char* name, std::initializer_list<std::pair<const char*, E>> values )
        {
            EnumInfo* info = new EnumInfo();
            info->Name = name;
            for( const auto& value : values )
            {
                info->Values.emplace_back( value.first, static_cast<int64_t>( value.second ) );
            }
            Registry::RegisterEnum( std::type_index( typeid( E ) ), info );
        }
    };

    template<typename E>
    const EnumInfo* GetEnumInfo()
    {
        return Registry::FindEnum( std::type_index( typeid( E ) ) );
    }

    // ---------------------------------------------------------------------------------------
    // Generic operations
    // ---------------------------------------------------------------------------------------

    // Writes every serializable field of instance into out (an object).
    void ToJson( const TypeInfo& type, const void* instance, json& out );
    // Reads the fields present in `in`; absent keys keep their current values.
    void FromJson( const TypeInfo& type, void* instance, const json& in );

    void ValueToJson( const ValueOps& ops, const void* value, json& out );
    bool ValueFromJson( const ValueOps& ops, void* value, const json& in );

    // A resolved property path such as "Settings.Exposure" or "Points[2].x".
    struct PropertyRef
    {
        const FieldInfo* Field = nullptr;   // Last named field on the path (null for array elements' sub-scalars).
        const ValueOps* Ops = nullptr;      // Type of the value at the end of the path.
        void* Value = nullptr;              // Pointer to the value at the end of the path.

        explicit operator bool() const { return Ops && Value; }
    };

    PropertyRef ResolvePath( const TypeInfo& type, void* instance, std::string_view path );

    // JSON value at path; null json if the path doesn't resolve.
    json GetPathJson( const TypeInfo& type, const void* instance, std::string_view path );
    bool SetPathJson( const TypeInfo& type, void* instance, std::string_view path, const json& value );

    // Top-level serializable field names whose values differ between a and b (same type).
    std::vector<std::string> DiffFields( const TypeInfo& type, const void* a, const void* b );

    // Copies every serializable field from src to dst (same type).
    void CopyFields( const TypeInfo& type, const void* src, void* dst );
}

// ---------------------------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------------------------

// Inside a class/struct body. Leaves the access specifier as private.
#define ME_REFLECTABLE( Type )                                  \
public:                                                         \
    static const ::Reflection::TypeInfo& StaticType();          \
private:

#define ME_REFLECT_CONCAT_IMPL( a, b ) a##b
#define ME_REFLECT_CONCAT( a, b ) ME_REFLECT_CONCAT_IMPL( a, b )

// In a .cpp file. Fields are declared between BEGIN and END with ME_FIELD / ME_FIELD_NAMED.
#define ME_REFLECT_BEGIN( Type )                                                                    \
    const ::Reflection::TypeInfo& Type::StaticType()                                                \
    {                                                                                               \
        static const ::Reflection::TypeInfo& s_typeInfo = ::Reflection::BuildType<Type>( #Type,    \
            []( ::Reflection::TypeBuilder<Type>& builder )                                          \
            {                                                                                       \
                using ReflectedType = Type;                                                         \
                (void)builder;

#define ME_FIELD( member ) builder.template Field<&ReflectedType::member>( #member )
#define ME_FIELD_NAMED( member, name ) builder.template Field<&ReflectedType::member>( name )
#define ME_REFLECT_BASE( BaseType ) builder.template Base<BaseType>()

#define ME_REFLECT_END()                                                                            \
            } );                                                                                    \
        return s_typeInfo;                                                                          \
    }

// Registers the type with the registry during static initialization (so FindType works by name).
#define ME_REFLECT_REGISTER( Type )                                                                 \
    static const bool ME_REFLECT_CONCAT( s_meReflectRegistered_, __LINE__ ) = ( Type::StaticType(), true );

#define ME_REFLECT_ENUM( EnumType, ... )                                                            \
    static const ::Reflection::EnumRegistrar<EnumType> ME_REFLECT_CONCAT( s_meEnumRegistrar_, __LINE__ )( #EnumType, __VA_ARGS__ );
