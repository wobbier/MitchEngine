#pragma once
#include <HavanaWidget.h>
#include "Dementia.h"

#if USING( ME_EDITOR )

#include <functional>
#include <string>
#include <vector>

// ED-3 onboarding. The tour walks through the editor's main panels one step at a time (each step
// opens and focuses its panel); it starts on a user's first launch and from Help > Take the Tour.
class TourWidget
	: public HavanaWidget
{
public:
	struct Step
	{
		const char* Title;
		const char* Text;
		const char* Panel;      // "" for none
	};

	TourWidget();

	void Init() override {}
	void Destroy() override {}
	void Update() override {}
	void Render() override;

	void Start();
	void Next();
	void Back();
	void Finish();
	int GetStep() const { return m_step; }
	static const std::vector<Step>& GetSteps();

	// Opens and focuses a panel by title (Havana::ShowWidget).
	std::function<void( const std::string& )> ShowPanel;

private:
	void Enter( int InStep );
	int m_step = 0;
};


// What's New: Engine/Docs/WhatsNew.md, rendered as headings and bullets. It opens by itself when
// the page changed since the user last saw it (the per-user preference Editor.WhatsNewSeen).
class WhatsNewWidget
	: public HavanaWidget
{
public:
	WhatsNewWidget();

	void Init() override;
	void Destroy() override {}
	void Update() override {}
	void Render() override;

	static constexpr const char* kPage = "Engine/Docs/WhatsNew.md";

private:
	std::vector<std::string> m_lines;
	std::string m_signature;
};

#endif
