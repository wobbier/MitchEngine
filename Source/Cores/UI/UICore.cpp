#include <PCH.h>
#include <Cores/UI/UICore.h>

#include <AppCore/Overlay.h>
#include <Components/Camera.h>
#include <Engine/Engine.h>
#include <Ultralight/platform/Platform.h>
#include <Ultralight/platform/Config.h>
#include <imgui.h>
#include <Utils/ImGuiUtils.h>
#include <AppCore/Platform.h>
#include <BGFXRenderer.h>

UICore::UICore(IWindow* window, BGFXRenderer* renderer)
	: Base(ComponentFilter().Requires<BasicUIView>())
	, m_uiTexture(BGFX_INVALID_HANDLE)
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
	config.use_gpu_renderer = false;
	// ??????
	config.resource_path = "M:\\Projects\\C++\\stack\\Engine\\Modules\\Havana\\..\\..\\..\\Build\\Debug Editor";
	//config_.cache_path = ultralight::String16(std::string(fileSystemRoot.Directory + "ultralight.log").c_str());

	m_context.reset(new GPUContext());
	if (!m_context->Initialize(m_window->width(), m_window->height(), m_window->scale(), m_window->is_fullscreen(), true, false, 1))
	{
		YIKES("Failed to initialize ultralight context");
	}

	m_driver.reset(new GPUDriverBGFX(m_context.get()));
	m_logger.reset(new ultralight::FileLogger(ultralight::String(std::string(fileSystemRoot + "Ultralight.log").c_str())));
#if ME_PLATFORM_UWP
	m_fontLoader.reset(new ultralight::FontLoaderWin());
#else
    m_fontLoader.reset(ultralight::GetPlatformFontLoader());
#endif
	ultralight::Platform& platform = ultralight::Platform::instance();
	platform.set_config(config);
	platform.set_file_system(m_fs.get());
	platform.set_font_loader(m_fontLoader.get());
	//if (config.use_gpu_renderer)
	{
		platform.set_gpu_driver(m_driver.get());
	}
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

//	Vector2 mousePosition = GetEngine().GetInput().GetMousePosition();
//
//#if ME_EDITOR
//	if (!static_cast<EditorApp*>(GetEngine().GetGame())->IsGameRunning())
//	{
//		return;
//	}
//
//	Havana* editor = static_cast<EditorCore*>(GetEngine().GetWorld().lock()->GetCore(EditorCore::GetTypeId()))->GetEditor();
//	
//	Vector2 windowPosition = GetEngine().GetWindow()->GetPosition();
//
//	mouseEvent.x = (windowPosition.X() + mousePosition.X()) - editor->GameViewRenderLocation.X();
//	mouseEvent.y = (windowPosition.Y() + mousePosition.Y()) - editor->GameViewRenderLocation.Y();
//
//	if (mousePosition.IsZero())
//	{
//		return;
//	}
//#else
//	mouseEvent.x = mousePosition.X();
//	mouseEvent.y = mousePosition.Y();
//#endif
//
//	static bool hasPressed = false;
//	if (GetEngine().GetInput().GetMouseState().leftButton && !hasPressed)
//	{
//		mouseEvent.button = ultralight::MouseEvent::Button::kButton_Left;
//		mouseEvent.type = ultralight::MouseEvent::kType_MouseDown;
//		hasPressed = true;
//	}
//	else if (!GetEngine().GetInput().GetMouseState().leftButton && hasPressed)
//	{
//		mouseEvent.button = ultralight::MouseEvent::Button::kButton_Left;
//		mouseEvent.type = ultralight::MouseEvent::kType_MouseUp;
//		hasPressed = false;
//	}
//	else
//	{
//		mouseEvent.button = ultralight::MouseEvent::Button::kButton_None;
//	}
//
//#if ME_EDITOR
//	//if (m_renderer->GetViewportMode() == ViewportMode::Game)
//#endif
//	{
//		for (auto& view : m_overlays)
//		{
//			view->view()->FireMouseEvent(mouseEvent);
//		}
//	}

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
		//m_context->BeginDrawing();
		m_driver->DrawCommandList();

		// Perform any additional drawing (Overlays) here...
		//DrawOverlays();

		// Flip buffers here.
		if (m_window)
		{
			Draw();
		}
		//m_context->EndDrawing();
	}

	for (auto ent : GetEntities())
	{
		ultralight::BitmapSurface* surface = (ultralight::BitmapSurface*)(ent.GetComponent<BasicUIView>().ViewRef->surface());

		if (!surface->dirty_bounds().IsEmpty())
		{
			CopyBitmapToTexture(surface->bitmap());

			surface->ClearDirtyBounds();
		}
	}
}

void UICore::OnResize(const Vector2& NewSize)
{
	if (m_context)
	{
		m_context->Resize(NewSize);
		for (auto overlay : overlays_)
		{
			overlay->Resize((int)NewSize.x, (int)NewSize.y);
		}
	}

	if (NewSize.IsZero())
	{
		return;
	}

	if (NewSize != UISize)
	{
		if (bgfx::isValid(m_uiTexture))
		{
			bgfx::destroy(m_uiTexture);
		}

		m_uiTexture = bgfx::createTexture2D(static_cast<uint16_t>(NewSize.x)
			, static_cast<uint16_t>(NewSize.y)
			, false
			, 1
			, bgfx::TextureFormat::BGRA8
			, BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT
		);
		UISize = NewSize;
	}
}

void UICore::InitUIView(BasicUIView& view)
{
	ultralight::Ref<ultralight::View> newView = m_uiRenderer->CreateView(static_cast<uint32_t>(Camera::CurrentCamera->OutputSize.x), static_cast<uint32_t>(Camera::CurrentCamera->OutputSize.y), true, nullptr);

	ultralight::RefPtr<ultralight::Overlay> overlay = ultralight::Overlay::Create(*m_window.get(), newView, 0, 0);
	overlay->view()->set_load_listener(&view);

	//overlay->view()->LoadHTML(view.SourceFile.Read().c_str());
	ultralight::String str = "file:///" + ultralight::String(view.FilePath.LocalPath.c_str());
	overlay->view()->LoadURL(str);

	m_overlays.push_back(overlay);
	GetOverlayManager()->Add(overlay.get());

	view.IsInitialized = true;
	view.Index = m_overlays.size() - 1;
	view.ViewRef = overlay->view();
}

ultralight::OverlayManager* UICore::GetOverlayManager()
{
	return this;
}

void UICore::CopyBitmapToTexture(ultralight::RefPtr<ultralight::Bitmap> bitmap)
{
	void* pixels = bitmap->LockPixels();

	//uint32_t width = bitmap->width();
	//uint32_t height = bitmap->height();
	uint32_t stride = bitmap->row_bytes();

	//bitmap->WritePNG(Path("Assets/TestUI.png").FullPath.c_str());

	{
		const uint16_t tw = bitmap->bounds().width();
		const uint16_t th = bitmap->bounds().height();
		const uint16_t tx = bitmap->bounds().x();
		const uint16_t ty = bitmap->bounds().y();

		const bgfx::Memory* mem = bgfx::makeRef(pixels, stride);

		if (bgfx::isValid(m_uiTexture) && Camera::CurrentCamera)
		{
			bgfx::updateTexture2D(m_uiTexture, 0, 0, tx, ty, tw, th, mem, stride);
			GetEngine().GetRenderer().GetCameraCache().Get(Camera::CurrentCamera->GetCameraId())->UITexture = m_uiTexture;
		}
	}

	bitmap->UnlockPixels();
}

#if ME_EDITOR

void UICore::OnEditorInspect()
{
	Base::OnEditorInspect();
	ImGui::Image(m_uiTexture, ImVec2(UISize.x, UISize.y));
}

#endif
