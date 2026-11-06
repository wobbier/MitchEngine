#pragma once
#include "Dementia.h"

#if USING( ME_RMLUI )

#include "CLog.h"
#include "JSON.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataVariable.h>
#include <RmlUi/Core/TypeConverter.h>
#include <RmlUi/Core/Variant.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Script-owned RmlUi view models (UI-3): what C#'s UI.Model and the loading screen fill. Values
// live here, not in C#, so they survive hot reloads; Sync binds anything new into the context's
// data models (a document that predates a model must reload to see it) and dirties what changed.
//
// Events: data-event-click="name" queues "name"; data-event-click="pick(2, 'fast')" queues "pick"
// with its arguments, evaluated by RmlUi, which PollEvent's caller reads as a JSON array (ME-312).
//
// Lists (ME-312): a key can instead hold a list of records for data-for, e.g. a scoreboard:
//   <div data-for="row : players" data-class-local="row.local">{{ it_index + 1 }} {{ row.name }}</div>
// Each item's fields are numbers or strings like the model's values; a field an item hasn't set
// reads as "". "players.size" is the count. Any change re-evaluates the whole list's views.
// Header-only so the doctests drive it against a bare Rml::Context.
class RmlScriptModels
{
public:
    // A list's most items. A scoreboard or inventory is a few hundred at most; a bigger count or index
    // is a script bug (a NetId or garbage int as a row), refused with a warning rather than allocating
    // that many items and data-for rows.
    static constexpr int kMaxListItems = 4096;

    void Ensure( const std::string& InModel )
    {
        m_models[InModel];
    }

    // Scripts set every value every frame (a HUD's FillHud); only a changed one is dirtied, so RmlUi
    // re-evaluates (and lays out) only the bindings whose values changed (ME-318).
    void SetNumber( const std::string& InModel, const std::string& InKey, double InValue )
    {
        Value& value = m_models[InModel].Values[InKey];
        if( value.IsString || value.Number != InValue )
        {
            value.Number = InValue;
            value.IsString = false;
            value.Dirty = true;
        }
    }

    void SetString( const std::string& InModel, const std::string& InKey, const std::string& InValue )
    {
        Value& value = m_models[InModel].Values[InKey];
        if( !value.IsString || value.String != InValue )
        {
            value.String = InValue;
            value.IsString = true;
            value.Dirty = true;
        }
    }

    // A list's item count: new items have no fields, removed ones are dropped from the end.
    void SetListCount( const std::string& InModel, const std::string& InKey, int InCount )
    {
        if( InCount > kMaxListItems )
        {
            BRUH_NEW( "[UI] {}.{}: list count {} is over the {} item limit; ignored", InModel, InKey, InCount, kMaxListItems );
            return;
        }
        List& list = m_models[InModel].Lists[InKey];
        const size_t count = static_cast<size_t>( std::max( InCount, 0 ) );
        if( list.Items.size() != count )
        {
            list.Items.resize( count );
            list.Dirty = true;
        }
    }

    int GetListCount( const std::string& InModel, const std::string& InKey ) const
    {
        const auto model = m_models.find( InModel );
        if( model == m_models.end() )
        {
            return 0;
        }
        const auto list = model->second.Lists.find( InKey );
        return list != model->second.Lists.end() ? static_cast<int>( list->second.Items.size() ) : 0;
    }

    // An item's field; an index past the end grows the list to it. Only a change dirties the list.
    void SetItemNumber( const std::string& InModel, const std::string& InKey, int InIndex, const std::string& InField, double InValue )
    {
        if( Value* value = ItemField( InModel, InKey, InIndex, InField ) )
        {
            if( value->IsString || value->Number != InValue || !value->Bound )
            {
                value->Number = InValue;
                value->IsString = false;
                value->Bound = true;   // set at least once
                m_models[InModel].Lists[InKey].Dirty = true;
            }
        }
    }

    void SetItemString( const std::string& InModel, const std::string& InKey, int InIndex, const std::string& InField, const std::string& InValue )
    {
        if( Value* value = ItemField( InModel, InKey, InIndex, InField ) )
        {
            if( !value->IsString || value->String != InValue || !value->Bound )
            {
                value->String = InValue;
                value->IsString = true;
                value->Bound = true;
                m_models[InModel].Lists[InKey].Dirty = true;
            }
        }
    }

    void RegisterEvent( const std::string& InModel, const std::string& InKey )
    {
        m_models[InModel].Events.emplace( InKey, false );
    }

    double GetNumber( const std::string& InModel, const std::string& InKey ) const
    {
        const Value* value = Find( InModel, InKey );
        return value && !value->IsString ? value->Number : 0.0;
    }

    std::string GetString( const std::string& InModel, const std::string& InKey ) const
    {
        const Value* value = Find( InModel, InKey );
        if( !value )
        {
            return {};
        }
        return value->IsString ? value->String : std::to_string( value->Number );
    }

    // The model's values as text, by key (the UI designer's Bindings panel).
    std::vector<std::pair<std::string, std::string>> ListValues( const std::string& InModel ) const
    {
        std::vector<std::pair<std::string, std::string>> values;
        const auto model = m_models.find( InModel );
        if( model == m_models.end() )
        {
            return values;
        }
        for( const auto& [key, value] : model->second.Values )
        {
            values.emplace_back( key, value.IsString ? "\"" + value.String + "\"" : Rml::ToString( value.Number ) );
        }
        for( const auto& [key, list] : model->second.Lists )
        {
            values.emplace_back( key, "[" + std::to_string( list.Items.size() ) + " items]" );
        }
        return values;
    }

    // Pops the oldest queued event name ("" when none are waiting); its arguments are then
    // LastEventArgs.
    std::string PollEvent( const std::string& InModel )
    {
        auto model = m_models.find( InModel );
        if( model == m_models.end() )
        {
            return {};
        }
        if( model->second.EventQueue.empty() )
        {
            model->second.LastArgs = "[]";
            return {};
        }
        QueuedEvent event = std::move( model->second.EventQueue.front() );
        model->second.EventQueue.pop_front();
        model->second.LastArgs = std::move( event.Args );
        return event.Name;
    }

    // The arguments of the event PollEvent last returned for this model, as a JSON array:
    // numbers, strings and bools as themselves, anything else as its text ("[]" without any).
    std::string LastEventArgs( const std::string& InModel ) const
    {
        const auto model = m_models.find( InModel );
        return model != m_models.end() ? model->second.LastArgs : std::string( "[]" );
    }

    // Binds new values and events into the context's data models and dirties changed values. True
    // when anything new was bound: documents that were loaded before must reload to see it.
    bool Sync( Rml::Context& InContext )
    {
        bool materialized = false;
        for( auto& [name, model] : m_models )
        {
            Rml::DataModelConstructor constructor;
            if( !model.Created )
            {
                constructor = InContext.CreateDataModel( name );
                if( !constructor )
                {
                    constructor = InContext.GetDataModel( name );
                }
                if( !constructor )
                {
                    continue;
                }
                model.Created = true;
                materialized = true;
            }
            const auto constructorFor = [&]() -> Rml::DataModelConstructor& {
                if( !constructor )
                {
                    constructor = InContext.GetDataModel( name );
                }
                return constructor;
            };
            Rml::DataModelHandle handle;
            for( auto& [key, value] : model.Values )
            {
                if( !value.Bound )
                {
                    Value* stored = &value;
                    constructorFor().BindFunc(
                        key,
                        [stored]( Rml::Variant& OutValue ) { stored->Read( OutValue ); },
                        // Two-way bindings (data-value on inputs) write the view's value back.
                        [stored]( const Rml::Variant& InValue ) { stored->Write( InValue ); } );
                    value.Bound = true;
                    value.Dirty = false;
                    materialized = true;
                    continue;
                }
                if( value.Dirty )
                {
                    if( !handle )
                    {
                        handle = InContext.GetDataModel( name ).GetModelHandle();
                    }
                    handle.DirtyVariable( key );
                    value.Dirty = false;
                }
            }
            for( auto& [key, list] : model.Lists )
            {
                if( !list.Bound )
                {
                    constructorFor().BindCustomDataVariable( key, Rml::DataVariable( &ListDefinition::Get(), &list ) );
                    list.Bound = true;
                    list.Dirty = false;
                    materialized = true;
                    continue;
                }
                if( list.Dirty )
                {
                    if( !handle )
                    {
                        handle = InContext.GetDataModel( name ).GetModelHandle();
                    }
                    handle.DirtyVariable( key );
                    list.Dirty = false;
                }
            }
            for( auto& [key, bound] : model.Events )
            {
                if( bound )
                {
                    continue;
                }
                Model* owner = &model;
                const std::string eventName = key;
                constructorFor().BindEventCallback( key, [owner, eventName]( Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& InArguments ) {
                    owner->EventQueue.push_back( { eventName, ArgumentsJson( InArguments ) } );
                } );
                bound = true;
                materialized = true;
            }
        }
        return materialized;
    }

    // RmlUi's evaluated data-event arguments as a JSON array.
    static std::string ArgumentsJson( const Rml::VariantList& InArguments )
    {
        json arguments = json::array();
        for( const Rml::Variant& argument : InArguments )
        {
            switch( argument.GetType() )
            {
            case Rml::Variant::BOOL:
                arguments.push_back( argument.Get<bool>() );
                break;
            case Rml::Variant::BYTE:
            case Rml::Variant::INT:
            case Rml::Variant::INT64:
                arguments.push_back( argument.Get<int64_t>() );
                break;
            case Rml::Variant::UINT:
            case Rml::Variant::UINT64:
                arguments.push_back( argument.Get<uint64_t>() );
                break;
            case Rml::Variant::FLOAT:
            case Rml::Variant::DOUBLE:
            {
                // RmlUi evaluates every literal and it_index as a double; a whole one goes out as an
                // integer so C# reads pick(2) as "2", not "2.0".
                const double number = argument.Get<double>();
                if( std::trunc( number ) == number && std::abs( number ) < 9.0e15 )
                {
                    arguments.push_back( static_cast<int64_t>( number ) );
                }
                else
                {
                    arguments.push_back( number );
                }
                break;
            }
            default:
                arguments.push_back( argument.Get<Rml::String>() );
                break;
            }
        }
        return arguments.dump();
    }

private:
    // One script view-model value: a number or a UTF-8 string.
    struct Value
    {
        double Number = 0.0;
        std::string String;
        bool IsString = false;
        bool Bound = false;
        bool Dirty = false;

        void Read( Rml::Variant& OutValue ) const
        {
            if( IsString )
            {
                OutValue = String;
            }
            else
            {
                OutValue = Number;
            }
        }

        void Write( const Rml::Variant& InValue )
        {
            if( IsString )
            {
                String = InValue.Get<Rml::String>();
            }
            else
            {
                Number = InValue.Get<double>();
            }
        }
    };
    // A list item: its fields by name. Field values reuse Value (Bound: set at least once).
    struct Item
    {
        std::map<std::string, Value> Fields;
    };
    struct List
    {
        std::vector<Item> Items;
        bool Bound = false;
        bool Dirty = false;
    };

    // RmlUi variable definitions over a List: an array of structs of scalars. RmlUi resolves an
    // address (players[2].name) through them on every evaluation and keeps no item pointers, so the
    // items may be resized between updates.
    class FieldDefinition final : public Rml::VariableDefinition
    {
    public:
        FieldDefinition() : VariableDefinition( Rml::DataVariableType::Scalar ) {}
        static FieldDefinition& Get() { static FieldDefinition definition; return definition; }

        bool Get( void* InPtr, Rml::Variant& OutVariant ) override
        {
            if( InPtr )
            {
                static_cast<const Value*>( InPtr )->Read( OutVariant );
            }
            else
            {
                OutVariant = Rml::String();   // a field this item hasn't set
            }
            return true;
        }

        bool Set( void* InPtr, const Rml::Variant& InVariant ) override
        {
            if( InPtr )
            {
                static_cast<Value*>( InPtr )->Write( InVariant );
            }
            return InPtr != nullptr;
        }
    };

    class ItemDefinition final : public Rml::VariableDefinition
    {
    public:
        ItemDefinition() : VariableDefinition( Rml::DataVariableType::Struct ) {}
        static ItemDefinition& Get() { static ItemDefinition definition; return definition; }

        Rml::DataVariable Child( void* InPtr, const Rml::DataAddressEntry& InAddress ) override
        {
            Item& item = *static_cast<Item*>( InPtr );
            const auto field = item.Fields.find( InAddress.name );
            return Rml::DataVariable( &FieldDefinition::Get(), field != item.Fields.end() ? &field->second : nullptr );
        }

        Rml::StringList ReflectMemberNames() override
        {
            return {};
        }
    };

    class ListDefinition final : public Rml::VariableDefinition
    {
    public:
        ListDefinition() : VariableDefinition( Rml::DataVariableType::Array ) {}
        static ListDefinition& Get() { static ListDefinition definition; return definition; }

        int Size( void* InPtr ) override
        {
            return static_cast<int>( static_cast<List*>( InPtr )->Items.size() );
        }

        Rml::DataVariable Child( void* InPtr, const Rml::DataAddressEntry& InAddress ) override
        {
            List& list = *static_cast<List*>( InPtr );
            const int count = static_cast<int>( list.Items.size() );
            if( InAddress.index >= 0 && InAddress.index < count )
            {
                return Rml::DataVariable( &ItemDefinition::Get(), &list.Items[InAddress.index] );
            }
            if( InAddress.name == "size" )
            {
                return Rml::MakeLiteralIntVariable( count );
            }
            return Rml::DataVariable();
        }
    };

    Value* ItemField( const std::string& InModel, const std::string& InKey, int InIndex, const std::string& InField )
    {
        if( InIndex < 0 )
        {
            return nullptr;
        }
        if( InIndex >= kMaxListItems )
        {
            BRUH_NEW( "[UI] {}.{}[{}].{}: index is over the {} item limit; ignored", InModel, InKey, InIndex, InField, kMaxListItems );
            return nullptr;
        }
        List& list = m_models[InModel].Lists[InKey];
        if( static_cast<size_t>( InIndex ) >= list.Items.size() )
        {
            list.Items.resize( static_cast<size_t>( InIndex ) + 1 );
            list.Dirty = true;
        }
        return &list.Items[InIndex].Fields[InField];
    }

    struct QueuedEvent
    {
        std::string Name;
        std::string Args;   // JSON array
    };
    struct Model
    {
        std::map<std::string, Value> Values;
        std::map<std::string, List> Lists;
        std::map<std::string, bool> Events;   // name -> bound to Rml yet
        std::deque<QueuedEvent> EventQueue;
        std::string LastArgs = "[]";
        bool Created = false;
    };

    const Value* Find( const std::string& InModel, const std::string& InKey ) const
    {
        const auto model = m_models.find( InModel );
        if( model == m_models.end() )
        {
            return nullptr;
        }
        const auto value = model->second.Values.find( InKey );
        return value != model->second.Values.end() ? &value->second : nullptr;
    }

    std::map<std::string, Model> m_models;
};

#endif
