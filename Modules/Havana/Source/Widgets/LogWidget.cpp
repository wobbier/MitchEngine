#include "LogWidget.h"
#include <optick.h>
#include <Utils/PlatformUtils.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>
#include <unordered_map>

namespace
{
	const char* kLevelNames[6] = { "None", "Trace", "Debug", "Info", "Warning", "Error" };

	ImVec4 LevelColor(CLog::LogType type)
	{
		switch (type)
		{
		case CLog::LogType::Error: return ImVec4(1.f, 0.36f, 0.36f, 1.f);
		case CLog::LogType::Warning: return ImVec4(1.f, 0.8f, 0.3f, 1.f);
		case CLog::LogType::Info: return ImVec4(0.85f, 0.85f, 0.85f, 1.f);
		case CLog::LogType::Debug: return ImVec4(0.45f, 0.75f, 1.f, 1.f);
		case CLog::LogType::Trace: return ImVec4(0.55f, 0.55f, 0.55f, 1.f);
		default: return ImVec4(0.7f, 0.7f, 0.7f, 1.f);
		}
	}

	std::string FirstLine(const std::string& text)
	{
		const size_t newline = text.find('\n');
		return newline == std::string::npos ? text : text.substr(0, newline) + " ...";
	}

	bool ContainsInsensitive(const std::string& haystack, const char* needle)
	{
		if (!needle || !*needle)
		{
			return true;
		}
		const size_t length = std::strlen(needle);
		auto it = std::search(haystack.begin(), haystack.end(), needle, needle + length, [](char a, char b) {
			return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
		});
		return it != haystack.end();
	}

	// First "path/file.ext:line" reference in a message.
	bool FindSourceLocation(const std::string& text, std::string& outFile, int& outLine)
	{
		static const std::regex pattern(R"(([A-Za-z]:)?[\w./\\-]+\.(cpp|cc|c|hpp|h|inl|cs|sc|sh|lvl|prefab|json):(\d+))");
		std::smatch match;
		if (!std::regex_search(text, match, pattern))
		{
			return false;
		}
		const std::string whole = match.str(0);
		const size_t colon = whole.find_last_of(':');
		outFile = whole.substr(0, colon);
		outLine = std::atoi(whole.c_str() + colon + 1);
		return true;
	}

	void FormatTime(double seconds, char* buffer, size_t size)
	{
		const int minutes = static_cast<int>(seconds / 60.0);
		const double rest = seconds - minutes * 60.0;
		std::snprintf(buffer, size, "%02d:%06.3f", minutes, rest);
	}
}


LogWidget::LogWidget()
	: HavanaWidget("Log")
{
}


void LogWidget::Init()
{
}


void LogWidget::Destroy()
{
}


void LogWidget::Update()
{
}


bool LogWidget::PassesFilter(const CLog::LogEntry& entry) const
{
	const int level = static_cast<int>(entry.Type);
	if (level >= 0 && level < 6 && !m_levelEnabled[level])
	{
		return false;
	}
	return ContainsInsensitive(entry.Message, m_search);
}


void LogWidget::RebuildRows()
{
	m_rows.clear();
	std::fill(std::begin(m_levelCounts), std::end(m_levelCounts), 0u);
	std::unordered_map<std::string, size_t> collapsed;
	for (size_t i = 0; i < CLog::Messages.size(); ++i)
	{
		const CLog::LogEntry& entry = CLog::Messages[i];
		const int level = static_cast<int>(entry.Type);
		if (level >= 0 && level < 6)
		{
			++m_levelCounts[level];
		}
		if (!PassesFilter(entry))
		{
			continue;
		}
		if (m_collapse)
		{
			const std::string key = std::to_string(level) + entry.Message;
			auto existing = collapsed.find(key);
			if (existing != collapsed.end())
			{
				++m_rows[existing->second].Count;
				continue;
			}
			collapsed.emplace(key, m_rows.size());
		}
		m_rows.push_back({ i, 1 });
	}
}


void LogWidget::Render()
{
	if (!IsOpen)
	{
		return;
	}
	OPTICK_CATEGORY("Log", Optick::Category::Debug);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
	const bool open = ImGui::Begin("Log", &IsOpen, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar);
	ImGui::PopStyleVar();
	if (!open)
	{
		ImGui::End();
		return;
	}

	// Other threads append to CLog::Messages; hold the log lock while the list is used.
	std::lock_guard<std::recursive_mutex> logLock(CLog::GetInstance().GetMutex());

	uint32_t filterHash = m_collapse ? 1u : 0u;
	for (int i = 0; i < 6; ++i)
	{
		filterHash = filterHash * 3u + (m_levelEnabled[i] ? 1u : 0u);
	}
	for (const char* c = m_search; *c; ++c)
	{
		filterHash = filterHash * 31u + static_cast<unsigned char>(*c);
	}
	const uint64_t total = CLog::GetInstance().GetTotalMessageCount();
	if (total != m_builtMessageCount || CLog::Messages.size() != m_builtSize || filterHash != m_builtFilterHash)
	{
		const bool grew = total != m_builtMessageCount;
		RebuildRows();
		m_builtMessageCount = total;
		m_builtSize = CLog::Messages.size();
		m_builtFilterHash = filterHash;
		m_scrollToBottom = grew && m_autoScroll;
		// Keep the selection on the same message if it survived trimming/filtering.
		m_selectedRow = -1;
		for (size_t i = 0; i < m_rows.size(); ++i)
		{
			if (m_rows[i].Index == m_selectedMessage)
			{
				m_selectedRow = static_cast<int>(i);
				break;
			}
		}
	}

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::Button("Clear"))
		{
			CLog::Messages.clear();
			m_selectedRow = -1;
			m_selectedMessage = static_cast<size_t>(-1);
		}
		ImGui::Checkbox("Collapse", &m_collapse);
		ImGui::Checkbox("Auto-scroll", &m_autoScroll);
		ImGui::Separator();
		for (int level = 1; level < 6; ++level)
		{
			ImGui::PushID(level);
			char label[64];
			std::snprintf(label, sizeof(label), "%s %u", kLevelNames[level], m_levelCounts[level]);
			const ImVec4 color = LevelColor(static_cast<CLog::LogType>(level));
			ImGui::PushStyleColor(ImGuiCol_Text, m_levelEnabled[level] ? color : ImVec4(color.x, color.y, color.z, 0.35f));
			if (ImGui::Button(label))
			{
				m_levelEnabled[level] = !m_levelEnabled[level];
			}
			ImGui::PopStyleColor();
			ImGui::PopID();
		}
		ImGui::Separator();
		ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x - 8.f, 80.f));
		ImGui::InputTextWithHint("##LogSearch", "Search", m_search, sizeof(m_search));
		ImGui::EndMenuBar();
	}

	const bool hasSelection = m_selectedRow >= 0 && m_selectedRow < static_cast<int>(m_rows.size());
	const float listHeight = hasSelection ? std::max(ImGui::GetContentRegionAvail().y - m_detailHeight - 6.f, 40.f) : 0.f;

	const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV;
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.f, 1.f));
	if (ImGui::BeginTable("##LogTable", 3, flags, ImVec2(0.f, listHeight)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.f);
		ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 60.f);
		ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(m_rows.size()));
		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const Row& entryRow = m_rows[row];
				const CLog::LogEntry& entry = CLog::Messages[entryRow.Index];
				ImGui::TableNextRow();
				ImGui::PushID(row);

				ImGui::TableSetColumnIndex(0);
				char time[32];
				FormatTime(entry.Timestamp, time, sizeof(time));
				ImGui::TextDisabled("%s", time);

				ImGui::TableSetColumnIndex(1);
				const int level = static_cast<int>(entry.Type);
				ImGui::TextColored(LevelColor(entry.Type), "%s", level >= 0 && level < 6 ? kLevelNames[level] : "?");

				ImGui::TableSetColumnIndex(2);
				const std::string line = FirstLine(entry.Message);
				ImGui::PushStyleColor(ImGuiCol_Text, entry.Type == CLog::LogType::Error || entry.Type == CLog::LogType::Warning ? LevelColor(entry.Type) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
				if (ImGui::Selectable(line.c_str(), row == m_selectedRow, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
				{
					m_selectedRow = row;
					m_selectedMessage = entryRow.Index;
					if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					{
						std::string file;
						int lineNumber = 0;
						if (FindSourceLocation(entry.Message, file, lineNumber))
						{
							PlatformUtils::OpenInCodeEditor(file, lineNumber);
						}
					}
				}
				ImGui::PopStyleColor();
				if (ImGui::BeginPopupContextItem("LogRowContext"))
				{
					if (ImGui::MenuItem("Copy Message"))
					{
						ImGui::SetClipboardText(entry.Message.c_str());
					}
					std::string file;
					int lineNumber = 0;
					if (FindSourceLocation(entry.Message, file, lineNumber) && ImGui::MenuItem("Open Source Location"))
					{
						PlatformUtils::OpenInCodeEditor(file, lineNumber);
					}
					ImGui::EndPopup();
				}
				if (entryRow.Count > 1)
				{
					ImGui::SameLine(ImGui::GetContentRegionMax().x - 40.f);
					ImGui::TextDisabled("x%u", entryRow.Count);
				}
				ImGui::PopID();
			}
		}

		if (m_scrollToBottom)
		{
			ImGui::SetScrollHereY(1.f);
			m_scrollToBottom = false;
		}
		// Scrolling up pauses auto-scroll; reaching the bottom resumes it.
		if (ImGui::GetScrollMaxY() > 0.f && ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.f)
		{
			m_autoScroll = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f;
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleVar();

	if (hasSelection)
	{
		// Splitter + full message of the selected row.
		ImGui::InvisibleButton("##LogSplitter", ImVec2(-1.f, 6.f));
		if (ImGui::IsItemHovered() || ImGui::IsItemActive())
		{
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
		}
		if (ImGui::IsItemActive())
		{
			m_detailHeight = std::clamp(m_detailHeight - ImGui::GetIO().MouseDelta.y, 30.f, 600.f);
		}
		const CLog::LogEntry& entry = CLog::Messages[m_rows[m_selectedRow].Index];
		ImGui::BeginChild("##LogDetail", ImVec2(0.f, 0.f), false);
		ImGui::Indent(6.f);
		ImGui::PushTextWrapPos(0.f);
		ImGui::TextUnformatted(entry.Message.c_str());
		ImGui::PopTextWrapPos();
		ImGui::TextDisabled("thread %u", entry.ThreadId);
		ImGui::Unindent(6.f);
		ImGui::EndChild();
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && hasSelection && ImGui::IsKeyChordPressed(ImGuiMod_Shortcut | ImGuiKey_C) && !ImGui::GetIO().WantTextInput)
	{
		ImGui::SetClipboardText(CLog::Messages[m_rows[m_selectedRow].Index].Message.c_str());
	}
	ImGui::End();
}
