#include "PCH.h"
#include "UICore.h"
#include "Components/Transform.h"
#include "ECS/ComponentFilter.h"
#include "CLog.h"
#include "Graphics/ShaderCommand.h"
#include "Resource/Resource.h"

#include <iostream>
#include "Resource/ResourceCache.h"
#include "RenderCommands.h"
#include "Engine/Engine.h"
#include "Utils/StringUtils.h"

#include <Ultralight/platform/Platform.h>
#include <Ultralight/platform/Config.h>
#include "UI/UIWindow.h"
#include "AppCore/Overlay.h"
#include <Shlwapi.h>
#include "UI/d3d11/GPUDriverD3D11.h"
#include "UI/d3d11/GPUContextD3D11.h"
#include "UI/OverlayImpl.h"
#include "File.h"
#include <filesystem>
#include "Engine/Input.h"
#include "Dementia.h"

#include "UI/OverlayImpl.h"
//#include "UI/Overlay.cpp"

#if ME_EDITOR
#include "Havana.h"
#include "Cores/EditorCore.h"
#endif
#include <libloaderapi.h>
#include "Components/Camera.h"
#include "UI/FileLogger.h"
#include "UI/FontLoaderWin.h"

UICore::UICore(IWindow* window)
	: Base(ComponentFilter().Requires<Transform>().Requires<BasicUIView>())
{
	IsSerializable = false;
	m_renderer = &GetEngine().GetRenderer();

	m_window = AdoptRef(*new UIWindow(window, GetOverlayManager()));
	ultralight::Platform& platform = ultralight::Platform::instance();
	ultralight::Config config_;
	config_.device_scale = 1.0f;
	config_.enable_images = true;
	config_.face_winding = ultralight::FaceWinding::kFaceWinding_Clockwise;
	config_.force_repaint = true;
	config_.font_family_standard = "Arial";

	// Generate cache path
	//String cache_path = GetRoamingAppDataPath();
	//cache_path = PlatformFileSystem::AppendPath(cache_path, settings_.developer_name);
	//cache_path = PlatformFileSystem::AppendPath(cache_path, settings_.app_name);
	//PlatformFileSystem::MakeAllDirectories(cache_path);

	//String log_path = PlatformFileSystem::AppendPath(cache_path, "ultralight.log");



	Path fileSystemRoot = Path("");
	m_fs.reset(new ultralight::FileSystemBasic(fileSystemRoot.Directory.c_str()));
	config_.resource_path = "M:\\Projects\\C++\\stack\\Engine\\Modules\\Havana\\..\\..\\..\\Build\\Debug Editor";

	m_logger.reset(new ultralight::FileLogger(ultralight::String(std::string(fileSystemRoot.Directory + "ultralight.log").c_str())));
	platform.set_logger(m_logger.get());
	m_context.reset(new ultralight::GPUContextD3D11());
	//config_.cache_path = ultralight::String16(std::string(fileSystemRoot.Directory + "ultralight.log").c_str());
	platform.set_config(config_);

	UIWindow* win = static_cast<UIWindow*>(m_window.get());
	if (!m_context->Initialize(win->width(), win->height(), win->scale(), win->is_fullscreen(), true, false, 1))
	{
		//MessageBoxW(NULL, (LPCWSTR)L"Failed to initialize D3D11 context", (LPCWSTR)L"Notification", MB_OK);
		exit(-1);
	}

	m_driver.reset(new ultralight::GPUDriverD3D11(m_context.get(), &GetEngine().GetRenderer().GetDevice()));

	platform.set_gpu_driver(m_driver.get());
	platform.set_file_system(m_fs.get());
	m_fontLoader.reset(new ultralight::FontLoaderWin());
	platform.set_font_loader(m_fontLoader.get());
	//win->set_app_listener(this);
	m_uiRenderer = (ultralight::Renderer::Create());
	
}

UICore::~UICore()
{
	CLog::Log(CLog::LogType::Debug, "UICore Destroyed...");
}

void UICore::Init()
{
	CLog::Log(CLog::LogType::Debug, "UICore Initialized...");
	if (m_renderer)
		m_renderer->ClearUIText();
	if (!IsInitialized)
	{
		IsInitialized = true;
	}
	for (auto overlay : m_overlays)
	{
		GetOverlayManager()->Remove(overlay.get());
	}
	m_overlays.clear();
}

void UICore::OnEntityAdded(Entity& NewEntity)
{
	if (NewEntity.HasComponent<BasicUIView>())
	{
		BasicUIView& view = NewEntity.GetComponent<BasicUIView>();
		
		InitUIView(view);
	}
}

void UICore::OnEntityRemoved(Entity& InEntity)
{
	if (InEntity.HasComponent<BasicUIView>())
	{
		BasicUIView view = InEntity.GetComponent<BasicUIView>();

		GetOverlayManager()->Remove(m_overlays[view.Index].get());

		m_overlays.erase(m_overlays.begin() + view.Index);
	}
}

void UICore::Update(float dt)
{
	// Update internal logic (timers, event callbacks, etc.)
	m_uiRenderer->Update();

	ultralight::MouseEvent evt;
	evt.type = ultralight::MouseEvent::kType_MouseMoved;
#if ME_EDITOR
	Havana* editor = static_cast<EditorCore*>(GetEngine().GetWorld().lock()->GetCore(EditorCore::GetTypeId()))->GetEditor();
	
	evt.x = (GetEngine().GetWindow()->GetPosition().X() + GetEngine().GetInput().GetMousePosition().X()) - editor->GameViewRenderLocation.X();
	evt.y = (GetEngine().GetWindow()->GetPosition().Y() + GetEngine().GetInput().GetMousePosition().Y()) - editor->GameViewRenderLocation.Y();

	Vector2 MousePosition = GetEngine().GetInput().GetMousePosition();
	if (MousePosition == Vector2(0, 0))
	{
		return;
	}

#else
	evt.x = GetEngine().GetInput().GetMousePosition().X();
	evt.y = GetEngine().GetInput().GetMousePosition().Y();
#endif
	static bool hasPressed = false;
	if (GetEngine().GetInput().GetMouseState().leftButton && !hasPressed)
	{
		evt.button = ultralight::MouseEvent::Button::kButton_Left;
		evt.type = ultralight::MouseEvent::kType_MouseDown;
		hasPressed = true;
	}
	else if (!GetEngine().GetInput().GetMouseState().leftButton && hasPressed)
	{
		evt.button = ultralight::MouseEvent::Button::kButton_Left;
		evt.type = ultralight::MouseEvent::kType_MouseUp;
		hasPressed = false;
	}
	else
	{
		evt.button = ultralight::MouseEvent::Button::kButton_None;
	}
	//ultralight::View::FireMouseEvent(evt);
#if ME_EDITOR
	if (GetEngine().GetRenderer().GetViewportMode() == ViewportMode::Game)
#endif
	{
		for (auto& view : m_overlays)
		{
			view->view()->FireMouseEvent(evt);
		}
	}

	auto Renderables = GetEntities();
	for (auto& InEntity : Renderables)
	{
		Transform& transform = InEntity.GetComponent<Transform>();

		if (InEntity.HasComponent<BasicUIView>())
		{
			BasicUIView& view = InEntity.GetComponent<BasicUIView>();
			if (!view.IsInitialized)
			{
				InitUIView(view);
			}
		}
	}

}

void UICore::OnStop()
{
}

void UICore::Render()
{
	OPTICK_EVENT("UI Render", Optick::Category::Rendering);
	m_driver->BeginSynchronize();

	// Render all active views to command lists and dispatch calls to GPUDriver
	m_uiRenderer->Render();

	m_driver->EndSynchronize();

	// Draw any pending commands to screen
	if (m_driver->HasCommandsPending())
	{
		m_context->BeginDrawing();
		m_driver->DrawCommandList();

		// Perform any additional drawing (Overlays) here...
		//DrawOverlays();

		// Flip buffers here.
		if (m_window)
		{
			Draw();
		}
		m_context->EndDrawing();
	}
}

ultralight::OverlayManager* UICore::GetOverlayManager()
{
	return this;
}

void UICore::OnResize(const Vector2& NewSize)
{
	if (m_context)
	{
		m_context->Resize((int)NewSize.X(), (int)NewSize.Y());
		//m_driver->RebindBackbuffer();
		for (auto overlay : overlays_)
		{
			overlay->Resize((int)NewSize.X(), (int)NewSize.Y());
		}
	}
}

void UICore::InitUIView(BasicUIView& view)
{
	ultralight::Ref<ultralight::View> newView = m_uiRenderer->CreateView(Camera::CurrentCamera->OutputSize.X(), Camera::CurrentCamera->OutputSize.Y(), true, nullptr);
	ultralight::RefPtr<ultralight::Overlay> overlay = ultralight::Overlay::Create(*m_window.get(), newView, 0, 0);
	//overlay->view()->LoadHTML(view.SourceFile.Read().c_str());
	overlay->view()->set_load_listener(&view);
	ultralight::String str = "file:///" + ultralight::String(view.FilePath.LocalPath.c_str());
	overlay->view()->LoadURL(str);
	m_overlays.push_back(overlay);
	GetOverlayManager()->Add(overlay.get());
	view.IsInitialized = true;
	view.Index = m_overlays.size() - 1;
}