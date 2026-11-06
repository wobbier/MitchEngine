#pragma once
#include "Dementia.h"

#if USING( ME_RMLUI )

#include "JSON.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/DataVariable.h>
#include <RmlUi/Core/TypeConverter.h>
#include <RmlUi/Core/Variant.h>
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
// Header-only so the doctests drive it against a bare Rml::Context.
class RmlScriptModels
{
public:
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
            case Rml::Variant::FLOAT:
            case Rml::Variant::DOUBLE:
            case Rml::Variant::INT:
            case Rml::Variant::INT64:
            case Rml::Variant::UINT:
            case Rml::Variant::UINT64:
                arguments.push_back( argument.Get<double>() );
                break;
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
    struct QueuedEvent
    {
        std::string Name;
        std::string Args;   // JSON array
    };
    struct Model
    {
        std::map<std::string, Value> Values;
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
