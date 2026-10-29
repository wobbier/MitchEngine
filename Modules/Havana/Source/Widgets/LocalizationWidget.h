#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"

#if USING( ME_EDITOR )

#include <map>
#include <string>

// The project's string tables (PLAY-6): every key of the base language (English) beside one
// language's text, editable, plus the text looked up without a translation since the language
// was set (add it to the tables from here). Saves Assets/Localization/<code>.json with what
// differs from the engine's table.
class LocalizationWidget
	: public HavanaWidget
{
public:
	LocalizationWidget();

	void Init() override {}
	void Destroy() override {}
	void Update() override {}
	void Render() override;

private:
	void Reload();

	std::string m_language;
	std::map<std::string, std::string> m_base;
	std::map<std::string, std::string> m_table;
	char m_filter[128] = {};
	bool m_loaded = false;
	bool m_dirty = false;
};

#endif
