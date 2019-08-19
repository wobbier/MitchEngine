#include "EditorApp.h"
#include "Engine/Engine.h"

int main()
{
	EditorApp* app = new EditorApp();
	GetEngine().Init(app);
	GetEngine().Run();

	return 0;
}
//#include <AppCore/App.h>
//#include <AppCore/Window.h>
//#include <AppCore/Overlay.h>
//#include "Window/Win32Window.h"
//#include "UI/UIWindow.h"
//#include "UI2/OverlayManager.h"
//#include "UI2/AppWin.h"
//#include "Cores/UI/UICore.h"
//#include "Engine/Engine.h"
//
//class MyApp : public ultralight::WindowListener {
//	ultralight::RefPtr<ultralight::App> app_;
//	ultralight::RefPtr<ultralight::Window> window_;
//	ultralight::RefPtr<ultralight::Overlay> overlay_;
//	Win32Window* EngineWindow;
//	UIWindow* NewWindow;
//	UICore* ptr;
//public:
//	MyApp() {
//		///
//		/// Create our main App instance.
//		///
//		app_ = ultralight::App::Create();
//		EngineWindow = new Win32Window("NEW WINDOW", [this](const Vector2& Size) { OnResize(Size.X(), Size.Y()); }, 1280, 720);
//		ptr = new UICore(EngineWindow);
//		GetEngine().UI = ptr;
//		//NewWindow = new UIWindow(EngineWindow, new ultralight::OverlayManager());
//		//window_ = AdoptRef(*NewWindow);
//		///
//		/// Create our Window with the Resizable window flag.
//		///
//// 		window_ = Window::Create(app_->main_monitor(), 300, 300, false,
//// 			kWindowFlags_Titled | kWindowFlags_Resizable);
//
//		///
//		/// Set our window title.
//		///
//		//window_->SetTitle("Tutorial 3 - Resize Me!");
//
//		///
//		/// Bind our App's main window.
//		///
//		/// @note This MUST be done before creating any overlays or calling App::Run
//		///
//		//app_->set_window(*window_.get());
//
//		///
//		/// Create our Overlay, use the same dimensions as our Window.
//		///
//		ptr->Init();
//		/*overlay_ = ultralight::Overlay::Create(*window_.get(), window_->width(),
//			window_->height(), 0, 0);
//*/
//		///
//		/// Load a string of HTML into our overlay's View
//		///
//		//overlay_->view()->LoadHTML("<center>Hello World!</center>");
//
//		///
//		/// Register our MyApp instance as a WindowListener so we can handle the
//		/// Window's OnResize event below.
//		///
//		//window_->set_listener(this);
//	}
//
//	virtual ~MyApp() {}
//
//	///
//	/// Inherited from WindowListener, not used.
//	///
//	virtual void OnClose() override {}
//
//	///
//	/// Inherited from WindowListener, called when the Window is resized.
//	///
//	virtual void OnResize(uint32_t width, uint32_t height) override {
//		///
//		/// Resize our Overlay to match the new Window dimensions.
//		///
//		ptr->OnResize(Vector2(width, height));
//	}
//
//	void Run() {
//		while (true)
//		{
//			EngineWindow->ParseMessageQueue();
//			ptr->Update(0);
//			ptr->Render();
//			Sleep(0);
//		}
//	}
//};
//
//int main() {
//	MyApp app;
//	app.Run();
//
//	return 0;
//}
