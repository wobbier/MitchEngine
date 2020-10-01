#include <PCH.h>
#include <Cores/UI/UICore.h>

#include <AppCore/Overlay.h>
#include <Components/Camera.h>
#include <Engine/Engine.h>
#include <UI/d3d11/GPUDriverD3D11.h>
#include <UI/d3d11/GPUContextD3D11.h>
#include <UI/FileSystemBasic.h>
#include <UI/FileLogger.h>
#include <UI/FontLoaderWin.h>
#include <UI/UIWindow.h>
#include <Ultralight/platform/Platform.h>
#include <Ultralight/platform/Config.h>
#include <Ultralight/Renderer.h>

#if ME_EDITOR

#include <Cores/EditorCore.h>
#include <EditorApp.h>
#include <Havana.h>

#endif

UICore::UICore(IWindow* window, Moonlight::Renderer* renderer)
	: Base(ComponentFilter().Requires<BasicUIView>())
{
	SetIsSerializable(false);

	m_renderer = renderer;
	m_window = AdoptRef(*new UIWindow(window, GetOverlayManager()));

	std::string fileSystemRoot = Path("").Directory;
	m_fs.reset(new ultralight::FileSystemBasic(fileSystemRoot.c_str()));

	ultralight::Config config;
	config.device_scale = 1.0f;
	config.enable_images = true;
	config.face_winding = ultralight::FaceWinding::kFaceWinding_Clockwise;
	config.force_repaint = true;
	config.font_family_standard = "Arial";
	// ??????
	config.resource_path = "M:\\Projects\\C++\\stack\\Engine\\Modules\\Havana\\..\\..\\..\\Build\\Debug Editor";
	//config_.cache_path = ultralight::String16(std::string(fileSystemRoot.Directory + "ultralight.log").c_str());

	m_context.reset(new ultralight::GPUContextD3D11());
	if (!m_context->Initialize(m_window->width(), m_window->height(), m_window->scale(), m_window->is_fullscreen(), true, false, 1))
	{
		YIKES("Failed to initialize ultralight context");
	}

	m_driver.reset(new ultralight::GPUDriverD3D11(m_context.get(), &m_renderer->GetDevice()));
	m_logger.reset(new ultralight::FileLogger(ultralight::String(std::string(fileSystemRoot + "Ultralight.log").c_str())));
	m_fontLoader.reset(new ultralight::FontLoaderWin());

	ultralight::Platform& platform = ultralight::Platform::instance();
	platform.set_config(config);
	platform.set_file_system(m_fs.get());
	platform.set_font_loader(m_fontLoader.get());
	platform.set_gpu_driver(m_driver.get());
	platform.set_logger(m_logger.get());

	m_uiRenderer = ultralight::Renderer::Create();
}

UICore::~UICore()
{
	CLog::Log(CLog::LogType::Debug, "UICore Destroyed...");
}

void UICore::Init()
{
	CLog::Log(CLog::LogType::Debug, "UICore Initialized...");
}

void UICore::OnEntityAdded(Entity& NewEntity)
{
	BasicUIView& view = NewEntity.GetComponent<BasicUIView>();

	InitUIView(view);
}

void UICore::OnEntityRemoved(Entity& InEntity)
{
	BasicUIView& view = InEntity.GetComponent<BasicUIView>();

	GetOverlayManager()->Remove(m_overlays[view.Index].get());
	m_overlays.erase(m_overlays.begin() + view.Index);
}

void UICore::OnStop()
{
	for (auto overlay : m_overlays)
	{
		GetOverlayManager()->Remove(overlay.get());
	}
	m_overlays.clear();
}

void UICore::Update(float dt)
{
	auto& entities = GetEntities();
	for (auto& InEntity : entities)
	{
		BasicUIView& view = InEntity.GetComponent<BasicUIView>();
		if (!view.IsInitialized)
		{
			InitUIView(view);
		}
	}

	ultralight::MouseEvent mouseEvent;
	mouseEvent.type = ultralight::MouseEvent::kType_MouseMoved;

	Vector2 mousePosition = GetEngine().GetInput().GetMousePosition();

#if ME_EDITOR
	if (!static_cast<EditorApp*>(GetEngine().GetGame())->IsGameRunning())
	{
		return;
	}

	Havana* editor = static_cast<EditorCore*>(GetEngine().GetWorld().lock()->GetCore(EditorCore::GetTypeId()))->GetEditor();
	
	Vector2 windowPosition = GetEngine().GetWindow()->GetPosition();

	mouseEvent.x = (windowPosition.X() + mousePosition.X()) - editor->GameViewRenderLocation.X();
	mouseEvent.y = (windowPosition.Y() + mousePosition.Y()) - editor->GameViewRenderLocation.Y();

	if (mousePosition.IsZero())
	{
		return;
	}
#else
	mouseEvent.x = mousePosition.X();
	mouseEvent.y = mousePosition.Y();
#endif

	static bool hasPressed = false;
	if (GetEngine().GetInput().GetMouseState().leftButton && !hasPressed)
	{
		mouseEvent.button = ultralight::MouseEvent::Button::kButton_Left;
		mouseEvent.type = ultralight::MouseEvent::kType_MouseDown;
		hasPressed = true;
	}
	else if (!GetEngine().GetInput().GetMouseState().leftButton && hasPressed)
	{
		mouseEvent.button = ultralight::MouseEvent::Button::kButton_Left;
		mouseEvent.type = ultralight::MouseEvent::kType_MouseUp;
		hasPressed = false;
	}
	else
	{
		mouseEvent.button = ultralight::MouseEvent::Button::kButton_None;
	}

#if ME_EDITOR
	if (m_renderer->GetViewportMode() == ViewportMode::Game)
#endif
	{
		for (auto& view : m_overlays)
		{
			view->view()->FireMouseEvent(mouseEvent);
		}
	}

	// Update internal logic (timers, event callbacks, etc.)
	m_uiRenderer->Update();
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

void UICore::OnResize(const Vector2& NewSize)
{
	if (m_context)
	{
		m_context->Resize((int)NewSize.X(), (int)NewSize.Y());
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
	overlay->view()->set_load_listener(&view);

	//overlay->view()->LoadHTML(view.SourceFile.Read().c_str());
	ultralight::String str = "file:///" + ultralight::String(view.FilePath.LocalPath.c_str());
	overlay->view()->LoadURL(str);

	m_overlays.push_back(overlay);
	GetOverlayManager()->Add(overlay.get());

	view.IsInitialized = true;
	view.Index = m_overlays.size() - 1;
}

ultralight::OverlayManager* UICore::GetOverlayManager()
{
	return this;
}