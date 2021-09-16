-- premake5.lua
require "vstudio"
dofile "premake-winrt/_preload.lua"
require "premake-winrt/winrt"

newoption {
  trigger     = "project-type",
  description = "Generate a UWP solution.",
   default     = "Win64",
   value = "Win64",
  allowed = {
    { "UWP",  "UWP" },
	{ "Win64", "Win64" },
	{ "macOS", "macOS" }
  }
}

newoption {
  trigger     = "with-renderdoc",
  description = "Include support for RenderDoc."
}

newoption {
  trigger     = "project-name",
  description = "Notifies premake we're generating a game solution."
}

-- Modify these in your game premake --
CertificateFile = "";
CertificateThumbprint = "";
---------------------------------------


function isPlatform(plat)
	return (_OPTIONS["project-type"] == plat)
end

function getPlatformPostfix(name)
  if (isPlatform("UWP")) then
    return (name .. "UWP")
  end
  return name
end

function postBuildCopyCommands(dir)
    filter { "system:windows" }
    if isPlatform("UWP") then
	  postbuildcommands {
	    "xcopy /y /d  \"$(ProjectDir)" .. dir .. "Engine\\ThirdParty\\UltralightSDK\\bin\\UWP\\*.dll\" \"$(ProjectDir)$(OutDir)\"",
	    "xcopy /y /d  \"$(ProjectDir)" .. dir .. "Engine\\ThirdParty\\UltralightSDK\\bin\\UWP\\*.dll\" \"$(ProjectDir)$(OutDir)AppX\""
	  }
    end

    if isPlatform("Win64") then
	  postbuildcommands {
	    "xcopy /y /d  \"$(ProjectDir)" .. dir .. "Engine\\ThirdParty\\UltralightSDK\\bin\\Win64\\*.dll\" \"$(ProjectDir)$(OutDir)\""
	  }
    end
    filter { "system:windows", "Debug*" }
    if isPlatform("Win64") then
	  postbuildcommands {
	    "xcopy /y /d  \"$(ProjectDir)" .. dir .. "Engine\\ThirdParty\\Lib\\SDL\\Win64\\Debug\\*.dll\" \"$(ProjectDir)$(OutDir)\""
	  }
    end
    if isPlatform("UWP") then
	  postbuildcommands {
	    "xcopy /y /d  \"$(ProjectDir)" .. dir .. "Engine\\ThirdParty\\Lib\\SDL\\UWP\\Debug\\*.dll\" \"$(ProjectDir)$(OutDir)\""
      }
    end
    
    filter { "system:windows", "Release*" }
    if isPlatform("Win64") then
	  postbuildcommands {
	    "xcopy /y /d  \"$(ProjectDir)" .. dir .. "Engine\\ThirdParty\\Lib\\SDL\\Win64\\Release\\*.dll\" \"$(ProjectDir)$(OutDir)\""
	  }
    end
    if isPlatform("UWP") then
	  postbuildcommands {
	    "xcopy /y /d  \"$(ProjectDir)" .. dir .. "Engine\\ThirdParty\\Lib\\SDL\\UWP\\Release\\*.dll\" \"$(ProjectDir)$(OutDir)\""
      }
    end
    filter {}
end

function macOSEntryPoints()
    if isPlatform("macOS") then
    filter { "Debug Editor" }

        libdirs {
            "../../../Build/Debug",
            "../../Engine/Build/Debug",
            "../../Engine/ThirdParty/Lib/Assimp/macOS/Debug",
            "../../Engine/ThirdParty/Lib/Bullet/macOS/Debug",
            "../ThirdParty/Lib/GLFW/Win64/Debug",
            "../../Engine/ThirdParty/Lib/BGFX/macOS/Debug",
            "../../Engine/ThirdParty/Lib/Optick/macOS/Debug",
            "../../Engine/ThirdParty/Lib/SDL/macOS/Debug",
            "../../Engine/ThirdParty/UltralightSDK/lib/macOS"
        }

        filter { "Debug*" }
        libdirs {
            "Build/Debug",
            "../Engine/Build/Debug",
            "Engine/ThirdParty/Lib/Assimp/macOS/Debug",
            "Engine/ThirdParty/Lib/Bullet/macOS/Debug",
            "../ThirdParty/Lib/GLFW/Win64/Debug",
            "Engine/ThirdParty/Lib/BGFX/macOS/Debug",
            "Engine/ThirdParty/Lib/Optick/macOS/Debug",
            "Engine/ThirdParty/Lib/SDL/macOS/Debug",
            "Engine/ThirdParty/UltralightSDK/lib/macOS"
        }

        links {
            (getPlatformPostfix("MitchEngine")),
            "SDL2d",
            "ImGui",
            "Moonlight",
            "Cocoa.framework",
            "ForceFeedback.framework",
            "IOKit.framework",
            "CoreVideo.framework",
            "Carbon.framework",
            "Metal.framework",
            "Foundation.framework",
            "Quartz.framework",
            "iconv",
            "bgfxDebug",
            "OptickCore",
            "bimg_decodeDebug",
            "bimgDebug",
            "bxDebug",
            "BulletDynamics",
            "BulletCollision",
            "LinearMath",
            "zlibstatic",
            (getPlatformPostfix("MitchEngine")),
            "Dementia",
            "ImGui",
            "UltralightCore",
            "Ultralight",
            "WebCore",
            "AppCore",
            getPlatformPostfix(ProjectName)
        }
        filter {}
    end
end

isUWP = isPlatform("UWP")
withRenderdoc = _OPTIONS["with-renderdoc"]
withDirectX = isPlatform("Win64") or isUWP
dirPrefix = "../";
ProjectName = _OPTIONS["project-name"];

if(_OPTIONS["project-name"]) then
  dirPrefix = "../../"
else
  ProjectName = "MitchEngine"
  CertificateFile = "MitchGame_TemporaryKey.pfx"
  CertificateThumbprint = "8d68369eaf2c030cd45ca6fce7f367e608b5463e"
end

-- Engine workspace
workspace (getPlatformPostfix(ProjectName))
if isUWP then
configurations { "Debug", "Release" }
platforms { "UWP" }
else
configurations { "Debug", "Release", "Debug Editor", "Release Editor" }
end
if isPlatform("Win64") then
platforms { "Win64" }
end

architecture "x64" 
cppdialect "C++17"
if isUWP then
  startproject (getPlatformPostfix(ProjectName .. "_EntryPoint"))
else
  startproject "Havana"
end

location (dirPrefix)
includedirs {
    "../Source",
    "../ThirdParty/Bullet/src",
    "../ThirdParty/RenderDoc",
    "../ThirdParty/Optick/src",
    "../Modules/Moonlight/Source",
    "../Modules/Dementia/Source",
    "../Modules/Havana/Source",
    "../ThirdParty/Assimp/include",
    "../ThirdParty/ImGui",
    "../Modules/ImGUI/Source",
    "../ThirdParty/PerlinNoise",
    "../ThirdParty/UltralightSDK/include",
    "../ThirdParty/JSON/single_include",
    "../ThirdParty/glm",
    "../ThirdParty/bgfx/include",
    "../ThirdParty/bx/include",
    "../ThirdParty/bx/include/compat/msvc/",
    "../ThirdParty/bimg/include",
    "../ThirdParty/SDL/include"
}

-- macOS
sysincludedirs {
    "../Source",
    "../ThirdParty/Bullet/src",
    "../ThirdParty/RenderDoc",
    "../ThirdParty/Assimp/include",
    "../ThirdParty/Optick/src",
    "../Modules/Moonlight/Source",
    "../Modules/Dementia/Source",
    "../ThirdParty/bgfx/include",
    "../ThirdParty/ImGui",
    "../Modules/ImGUI/Source",
    "../ThirdParty/bgfx/include",
    "../ThirdParty/bx/include",
    "../ThirdParty/JSON/single_include",
    "../ThirdParty/glm",
    "../ThirdParty/UltralightSDK/include",
    "../ThirdParty/bimg/include",
    "../ThirdParty/SDL/include",
}

--flags { "FatalWarnings" }

---- Visual Studio ----
filter "action:vs*"
    links {
        "OptickCore.lib",
        "Usp10.lib",
        "Ultralight.lib",
        "UltralightCore.lib",
        "WebCore.lib",
    }

if isPlatform("UWP") then
    defines { "ME_PLATFORM_UWP" }
    --includedirs { (dirPrefix) .. "packages/directxtk_uwp.2018.11.20.1/include" }
    libdirs {
        "../ThirdParty/UltralightSDK/lib/UWP",
        "../ThirdParty/UltralightSDK/bin/UWP"
    }
    links {
        "assimp-vc140-mt",
    }
end
if isPlatform("Win64") then
    defines { "ME_PLATFORM_WIN64" }
    --includedirs { (dirPrefix) .. "packages/directxtk_desktop_2015.2018.11.20.1/include" }
    libdirs {
        "../ThirdParty/UltralightSDK/lib/Win64",
        "../ThirdParty/UltralightSDK/bin/Win64"
    }
    links {
        "dwmapi",
        "assimp-vc140-mt",
        "Shlwapi.lib",
        "AppCore.lib"
    }
end
---- Visual Studio ----

filter { }

---- macOS ----
if isPlatform("macOS") then
    defines { "ME_PLATFORM_MACOS" }
end
---- macOS ----

libdirs {
  "../Build/%{cfg.buildcfg}",
}

links {
  "Dementia",
  "IrrXML",
  --"DirectXTK",
}

linkoptions {
  "-IGNORE:4221,4006,4264,4099"
}

defines{
  "NOMINMAX",
  "ULTRALIGHT_IMPLEMENTATION",
  "_DISABLE_EXTENDED_ALIGNED_STORAGE"
}

configuration "*Editor"
defines {
	"ME_EDITOR=1"
}
configuration {}

filter "configurations:Debug*"
defines { "DEBUG" }
symbols "On"
links {
  "bgfxDebug",
  "bimg_decodeDebug",
  "bimgDebug",
  "bxDebug",
}

---- macOS ----
if isPlatform("macOS") then
    syslibdirs {
        "../ThirdParty/Lib/Assimp/macOS/Debug",
        "../ThirdParty/Lib/Bullet/macOS/Debug",
        "../ThirdParty/Lib/BGFX/macOS/Debug",
        "../ThirdParty/Lib/Optick/macOS/Debug",
        "../ThirdParty/Lib/SDL/macOS/Debug"
    }
    links {
        "BulletDynamics",
        "BulletCollision",
        "LinearMath",
        "zlibstatic",
        "SDL2d",
        "assimp"
    }
end

---- Debug VS ----
filter { "Debug*", "action:vs*" }
    libdirs {
        "../ThirdParty/Lib/Assimp/Debug"
    }
    links {
        "BulletDynamics_Debug",
        "BulletCollision_Debug",
        "LinearMath_Debug",
        "zlibstaticd",
    }
    
if isPlatform("UWP") then
    defines {"USE_OPTICK=0"}
    libdirs {
        "$(VCInstallDir)\\lib\\store\\amd64",
        "$(VCInstallDir)\\lib\\amd64",
        "../ThirdParty/Lib/BGFX/UWP/Debug",
        "../ThirdParty/Lib/Bullet/Win64/Debug",
        "../ThirdParty/Lib/Optick/UWP/Debug",
        "../ThirdParty/Lib/SDL/UWP/Debug"
    }
    links {
        "SDL2",
    }
end

if isPlatform("Win64") then
    libdirs {
      "../ThirdParty/Lib/Bullet/Win64/Debug",
      "../ThirdParty/Lib/GLFW/Win64/Debug",
      "../ThirdParty/Lib/BGFX/Win64/Debug",
      "../ThirdParty/Lib/Optick/Win64/Debug",
      "../ThirdParty/Lib/SDL/Win64/Debug"
    }
    links {
        "SDL2d",
    }
end
filter {}
---- Debug VS ----

----- Release -----
filter "configurations:Release*"
defines { "NDEBUG" }
optimize "On"
libdirs {
  "../ThirdParty/Lib/SDL/Win64/Release",
  "../ThirdParty/Lib/Assimp/Release"
}
links {
  "BulletDynamics_MinsizeRel",
  "BulletCollision_MinsizeRel",
  "LinearMath_MinsizeRel",
  "zlibstatic",
  "SDL2",
  "bgfxRelease",
  "bimg_decodeRelease.lib",
  "bimgRelease.lib",
  "bxRelease.lib",
  "SDL2",
}

if isUWP then
  defines {"USE_OPTICK=0"}
  libdirs {
    "$(VCInstallDir)\\lib\\store\\amd64",
    "$(VCInstallDir)\\lib\\amd64",
    "../ThirdParty/Lib/BGFX/UWP/Release",
    "../ThirdParty/Lib/Bullet/Win64/Release",
    "../ThirdParty/Lib/Optick/UWP/Release",
    "../ThirdParty/Lib/SDL/UWP/Release"
  }
else
  libdirs {
    "../ThirdParty/Lib/Bullet/Win64/Release",
    "../ThirdParty/Lib/GLFW/Win64/Release",
    "../ThirdParty/Lib/BGFX/Win64/Release",
    "../ThirdParty/Lib/Optick/Win64/Release",
    "../ThirdParty/Lib/SDL/Win64/Release"
  }
end

configuration {}
filter {}

--if withRenderdoc then
  defines { "ME_ENABLE_RENDERDOC" } ---, "__cplusplus_winrt"
--end

------------------------------------------------------- Renderer Project -----------------------------------------------------

group "Engine/Modules"
project (getPlatformPostfix("Moonlight"))
kind "StaticLib"
if (isUWP) then
  system "windowsuniversal"
  consumewinrtextension "true"
end
language "C++"
targetdir "../Build/%{cfg.buildcfg}"
location "../Modules/Moonlight"
--flags { "FatalWarnings" }
dependson { "Dementia" }

filter "action:vs*"
	systemversion "10.0.14393.0"
filter {}

filter "system:macosx"
	excludes {
	}
	sysincludedirs {
	}
filter {}

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

if not isUWP then
------------------------------------------------------- Editor Project -----------------------------------------------------

group "Apps"
project "Havana"
    kind "ConsoleApp"
    filter "action:vs*"
        systemversion "10.0.14393.0"
    filter {}
    language "C++"
    targetdir ((dirPrefix) .. "Build/%{cfg.buildcfg}")
    location "../Modules/Havana"
	
    --flags { "FatalWarnings" }
    includedirs {
      "../Modules/Havana/Source/",
        "."
    }
          
    sysincludedirs { "../Modules/Havana/Source/" }
    files {
      "../Modules/Havana/Source/**.*"
    }
    links { "ImGui" }
    vpaths {
      ["Source"] = "../Source/**.*",
      ["Source"] = "../Source/*.*"
    }
      libdirs {
          "../../Build/%{cfg.buildcfg}"
      }

    macOSEntryPoints()
    postBuildCopyCommands("..\\..\\..\\")
    
    if not isPlatform("macOS") then
        filter "not *Editor"
        kind "None"
        filter {}
    end

	filter { "action:vs*" }
	links {
		getPlatformPostfix(ProjectName) .. ".lib"
	}
    debugdir "$(SolutionDir)"
	filter {}
        
      links {
        (getPlatformPostfix("MitchEngine"))
      }
      dependson {
        getPlatformPostfix("MitchEngine")
      }

  configuration "*Editor"
      links {
        (getPlatformPostfix(ProjectName))
      }
      dependson {
        getPlatformPostfix(ProjectName)
      }
      sysincludedirs{
          "../Modules/Havana/Source/",
      }

end
  configuration {}
------------------------------------------------------- ImGui Project ------------------------------------------------------

group "Engine/ThirdParty"
project ("ImGui")
kind "StaticLib"
--if (isUWP) then
--	system "windowsuniversal"
--	consumewinrtextension "true"
--ends
if isPlatform("Win64") then
files {
    "../ThirdParty/ImGui/**/*win32.h",
    "../ThirdParty/ImGui/**/*win32.cpp",
    "../ThirdParty/ImGui/**/*dx11.h",
    "../ThirdParty/ImGui/**/*dx11.cpp",
}
end

filter "action:vs*"
systemversion "10.0.14393.0"
filter {}
filter "system:macosx"
excludes {
    "../ThirdParty/ImGui/**/*win32.h",
    "../ThirdParty/ImGui/**/*win32.cpp",
    "../ThirdParty/ImGui/**/*dx11.h",
    "../ThirdParty/ImGui/**/*dx11.cpp",
 }
 files {
     "../ThirdParty/ImGui/**/*osx.*",
     --"../ThirdParty/ImGui/**/*metal.*",
 }
sysincludedirs {
    "../ThirdParty/SDL/include"
}
filter { }

language "C++"
targetdir "../Build/%{cfg.buildcfg}"
location "../Modules/ImGUI"
removeincludedirs "*"
removelinks "*"
includedirs {
	"../ThirdParty/ImGui",
    "../ThirdParty/ImGui/backends",
}
files {
  "../Modules/ImGUI/Source/**.*",
  "../ThirdParty/ImGui/*.h",
  "../ThirdParty/ImGui/*.cpp",
  "../ThirdParty/ImGui/**/*sdl.h",
  "../ThirdParty/ImGui/**/*sdl.cpp",
  "../ThirdParty/ImGui/**/imgui_stdlib.*"
}

vpaths {
  ["ImGUI"] = "../ThirdParty/ImGui/**.*",
  ["Source"] = "../Modules/ImGUI/Source/*.*"
}

------------------------------------------------------- Utility Project ------------------------------------------------------

group "Engine/Modules"

project ("Dementia")
kind "StaticLib"
--if (isUWP) then
--	system "windowsuniversal"
--	consumewinrtextension "true"
--end
language "C++"
targetdir "../Build/%{cfg.buildcfg}"
location "../Modules/Dementia"
removeincludedirs "*"
removelinks "*"

filter "action:vs*"
systemversion "10.0.14393.0"
filter {}

filter "system:macosx"
excludes {
 }
sysincludedirs {
  "../ThirdParty/glm",
  "../ThirdParty/bgfx/include",
  "../ThirdParty/bx/include",
}
filter { }

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
--flags { "FatalWarnings" }
dependson {
  "ImGui"
}

filter {}

if(_OPTIONS["project-name"]) then
    postbuildcommands {
        "xcopy /y /d  \"$(ProjectDir)..\\..\\ThirdParty\\RenderDoc\\renderdoc.dll\" \"$(ProjectDir)..\\$(OutDir)\""
    }
else
    postbuildcommands {
        "xcopy /y /d  \"$(ProjectDir)..\\..\\ThirdParty\\RenderDoc\\renderdoc.dll\" \"$(ProjectDir)$(OutDir)\""
    }
end

------------------------------------------------------- Engine Project -------------------------------------------------------

group "Engine"
project (getPlatformPostfix("MitchEngine"))

kind "StaticLib"
if (isUWP) then
  system "windowsuniversal"
  consumewinrtextension "true"
end
language "C++"
targetdir "../Build/%{cfg.buildcfg}"
location "../"

filter { "action:xcode*" }
pchheader "Source/PCH.h"
filter { "action:vs*" }
pchheader "PCH.h"
pchsource "../Source/PCH.cpp"
systemversion "10.0.14393.0"
filter { }

--flags { "FatalWarnings" }
files {
  "../Source/**.h",
  "../Source/**.cpp",
  "../Source/**.txt",
  "../Tools/*.lua",
  "../Assets/Shaders/**.frag",
  "../Assets/Shaders/**.vert",
  "../Assets/Shaders/**.comp",
  "../Assets/Shaders/**.var",
}


dependson {
  getPlatformPostfix("Moonlight")
}

includedirs {
	"../Modules/Havana/Source"
}

links {
  (getPlatformPostfix("Moonlight") .. ".lib")
}

vpaths {
  ["Build"] = "../Tools/*.lua",
  ["Source"] = "Source/**.*",
  ["Shaders"] = "Assets/Shaders/**.frag",
  ["Shaders"] = "Assets/Shaders/**.vert",
  ["Shaders"] = "Assets/Shaders/**.comp",
  ["Shaders"] = "Assets/Shaders/**.var",
}

configuration "with-renderdoc"
defines { "MAN_ENABLE_RENDERDOC" }--, "__cplusplus_winrt"
postbuildcommands {
  "xcopy /y /d  \"C:\\Program Files\\RenderDoc\\renderdoc.dll\" \"$(ProjectDir)$(OutDir)\""
}

filter "configurations:Debug"
postbuildcommands {
}

filter "configurations:Release"
postbuildcommands {
}

filter { "files:**.hlsl" }
  flags {"ExcludeFromBuild"}


filter {}

group "Apps"
function GenerateGameSolution()
project (getPlatformPostfix(ProjectName .. "_EntryPoint"))
	if isPlatform("UWP") then
		kind "WindowedApp"
	end
	if isPlatform("Win64") or isPlatform("macOS") then
		kind "ConsoleApp"
	end
	if (isUWP) then
		system "windowsuniversal"
		consumewinrtextension "true"
		certificatefile (CertificateFile)
		certificatethumbprint (CertificateThumbprint)
		defaultlanguage "en-US"
		
		links {
			"D3D12.lib",
			"d2d1.lib",
			"d3d11.lib",
			"dxgi.lib",
			"windowscodecs.lib",
			"dwrite.lib",
			"D3D12.lib"
		}
	end

    macOSEntryPoints()
    postBuildCopyCommands("..\\")

    debugdir "$(OutDir)"
    
    if not isPlatform("macOS") then
        filter "*Editor"
        kind "None"
        filter {}
    end

	filter { "action:vs*" }
		systemversion "10.0.14393.0"
        links {
          (getPlatformPostfix(ProjectName) .. ".lib")
        }
	filter {}

    language "C++"
    targetdir "Build/%{cfg.buildcfg}"
    location "Game_EntryPoint"
    libdirs {
      "Build/%{cfg.buildcfg}"
    }
    dependson {
      getPlatformPostfix(ProjectName)
    }
    files {
      "Game_EntryPoint/Assets/**.png",
      "Game_EntryPoint/**.h",
      "Game_EntryPoint/**.cpp",
      "Game_EntryPoint/**.pfx",
      "Game_EntryPoint/**.appxmanifest"
    }

    includedirs {
      "Game/Source",
      "."
    }
    
    filter { "files:Assets/*.png" }
    deploy "true"
	
 ---------------------------- Game Project ------------------------------
group "Game"
project ((getPlatformPostfix(ProjectName)))

    kind "StaticLib"

  
filter { "action:vs*" }
  systemversion "10.0.14393.0"
  links {
    (getPlatformPostfix("MitchEngine")) .. ".lib",
	"Dementia.lib",
	"ImGui.lib",
  }

filter {}
    postBuildCopyCommands("..\\")
  
  language "C++"
  targetdir "Build/%{cfg.buildcfg}"
  location "Game"
  includedirs {
    "Game/Source"
  }
  dependson {
    getPlatformPostfix("MitchEngine")
  }
  files {
    "Game/**.h",
    "Game/**.cpp",
    "Assets/Shaders/**.frag",
    "Assets/Shaders/**.vert",
    "Assets/Shaders/**.comp",
    "Assets/Shaders/**.var",
	}
  vpaths {
	["Shaders"] = "Assets/Shaders/**.frag",
	["Shaders"] = "Assets/Shaders/**.vert",
	["Shaders"] = "Assets/Shaders/**.comp",
	["Shaders"] = "Assets/Shaders/**.var",
  }
  
  dependson {
    "ImGui"
  }

  filter { "files:**.hlsl" }
    flags {"ExcludeFromBuild"}
  filter {}

  if (isUWP) then
    excludes {
      "Game/Source/main.cpp"
    }
  end

  includedirs {
    "Game/Source",
    "."
  }
      
    sysincludedirs { "Game/Source" }
    
  configuration "*Editor" 
    kind "StaticLib"
	links {
	"ImGui.lib"
	}
end
