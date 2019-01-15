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

newoption {
   trigger     = "gfxapi",
   value       = "API",
   description = "Choose a particular 3D API for rendering",
   allowed = {
      { "directx",  "DirectX" }
   }
}

function getPlatformPostfix(thing)
	if (_OPTIONS["uwp"]) then
		return (thing .. "UWP")
	end
	return thing
end

isUWP = _OPTIONS["uwp"]
withDirectX = _OPTIONS["gfxapi"] == "directx" or isUWP

-- Engine workspace
workspace (getPlatformPostfix("MitchEngine"))
	configurations { "Debug", "Release" }
	platforms { "x64" }
	startproject "MitchGame"
	location "../"
	includedirs {
		"../MitchEngine/",
		"../MitchEngine/Source",
		"../ThirdParty/Bullet/src",
		"../ThirdParty/GLM/glm",
		"C:/Program Files/Autodesk/FBX/FBX SDK/2019.0/include",
		"C:/Program Files/RenderDoc",
		"../ThirdParty/Brofiler/BrofilerCore",
		"../Modules/Moonlight/Source",
		"../Modules/Dementia/Source"
	}
	
	libdirs {
		"../Build/%{cfg.buildcfg}"
	}

	if isUWP then
		defines { "ME_PLATFORM_UWP" }
		libdirs {
			"../ThirdParty/Lib/Bullet/Win64/%{cfg.buildcfg}",
			"C:/Program Files/Autodesk/FBX/FBX SDK/2019.0/lib/vs2015store/%{cfg.platform}/%{cfg.buildcfg}",
			"../ThirdParty/Lib/Brofiler/UWP/%{cfg.buildcfg}"
		}
	else
		defines { "ME_PLATFORM_WIN64" }
		libdirs {
			"../ThirdParty/Lib/Bullet/Win64/%{cfg.buildcfg}",
			"../ThirdParty/Lib/GLFW/Win64/%{cfg.buildcfg}",
			"C:/Program Files/Autodesk/FBX/FBX SDK/2019.0/lib/vs2015/%{cfg.platform}/%{cfg.buildcfg}",
			"../ThirdParty/Lib/Brofiler/Win64/%{cfg.buildcfg}"
		}
	end
	
	links {
		"BrofilerCore",
		getPlatformPostfix("Dementia"),
		"libfbxsdk-md"
	}

	-- Platform specific options
	if withDirectX then
		defines { "ME_DIRECTX" }
		links {
			"d2d1", "d3d11", "dxgi", "windowscodecs", "dwrite"
		}
	end
	
	linkoptions {
		"-IGNORE:4221,4006,4264,4099"
	}

	defines{
		"NOMINMAX"
	}

	filter "configurations:Debug*"
	defines { "DEBUG" }
	symbols "On"
	links {
		"BulletDynamics_Debug",
		"BulletCollision_Debug",
		"LinearMath_Debug"
	}

	filter "configurations:Release*"
	defines { "NDEBUG" }
	optimize "On"
	libdirs {
		"$(VCInstallDir)\\lib\\store\\amd64",
		"$(VCInstallDir)\\lib\\amd64"
	}
	links {
		"BulletDynamics_MinsizeRel",
		"BulletCollision_MinsizeRel",
		"LinearMath_MinsizeRel"
	}

------------------------------------------------------- Renderer Project -----------------------------------------------------

group "Engine/Modules"
project (getPlatformPostfix("Moonlight"))
	kind "StaticLib"
	if (isUWP) then
		system "windowsuniversal"
		consumewinrtextension "true"
	end
	systemversion "10.0.14393.0"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
	location "../Modules/Moonlight"
	includedirs {
		"../Modules/Moonlight/Source/"
	}
	files {
		"../Modules/Moonlight/Source/**.*"
	}
	vpaths {
		["Source"] = "../Source/**.*",
		["Source"] = "../Source/*.*"
	}
	
------------------------------------------------------- Utility Project ------------------------------------------------------

project (getPlatformPostfix("Dementia"))
	kind "StaticLib"
	--if (isUWP) then
	--	system "windowsuniversal"
	--	consumewinrtextension "true"
	--end
	systemversion "10.0.14393.0"
	language "C++"
	targetdir "../Build/%{cfg.buildcfg}"
	location "../Modules/Dementia"
	removeincludedirs "*"
	removelinks "*"
	includedirs {
		"../Modules/Dementia/Source/"
	}
	files {
		"../Modules/Dementia/Source/**.*"
	}
	vpaths {
		["Source"] = "../Source/**.*",
		["Source"] = "../Source/*.*"
	}

------------------------------------------------------- Engine Project -------------------------------------------------------

group "Engine"
project (getPlatformPostfix("MitchEngine"))
	kind "StaticLib"
	if (isUWP) then
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
		"../MitchEngine/Source/**.h",
		"../MitchEngine/Source/**.cpp",
		"../MitchEngine/Source/**.txt",
		"../Tools/*.lua"
	}

	filter {}

	dependson {
		getPlatformPostfix("Moonlight")
	}

	links {
		(getPlatformPostfix("Moonlight") .. ".lib")
	}

	if not isUWP then
		excludes {
			"../MitchEngine/**/Graphics/Common/*.*",
			"../MitchEngine/**/Graphics/Content/*.*"
		}
	end
	vpaths {
		["Build"] = "../Tools/*.lua",
		["Source"] = "../Source/**.*"
	}
	postbuildcommands {
		"mkdir ..\\Build\\%{cfg.buildcfg}\\AppX",
		"fxc /T ps_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\SamplePixelShader.cso Assets\\Shaders\\SamplePixelShader.hlsl",
		"fxc /T ps_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\SamplePixelShader.cso Assets\\Shaders\\SamplePixelShader.hlsl",
		"fxc /T vs_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\SampleVertexShader.cso Assets\\Shaders\\SampleVertexShader.hlsl",
		"fxc /T vs_4_0_level_9_3 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\SampleVertexShader.cso Assets\\Shaders\\SampleVertexShader.hlsl"
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
	if (isUWP) then
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
	includedirs {
		"../MitchGame/Source"
	}
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
	if (isUWP) then
		excludes {
			"../MitchGame/Source/main.cpp"
		}
	end

	includedirs {
		"../MitchGame/Source",
		"."
	}

group "App"
if (isUWP) then
	project (getPlatformPostfix("MitchGame_EntryPoint"))
		kind "ConsoleApp"
		system "windowsuniversal"
		consumewinrtextension "true"
		systemversion "10.0.14393.0"
		certificatefile "MitchGame_TemporaryKey.pfx"
		certificatethumbprint "8d68369eaf2c030cd45ca6fce7f367e608b5463e"
		defaultlanguage "en-US"
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