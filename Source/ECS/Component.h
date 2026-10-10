// 2018 Mitchell Andrews
#pragma once
#include "ClassTypeId.h"
#include "EntityID.h"
#include "Dementia.h"

#include "EntityHandle.h"
#include "JSON.h"
#include "Reflection/Reflection.h"

#define ME_REGISTER_COMPONENT_FOLDER(TYPE, FOLDER)            \
	namespace details {                                       \
    namespace                                                 \
    {                                                         \
        template<class T>                                     \
        class ComponentRegistration;                          \
                                                              \
        template<>                                            \
        class ComponentRegistration<TYPE>                     \
        {                                                     \
            static const RegistryEntry<TYPE>& reg;            \
        };                                                    \
                                                              \
        const RegistryEntry<TYPE>&                            \
            ComponentRegistration<TYPE>::reg =                \
                RegistryEntry<TYPE>::Instance(#TYPE, FOLDER); \
    }}
#define ME_REGISTER_COMPONENT(TYPE) ME_REGISTER_COMPONENT_FOLDER(TYPE, "")

class World;

// Lifecycle (all on the main thread):
//   constructor -> [Deserialize] -> Init()            when added / loaded
//   OnEnable() / OnDisable()                          when the component or its entity's
//                                                     active-in-hierarchy state flips (at sync points)
//   OnDestroy()                                       right before the component is destroyed
class BaseComponent
{
    friend class World;
public:
    BaseComponent() = delete;
    BaseComponent( const char* CompName )
        : TypeName( CompName )
    {
        TypeName = TypeName.substr( TypeName.find( ' ' ) + 1 );
    }

    virtual ~BaseComponent() = default;

    // Called after Deserialize (or right after being added at runtime).
    virtual void Init() = 0;

    virtual void OnEnable() {}
    virtual void OnDisable() {}
    virtual void OnDestroy() {}

    // Called after a reflected field was changed through the reflection API (inspector, undo,
    // scripts, prefab overrides) so the component can react, e.g. Transform re-dirtying.
    virtual void OnPropertyChanged( const std::string& InFieldName ) {}

    const std::string& GetName() const
    {
        return TypeName;
    }

    // The entity that owns this component.
    EntityHandle Parent;

    // A disabled component is ignored by cores (it doesn't count toward their filters).
    bool IsEnabled() const { return m_isEnabled; }
    void SetEnabled( bool InEnabled );

    virtual void Serialize( json& outJson ) = 0;
    virtual void Deserialize( const json& inJson ) = 0;

    virtual TypeId GetTypeId() const = 0;

    // Reflection data for this component's concrete type, or null if it isn't reflected.
    virtual const Reflection::TypeInfo* GetTypeInfo() const { return nullptr; }
    // The object GetTypeInfo describes (the concrete component, which may sit at another address
    // than this base under multiple inheritance).
    virtual void* GetReflectedObject() { return nullptr; }

#if USING( ME_EDITOR )
    virtual void OnEditorInspect() = 0;
#endif

private:
    std::string TypeName;
    bool m_isEnabled = true;
};

template<typename T>
class Component
    : public BaseComponent
{
public:
    Component( const char* Name )
        : BaseComponent( Name )
    {
    }

    virtual TypeId GetTypeId() const final
    {
        return ClassTypeId<BaseComponent>::GetTypeId<T>();
    }

    static TypeId GetStaticTypeId()
    {
        return ClassTypeId<BaseComponent>::GetTypeId<T>();
    }

    // OnDeserialize guaranteed to be called before this
    virtual void Init() override {};

    virtual void* GetReflectedObject() override
    {
        return static_cast<T*>( this );
    }

    virtual const Reflection::TypeInfo* GetTypeInfo() const override
    {
        if constexpr( Reflection::HasStaticType<T> )
        {
            return &T::StaticType();
        }
        else
        {
            return nullptr;
        }
    }

    virtual void Serialize( json& outJson ) final
    {
        outJson["Type"] = GetName();
        if( !IsEnabled() )
        {
            outJson["Enabled"] = false;
        }
        OnSerialize( outJson );
    }

    virtual void Deserialize( const json& inJson ) final
    {
        auto enabled = inJson.find( "Enabled" );
        if( enabled != inJson.end() && enabled->is_boolean() )
        {
            SetEnabled( enabled->get<bool>() );
        }
        OnDeserialize( inJson );
    }

#if USING( ME_EDITOR )

    virtual void OnEditorInspect() override
    {
    }

#endif
private:
    // Default (de)serialization goes through reflection when the component is reflected.
    virtual void OnSerialize( json& outJson )
    {
        if constexpr( Reflection::HasStaticType<T> )
        {
            Reflection::ToJson( T::StaticType(), static_cast<T*>( this ), outJson );
        }
    }

    virtual void OnDeserialize( const json& inJson )
    {
        if constexpr( Reflection::HasStaticType<T> )
        {
            Reflection::FromJson( T::StaticType(), static_cast<T*>( this ), inJson );
        }
    }
};

using ComponentArray = std::vector<std::reference_wrapper<BaseComponent>>;
