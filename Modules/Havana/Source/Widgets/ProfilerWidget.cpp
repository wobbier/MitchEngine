#include "ProfilerWidget.h"
#include "Profiling/FrameStats.h"
#include <imgui.h>
#include <ImGuiTheme.h>
#include <algorithm>
#include <vector>

#if USING( ME_EDITOR )

ProfilerWidget::ProfilerWidget()
	: HavanaWidget("Profiler")
{
	IsOpen = false;
}


void ProfilerWidget::Render()
{
	FrameStats& stats = FrameStats::Get();
	// Per-view GPU timing has a cost; only gather it while someone is looking.
	stats.DetailedGpuTimings = IsOpen;
	if (!IsOpen)
	{
		return;
	}
	if (!ImGui::Begin(Name.c_str(), &IsOpen))
	{
		ImGui::End();
		return;
	}

	static std::vector<float> s_frozenFrames;
	static std::vector<float> s_frozenGpu;
	ImGui::Checkbox("Pause", &m_paused);
	if (!m_paused)
	{
		// Unroll the ring buffers oldest-first.
		const std::vector<float>& frames = stats.GetFrameHistory();
		const std::vector<float>& gpu = stats.GetGpuHistory();
		const size_t offset = stats.GetHistoryOffset();
		s_frozenFrames.resize(frames.size());
		s_frozenGpu.resize(gpu.size());
		for (size_t i = 0; i < frames.size(); ++i)
		{
			s_frozenFrames[i] = frames[(offset + i) % frames.size()];
			s_frozenGpu[i] = gpu[(offset + i) % gpu.size()];
		}
	}

	const FrameStats::RenderStats& render = stats.GetRenderStats();
	ImGui::SameLine();
	ImGui::Text("%.1f fps | CPU %.2f ms | GPU %.2f ms | render thread %.2f ms", stats.GetFramesPerSecond(), stats.GetSmoothedFrameMilliseconds(), render.GpuMilliseconds, render.RenderThreadMilliseconds);
	ImGui::Text("Draw calls %u | Triangles %u | GPU memory %.1f / %.1f MB", render.DrawCalls, render.Triangles, render.GpuMemoryUsed / (1024.0 * 1024.0), render.GpuMemoryMax / (1024.0 * 1024.0));

	const float maxFrame = std::max(16.7f, *std::max_element(s_frozenFrames.begin(), s_frozenFrames.end()) * 1.1f);
	ImGui::PlotLines("##CpuHistory", s_frozenFrames.data(), static_cast<int>(s_frozenFrames.size()), 0, "CPU frame (ms)", 0.f, maxFrame, ImVec2(-1.f, ImGuiTheme::Em(4.f)));
	ImGui::PlotLines("##GpuHistory", s_frozenGpu.data(), static_cast<int>(s_frozenGpu.size()), 0, "GPU (ms)", 0.f, maxFrame, ImVec2(-1.f, ImGuiTheme::Em(2.75f)));

	if (ImGui::CollapsingHeader("CPU (last frame, smoothed)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::BeginTable("##Scopes", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(4.f));
			ImGui::TableSetupColumn("% frame", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(6.5f));
			ImGui::TableHeadersRow();
			const double frame = std::max(stats.GetSmoothedFrameMilliseconds(), 0.001);
			for (const FrameStats::Scope& scope : stats.GetScopes())
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				// Nested scopes step in by a fraction of a tree indent (scopes nest deeply).
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + static_cast<float>(scope.Depth) * ImGuiTheme::Em(0.75f));
				ImGui::TextUnformatted(scope.Name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%.3f", scope.Smoothed);
				ImGui::TableSetColumnIndex(2);
				const float fraction = static_cast<float>(std::clamp(scope.Smoothed / frame, 0.0, 1.0));
				ImGui::ProgressBar(fraction, ImVec2(-1.f, 0.f));
			}
			ImGui::EndTable();
		}
	}

	// ME-320: who ran the frame's jobs. Bars are each lane's share of the frame.
	const FrameStats::JobStats& jobs = stats.GetJobStats();
	if (ImGui::CollapsingHeader("Job workers (smoothed)", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Text("%u workers | %.0f jobs | busy %.2f ms | main ran %u (%.2f ms) | main waited %.2f ms", jobs.WorkerCount, jobs.SmoothedJobs,
			jobs.SmoothedWorkerBusy, jobs.MainJobs, jobs.MainBusyMilliseconds, jobs.SmoothedWait);
		// ME-319: the background lane (IO, nav builds, cooks), its lanes listed after the workers.
		if (jobs.BackgroundCount > 0)
		{
			ImGui::Text("background: %u threads | %u tasks | busy %.2f ms | %u queued", jobs.BackgroundCount, jobs.BackgroundJobs,
				jobs.BackgroundBusyMilliseconds, jobs.BackgroundQueued);
		}
		if (!jobs.Lanes.empty() && ImGui::BeginTable("##JobLanes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
			ImVec2(0.f, ImGuiTheme::Em(12.f))))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Lane", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Jobs", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(3.5f));
			ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(4.f));
			ImGui::TableSetupColumn("% frame", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(6.5f));
			ImGui::TableHeadersRow();
			const double frame = std::max(stats.GetSmoothedFrameMilliseconds(), 0.001);
			for (const FrameStats::JobLane& lane : jobs.Lanes)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(lane.Name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", lane.Jobs);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.3f", lane.Smoothed);
				ImGui::TableSetColumnIndex(3);
				ImGui::ProgressBar(static_cast<float>(std::clamp(lane.Smoothed / frame, 0.0, 1.0)), ImVec2(-1.f, 0.f));
			}
			ImGui::EndTable();
		}
	}

	if (ImGui::CollapsingHeader("GPU views", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (render.Views.empty())
		{
			ImGui::TextDisabled("No per-view timings reported by the renderer backend.");
		}
		else if (ImGui::BeginTable("##Views", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("View", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(4.f));
			ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(4.f));
			ImGui::TableSetupColumn("Tris", ImGuiTableColumnFlags_WidthFixed, ImGuiTheme::Em(5.f));
			ImGui::TableHeadersRow();
			for (const FrameStats::GpuView& view : render.Views)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(view.Name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%.3f", view.CpuMilliseconds);
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%.3f", view.GpuMilliseconds);
				ImGui::TableSetColumnIndex(3);
				if (view.Triangles > 0) ImGui::Text("%llu", static_cast<unsigned long long>(view.Triangles));
			}
			ImGui::EndTable();
		}
	}
	ImGui::End();
}

#endif
