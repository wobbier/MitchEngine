#pragma once
#include <imgui.h>
#include <string>
#include <map>

#include "Pointers.h"
#include "Singleton.h"

#define ME_REGISTER_EDITOR_WIDGET(TYPE)            \
	namespace details {                                       \
    namespace                                                 \
    {                                                         \
        template<class T>                                     \
        class EditorWidgetRegistration;                          \
                                                              \
        template<>                                            \
        class EditorWidgetRegistration<TYPE>                     \
        {                                                     \
            static const WidgetRegistryEntry<TYPE>& reg;            \
        };                                                    \
                                                              \
        const WidgetRegistryEntry<TYPE>&                            \
            EditorWidgetRegistration<TYPE>::reg =                \
                WidgetRegistryEntry<TYPE>::Instance(#TYPE); \
    }}

class HavanaWidget
{
public:
	HavanaWidget(const std::string& inName, const std::string& inHotkey = "")
		: Name(inName)
		, Hotkey(inHotkey)
	{
	}

	virtual void Init() = 0;
	virtual void Destroy() = 0;

	virtual void Update() = 0;
	virtual void Render() = 0;

	bool IsOpen = true;
	std::string Name;
	std::string Hotkey;
};


//class EditorWidgetRegistry
//{
//    ME_SINGLETON_DEFINITION( EditorWidgetRegistry );
//public:
//    EditorWidgetRegistry() = default;
//    using CreateFunc = std::unique_ptr<HavanaWidget>( * )( );
//
//    bool RegisterWidget( std::string_view className, CreateFunc createFunc )
//    {
//        return m_registeredWidgets.emplace( className, createFunc ).second;
//    }
//
//    std::unique_ptr<HavanaWidget> CreateInstance( std::string className )
//    {
//        auto it = m_registeredWidgets.find( className );
//        if( it != m_registeredWidgets.end() )
//        {
//            return it->second();
//        }
//        return nullptr;
//    }
//
//    const std::map<std::string, CreateFunc>& GetWidgetRegistry() const
//    {
//        return m_registeredWidgets;
//    }
//
//private:
//    std::map<std::string, CreateFunc> m_registeredWidgets;
//};
//
//#define ME_REGISTER_EDITOR_WIDGET( ClassName ) \
//    namespace { \
//        UniquePtr<HavanaWidget> Create##ClassName() { return MakeUnique<ClassName>(); } \
//        bool Registered##ClassName = EditorWidgetRegistry::GetInstance().RegisterWidget( #ClassName, Create##ClassName ); \
//    }