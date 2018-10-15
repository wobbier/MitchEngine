-- premake5.lua
require "vstudio"
dofile "premake-winrt/_preload.lua"
require "premake-winrt/winrt"

newoption {
   trigger     = "with-renderdoc",
   description = "Include support for RenderDoc."
}

workspace "MitchEngine"
	configurations { "Debug", "Release", "Debug Editor", "Release Editor" }
	platforms { "x64" }
	startproject "MitchGame"
	location "../"
	includedirs {
		"../MitchEngine/",
		"../MitchEngine/Source",
		"../ThirdParty/GLM/glm",
		"C:/Program Files/RenderDoc"
	}

	libdirs {
		"../Build/%{cfg.buildcfg}"
	}
	links {
		"d2d1", "d3d11", "dxgi", "windowscodecs", "dwrite"
	}

	filter "configurations:Debug*"
	defines { "DEBUG" }
	symbols "On"

	filter "configurations:Release*"
	defines { "NDEBUG" }
	optimize "On"
	libdirs {
		"$(VCInstallDir)\\lib\\store\\amd64",
		"$(VCInstallDir)\\lib\\amd64"
	}
	
	filter "configurations:*Editor"
	defines { "MAN_EDITOR=1" }

	filter {}

group "Engine"
project "MitchEngine"
	kind "StaticLib"
	system "windowsuniversal"
	consumewinrtextension "true"
	systemversion "10.0.14393.0"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
	location "../MitchEngine"
	pchheader "PCH.h"
	pchsource "../MitchEngine/Source/PCH.cpp"
	files {
		"../MitchEngine/Assets/**.*",
		"../MitchEngine/**.h",
		"../MitchEngine/**.cpp",
		"../MitchEngine/**.txt",
		"../Tools/**.lua"
	}
	vpaths {
		["Build"] = "../Tools/*.lua"
	}
	
	configuration "with-renderdoc"
	defines { "MAN_ENABLE_RENDERDOC", "__cplusplus_winrt" }
	postbuildcommands {
		"xcopy /y /d  \"C:\\Program Files\\RenderDoc\\renderdoc.dll\" \"$(ProjectDir)$(OutDir)\""
	}

	filter "configurations:Debug*"
	postbuildcommands {
	}
	filter "configurations:Release*"
	postbuildcommands {
	}
	filter {}

group "Games"
project "MitchGame"
	kind "ConsoleApp"
	system "windowsuniversal"
	consumewinrtextension "true"
	systemversion "10.0.14393.0"
	certificatefile "MitchGame_TemporaryKey.pfx"
	certificatethumbprint "8d68369eaf2c030cd45ca6fce7f367e608b5463e"
	defaultlanguage "en-US"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
	location "../MitchGame"
	links {
		"MitchEngine.lib"
	}
	files {
		"../MitchGame/Assets/**.frag",
		"../MitchGame/Assets/**.vert",
		"../MitchGame/Assets/**.png",
		"../MitchGame/**.h",
		"../MitchGame/**.cpp",
		"../MitchGame/**.pfx",
		"../MitchGame/**.appxmanifest"
	}
	includedirs {
		"../MitchGame/Source",
		"."
	}
	postbuildcommands {
		"fxc /T ps_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\SamplePixelShader.cso Content\\SamplePixelShader.hlsl",
		"fxc /T ps_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\SamplePixelShader.cso Content\\SamplePixelShader.hlsl",
		"fxc /T vs_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\SampleVertexShader.cso Content\\SampleVertexShader.hlsl",
		"fxc /T vs_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\SampleVertexShader.cso Content\\SampleVertexShader.hlsl"
		}
	filter { "files:Assets/*.png" }
		deploy "true"
