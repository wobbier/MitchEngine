#pragma once
#include "HavanaWidget.h"

#include <CLog.h>
#include <imgui.h>
#include <cstdint>
#include <string>
#include <vector>

// Console: per-level toggles with counts, text search, duplicate collapsing, timestamps, auto-scroll,
// a detail pane for the selected message, copy, and double-click to open file:line references in
// the code editor.
class LogWidget
	: public HavanaWidget
{
public:
	LogWidget();

	void Init() final;
	void Destroy() final;

	void Update() final;
	void Render() final;

private:
	struct Row
	{
		size_t Index = 0;      // into CLog::Messages
		uint32_t Count = 1;    // duplicates folded into this row when collapsing
	};

	void RebuildRows();
	bool PassesFilter(const CLog::LogEntry& entry) const;

	bool m_levelEnabled[6] = { true, true, true, true, true, true };
	uint32_t m_levelCounts[6] = {};
	bool m_collapse = false;
	bool m_autoScroll = true;
	char m_search[256] = {};

	std::vector<Row> m_rows;
	uint64_t m_builtMessageCount = ~0ull;
	size_t m_builtSize = 0;
	uint32_t m_builtFilterHash = 0;
	int m_selectedRow = -1;
	size_t m_selectedMessage = static_cast<size_t>(-1);
	float m_detailHeight = 90.f;
	bool m_scrollToBottom = false;
};
