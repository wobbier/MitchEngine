#include "EditorApp.h"
#include "Engine/Engine.h"
#include <Widgets/AssetBrowser.h>

int main(int argc, char** argv)
{
	for (int i = 0; i < argc; ++i)
	{
		std::printf("argv[%d]: %s\n", i, argv[i]);
		if (std::string(argv[i]) == "-CompileAssets")
		{
			bgfx::Init init;
			init.resolution.width = static_cast<uint32_t>(10);
			init.resolution.height = static_cast<uint32_t>(10);
			init.resolution.reset = BGFX_RESET_VSYNC;

			bgfx::init(init);
			AssetBrowserWidget browser = AssetBrowserWidget(nullptr);
			browser.BuildAssets();
			return 0;
		}
	}
	EditorApp app(argc, argv);
	GetEngine().Init(&app);
	GetEngine().Run();
	return 0;
}