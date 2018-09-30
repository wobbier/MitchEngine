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
		"../ThirdParty/AssIMP/include",
		"../ThirdParty/Bullet/src",
		"../ThirdParty/GLAD/include/",
		"../ThirdParty/GLAD/src/",
		"../ThirdParty/GLM/glm",
		"../ThirdParty/GLFW/include",
		"../ThirdParty/STB",
		"../ThirdParty/Brofiler/BrofilerCore",
		"C:/Program Files/RenderDoc"
	}

	links {
		"opengl32",
		"glfw3dll",
		"BrofilerCore"
	}

	filter "configurations:Debug*"
	defines { "DEBUG" }
	symbols "On"
	links {
		"BulletDynamics_Debug",
		"BulletCollision_Debug",
		"LinearMath_Debug"
	}
	links {
		"d2d1", "d3d11", "dxgi", "windowscodecs", "dwrite"
	}
	libdirs {
		"../ThirdParty/Bullet/lib/Debug",
		"../ThirdParty/GLFW/src/Debug",
		"../ThirdParty/Brofiler/Bin/vs2017/x64/Debug"
	}

	filter "configurations:Release*"
	defines { "NDEBUG" }
	optimize "On"
	links {
		"BulletDynamics",
		"BulletCollision",
		"LinearMath"
	}
	libdirs {
		"../ThirdParty/Bullet/lib/Release",
		"../ThirdParty/GLFW/src/Release",
		"../ThirdParty/Brofiler/Bin/vs2017/x64/Release",
		"$(VCInstallDir)\\lib\\store\\amd64",
		"$(VCInstallDir)\\lib\\amd64"
	}
	
	filter "configurations:*Editor"
	defines { "MAN_EDITOR=1" }

	filter {}

group "Engine"
project "MitchEngine"
	kind "StaticLib"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
	location "../MitchEngine"
	pchheader "PCH.h"
	pchsource "../MitchEngine/Source/PCH.cpp"
	links {
		"LibAssIMP"
	}
	dependson {
		"LibAssIMP",
		"glfw",
		"LibBulletCollision",
		"LibBulletDynamics",
		"LibLinearMath",
		"LibBrofiler",
		"BrofilerApp"
	}
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
	defines { "MAN_ENABLE_RENDERDOC" }
	postbuildcommands {
		"xcopy /y /d  \"C:\\Program Files\\RenderDoc\\renderdoc.dll\" \"$(ProjectDir)$(OutDir)\""
	}

	filter "configurations:Debug*"
	postbuildcommands {
		"xcopy /y /d  \"..\\ThirdParty\\AssIMP\\bin\\Debug\\*.dll\" \"$(ProjectDir)$(OutDir)\"",
		"xcopy /y /d  \"..\\ThirdParty\\GLFW\\src\\Debug\\*.dll\" \"$(ProjectDir)$(OutDir)\"",
		"xcopy /y /d  \"..\\ThirdParty\\Brofiler\\Bin\\vs2017\\x64\\Debug\\*.dll\" \"$(ProjectDir)$(OutDir)\""
	}
	filter "configurations:Release*"
	postbuildcommands {
		"xcopy /y /d  \"..\\ThirdParty\\AssIMP\\bin\\Release\\*.dll\" \"$(ProjectDir)$(OutDir)\"",
		"xcopy /y /d  \"..\\ThirdParty\\GLFW\\src\\Release\\*.dll\" \"$(ProjectDir)$(OutDir)\"",
		"xcopy /y /d  \"..\\ThirdParty\\Brofiler\\Bin\\vs2017\\x64\\Release\\*.dll\" \"$(ProjectDir)$(OutDir)\""
	}

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
	links "MitchEngine"
	dependson {
		"MitchEngine"
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

group "Engine/ThirdParty"
externalproject "glfw"
	location "../ThirdParty/GLFW/src"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802C"
	kind "SharedLib"
	language "C++"
	toolset "v141"
	targetdir "../Build/%{cfg.buildcfg}"
	filter "configurations:Debug Editor"
	configuration "Debug"

group "Engine/ThirdParty/Bullet"
externalproject "LibBulletCollision"
	location "../ThirdParty/Bullet/src/BulletCollision"
	filename "BulletCollision"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802D"
	kind "StaticLib"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"

externalproject "LibLinearMath"
	location "../ThirdParty/Bullet/src/LinearMath"
	filename "LinearMath"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802E"
	kind "StaticLib"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"

externalproject "LibBulletDynamics"
	location "../ThirdParty/Bullet/src/BulletDynamics"
	filename "BulletDynamics"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802F"
	kind "StaticLib"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
	
group "Engine/ThirdParty/Assimp"
externalproject "LibAssIMP"
	location "../ThirdParty/AssIMP/code"
	filename "Assimp"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D58021"
	kind "SharedLib"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
externalproject "LibAssimpIrrXML"
	location "../ThirdParty/AssIMP/contrib/irrXML"
	filename "irrXML"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D58022"
	kind "SharedLib"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
externalproject "LibAssimpZLibStatic"
	location "../ThirdParty/AssIMP/contrib/zlib"
	filename "zlibstatic"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D58023"
	kind "SharedLib"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
	
group "Engine/ThirdParty/Brofiler"
externalproject "LibBrofiler"
	location "../ThirdParty/Brofiler/Build/vs2017"
	filename "BrofilerCore"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D58024"
	kind "SharedLib"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
	
externalproject "BrofilerApp"
	location "../ThirdParty/Brofiler/Brofiler"
	filename "Brofiler"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D58025"
	kind "WindowedApp"
	language "C#"
	targetdir "../Build/%{cfg.buildcfg}"