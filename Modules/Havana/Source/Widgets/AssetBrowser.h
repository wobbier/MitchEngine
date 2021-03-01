#pragma once
#include <chrono>
#include <string>
#include <unordered_map>
#include <functional>
#include <filesystem>
#include <thread>
#include <stack>
#include <map>
#include "Path.h"
#include "Pointers.h"
#include "Graphics/Texture.h"
#include "File.h"
#include "imgui.h"

class Transform;

namespace ImGui
{
#define IMGUI_FLAGS_NONE        UINT8_C(0x00)
#define IMGUI_FLAGS_ALPHA_BLEND UINT8_C(0x01)

	inline ImTextureID toId(bgfx::TextureHandle _handle, uint8_t _flags, uint8_t _mip)
	{
		union { struct { bgfx::TextureHandle handle; uint8_t flags; uint8_t mip; } s; ImTextureID id; } tex;
		tex.s.handle = _handle;
		tex.s.flags = _flags;
		tex.s.mip = _mip;
		return tex.id;
	}

	// Helper function for passing bgfx::TextureHandle to ImGui::Image.
	inline void Image(bgfx::TextureHandle _handle
		, uint8_t _flags
		, uint8_t _mip
		, const ImVec2& _size
		, const ImVec2& _uv0 = ImVec2(0.0f, 0.0f)
		, const ImVec2& _uv1 = ImVec2(1.0f, 1.0f)
		, const ImVec4& _tintCol = ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
		, const ImVec4& _borderCol = ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
	)
	{
		Image(toId(_handle, _flags, _mip), _size, _uv0, _uv1, _tintCol, _borderCol);
	}

	// Helper function for passing bgfx::TextureHandle to ImGui::Image.
	inline void Image(bgfx::TextureHandle _handle
		, const ImVec2& _size
		, const ImVec2& _uv0 = ImVec2(0.0f, 0.0f)
		, const ImVec2& _uv1 = ImVec2(1.0f, 1.0f)
		, const ImVec4& _tintCol = ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
		, const ImVec4& _borderCol = ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
	)
	{
		Image(_handle, IMGUI_FLAGS_ALPHA_BLEND, 0, _size, _uv0, _uv1, _tintCol, _borderCol);
	}

	// Helper function for passing bgfx::TextureHandle to ImGui::ImageButton.
	inline bool ImageButton(bgfx::TextureHandle _handle
		, uint8_t _flags
		, uint8_t _mip
		, const ImVec2& _size
		, const ImVec2& _uv0 = ImVec2(0.0f, 0.0f)
		, const ImVec2& _uv1 = ImVec2(1.0f, 1.0f)
		, int _framePadding = -1
		, const ImVec4& _bgCol = ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
		, const ImVec4& _tintCol = ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
	)
	{
		return ImageButton(toId(_handle, _flags, _mip), _size, _uv0, _uv1, _framePadding, _bgCol, _tintCol);
	}

	// Helper function for passing bgfx::TextureHandle to ImGui::ImageButton.
	inline bool ImageButton(bgfx::TextureHandle _handle
		, const ImVec2& _size
		, const ImVec2& _uv0 = ImVec2(0.0f, 0.0f)
		, const ImVec2& _uv1 = ImVec2(1.0f, 1.0f)
		, int _framePadding = -1
		, const ImVec4& _bgCol = ImVec4(0.0f, 0.0f, 0.0f, 0.0f)
		, const ImVec4& _tintCol = ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
	)
	{
		return ImageButton(_handle, IMGUI_FLAGS_ALPHA_BLEND, 0, _size, _uv0, _uv1, _framePadding, _bgCol, _tintCol);
	}

	inline void NextLine()
	{
		SetCursorPosY(GetCursorPosY() + GetTextLineHeightWithSpacing());
	}

	inline bool MouseOverArea()
	{
		return false
			|| ImGui::IsAnyItemActive()
			|| ImGui::IsAnyItemHovered()
			|| ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)
			//			|| ImGuizmo::IsOver()
			;
	}

} // namespace ImGui

enum class FileStatus : unsigned int
{
	Created = 0,
	Modified,
	Deleted
};

class AssetBrowser
{
public:
	enum class AssetType : unsigned int
	{
		Unknown = 0,
		Folder,
		Texture,
		Audio,
		Model,
		Level,
		Shader,
		Template,
		Text,
		Prefab,
		Count
	};
	struct AssetDescriptor
	{
		std::string Name;
		File MetaFile;
		Path FullPath;
		AssetType Type;
	};
	struct Directory
	{
		std::map<std::string, Directory> Directories;
		std::vector<AssetBrowser::AssetDescriptor> Files;
		Path FullPath;
	};
	AssetBrowser() = delete;
	AssetBrowser(const std::string& pathToWatch, std::chrono::duration<int, std::milli> delay);
	~AssetBrowser();
	void Start(const std::function<void(std::string, FileStatus)>& action);
	std::chrono::duration<int, std::milli> Delay;
	std::string PathToWatch;

	void ThreadStart(const std::function<void(std::string, FileStatus)>& action);

	void Draw();

	void Recursive(Directory& dir);
	void ProccessDirectory(const std::filesystem::directory_entry& file);
private:
	bool ProccessDirectoryRecursive(std::string& dir, Directory& dirRef, const std::filesystem::directory_entry& file);

	void SavePrefab(json& d, Transform* CurrentTransform, bool IsRoot);
	std::unordered_map<std::string, std::filesystem::file_time_type> Paths;
	bool IsRunning = true;
	bool Contains(const std::string& key);
	std::thread fileBrowser;
	Directory AssetDirectory;
	std::unordered_map<std::string, SharedPtr<Moonlight::Texture>> Icons;
	AssetDescriptor* SelectedAsset = nullptr;
};