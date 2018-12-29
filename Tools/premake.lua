-- premake5.lua
require "vstudio"
dofile "premake-winrt/_preload.lua"
require "premake-winrt/winrt"

newoption {
   trigger     = "with-renderdoc",
   description = "Include support for RenderDoc."
}

newoption {
   trigger     = "uwp",
   description = "Generate a UWP solution."
}

function getPlatformPostfix(thing)
	if (_OPTIONS["uwp"]) then
		return (thing .. "UWP")
	end
	return thing
end

function isUWP()
	if (_OPTIONS["uwp"]) then
		return true
	end

	return false
end

workspace (getPlatformPostfix("MitchEngine"))
	configurations { "Debug", "Release" }
	platforms { "x64" }
	startproject "MitchGame"
	location "../"
	includedirs {
		"../MitchEngine/",
		"../MitchEngine/Source",
		"../ThirdParty/GLM/glm",
		"C:/Program Files/Autodesk/FBX/FBX SDK/2019.0/include",
		"C:/Program Files/RenderDoc"
	}
	if not isUWP() then
		includedirs {
			"../ThirdParty/AssIMP/include",
			"../ThirdParty/Bullet/src",
			"../ThirdParty/GLAD/include/",
			"../ThirdParty/GLAD/src/",
			"../ThirdParty/GLFW/include",
			"../ThirdParty/STB",
			"../ThirdParty/Brofiler/BrofilerCore"
		}
	end
	
	linkoptions {
		"-IGNORE:4221,4006"
	}
	
	libdirs {
		"../Build/%{cfg.buildcfg}"
	}
	if (isUWP()) then
		defines { "ME_PLATFORM_UWP" }
		links {
			"d2d1", "d3d11", "dxgi", "windowscodecs", "dwrite", "libfbxsdk-md"
		}
		libdirs {
			"C:/Program Files/Autodesk/FBX/FBX SDK/2019.0/lib/vs2015store/%{cfg.platform}/%{cfg.buildcfg}"
		}
	else
		defines { "ME_PLATFORM_WIN64" }
		libdirs {
			"../ThirdParty/Lib/Bullet/Win64/%{cfg.buildcfg}",
			"../ThirdParty/Lib/GLFW/Win64/%{cfg.buildcfg}",
			"C:/Program Files/Autodesk/FBX/FBX SDK/2019.0/lib/vs2015/%{cfg.platform}/%{cfg.buildcfg}"
		}
		links {
			"opengl32", "glfw3dll", "libfbxsdk-md"
		}
	end
	
	defines{
		"NOMINMAX"
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

	filter {}

group "Engine"
project (getPlatformPostfix("MitchEngine"))
	kind "StaticLib"
	print("%{cfg.platform}")
	if (isUWP()) then
		system "windowsuniversal"
		consumewinrtextension "true"
	end
	systemversion "10.0.14393.0"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
	location "../MitchEngine"
	pchheader "PCH.h"
	pchsource "../MitchEngine/Source/PCH.cpp"
	files {
		"../MitchEngine/**.h",
		"../MitchEngine/**.cpp",
		"../MitchEngine/**.txt",
		"../Tools/*.lua"
	}
	if not isUWP() then
		excludes {
			"../MitchEngine/**/Graphics/Common/*.*",
			"../MitchEngine/**/Graphics/Content/*.*"
		}
	end
	vpaths {
		["Build"] = "../Tools/*.lua"
	}
	postbuildcommands {
		"mkdir ..\\Build\\%{cfg.buildcfg}\\AppX",
		"fxc /T ps_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\SamplePixelShader.cso Assets\\Shaders\\SamplePixelShader.hlsl",
		"fxc /T ps_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\SamplePixelShader.cso Assets\\Shaders\\SamplePixelShader.hlsl",
		"fxc /T vs_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\SampleVertexShader.cso Assets\\Shaders\\SampleVertexShader.hlsl",
		"fxc /T vs_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\SampleVertexShader.cso Assets\\Shaders\\SampleVertexShader.hlsl",
		"xcopy /y /d  \"..\\ThirdParty\\Lib\\GLFW\\Win64\\%{cfg.buildcfg}\\glfw3.dll\" \"..\\Build\\%{cfg.buildcfg}\""
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
project (getPlatformPostfix("MitchGame"))
	if (isUWP()) then
		kind "StaticLib"
		system "windowsuniversal"
		consumewinrtextension "true"
	else
		kind "ConsoleApp"
	end
	
	systemversion "10.0.14393.0"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
	location "../MitchGame"
	links {
		(getPlatformPostfix("MitchEngine") .. ".lib")
	}
	dependson {
		getPlatformPostfix("MitchEngine")
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
	if (isUWP()) then
		excludes {
			"../MitchGame/Source/main.cpp"
		}
	end

	includedirs {
		"../MitchGame/Source",
		"."
	}

group "App"
if (isUWP()) then
	project (getPlatformPostfix("MitchGame_EntryPoint"))
		kind "ConsoleApp"
		if (isUWP()) then
			system "windowsuniversal"
			consumewinrtextension "true"
			systemversion "10.0.14393.0"
			certificatefile "MitchGame_TemporaryKey.pfx"
			certificatethumbprint "8d68369eaf2c030cd45ca6fce7f367e608b5463e"
			defaultlanguage "en-US"
		end
		language "C++"
		targetdir "../Build/%{cfg.buildcfg}"
		location "../MitchGame_EntryPoint"
		links {
			(getPlatformPostfix("MitchGame") .. ".lib")
		}
		dependson {
			getPlatformPostfix("MitchGame")
		}
		files {
			"../MitchGame_EntryPoint/Assets/**.frag",
			"../MitchGame_EntryPoint/Assets/**.vert",
			"../MitchGame_EntryPoint/Assets/**.png",
			"../MitchGame_EntryPoint/**.h",
			"../MitchGame_EntryPoint/**.cpp",
			"../MitchGame_EntryPoint/**.pfx",
			"../MitchGame_EntryPoint/**.appxmanifest"
		}
		
		includedirs {
			"../MitchGame/Source",
			"."
		}
		filter { "files:Assets/*.png" }
			deploy "true"
end