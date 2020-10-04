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

newoption {
  trigger     = "project-name",
  description = "Notifies premake we're generating a game solution."
}

-- Modify these in your game premake --
CertificateFile = "";
CertificateThumbprint = "";
---------------------------------------

function getPlatformPostfix(thing)
  if (_OPTIONS["uwp"]) then
    return (thing .. "UWP")
  end
  return thing
end

isUWP = _OPTIONS["uwp"]
withRenderdoc = _OPTIONS["with-renderdoc"]
withDirectX = _OPTIONS["gfxapi"] == "directx" or isUWP
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
else
configurations { "Debug", "Release", "Debug Editor", "Release Editor" }
end
platforms { "x64" }
cppdialect "C++17"
if isUWP then
  startproject (getPlatformPostfix(ProjectName .. "_EntryPoint"))
else
  startproject (getPlatformPostfix(ProjectName))
end

location (dirPrefix)
includedirs {
  "../Source",
  "../ThirdParty/Bullet/src",
  "C:/Program Files/RenderDoc",
  "../ThirdParty/Optick/src",
  "../Modules/Moonlight/Source",
  "../Modules/Dementia/Source",
  "../Modules/Havana/Source",
  "../ThirdParty/Assimp/include",
  "../ThirdParty/ImGuiTables",
  "../Modules/ImGUI/Source",
  "../ThirdParty/PerlinNoise",
  "../ThirdParty/UltralightSDK/include",
  "../ThirdParty/JSON/single_include"
}


if isUWP then
  defines { "ME_PLATFORM_UWP" }
  includedirs { (dirPrefix) .. "packages/directxtk_uwp.2018.11.20.1/include" }
  libdirs {
  "../ThirdParty/UltralightSDK/lib/UWP",
  "../ThirdParty/UltralightSDK/bin/UWP"
  }
else
  includedirs { (dirPrefix) .. "packages/directxtk_desktop_2015.2018.11.20.1/include" }
  libdirs {
  "../ThirdParty/UltralightSDK/lib/Win64",
  "../ThirdParty/UltralightSDK/bin/Win64"
  }
  defines { "ME_PLATFORM_WIN64" }
  links {
  	"dwmapi",
    "DirectXTKAudioWin8",
  "Shlwapi.lib",
  "AppCore.lib"
  }
end

libdirs {
  "../Build/%{cfg.buildcfg}",
}

links {
  "OptickCore.lib",
  (getPlatformPostfix("Dementia") .. ".lib"),
  "assimp-vc140-mt",
  "IrrXML",
  "DirectXTK",
  "Usp10.lib",
  "Ultralight.lib",
  "UltralightCore.lib",
  "WebCore.lib"
}

-- Platform specific options
if withDirectX then
  defines { "ME_DIRECTX" }
  links {
    "d2d1", "d3d11", "dxgi", "windowscodecs", "dwrite", "D3DCompiler"
  }
end

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
  "BulletDynamics_Debug",
  "BulletCollision_Debug",
  "LinearMath_Debug",
  "zlibstaticd"
}
libdirs {
  "../ThirdParty/Lib/Assimp/Debug"
}

if isUWP then
  defines {"USE_OPTICK=0"}
  libdirs {
    "../ThirdParty/Lib/Bullet/Win64/Debug",
    "../ThirdParty/Lib/Optick/UWP/Debug"
  }
else
  libdirs {
    "../ThirdParty/Lib/Bullet/Win64/Debug",
    "../ThirdParty/Lib/GLFW/Win64/Debug",
    "../ThirdParty/Lib/Optick/Win64/Debug"
  }
end

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
  "LinearMath_MinsizeRel",
  "zlibstatic"
}
libdirs {
  "../ThirdParty/Lib/Assimp/Release"
}

if isUWP then
  defines {"USE_OPTICK=0"}
  libdirs {
    "../ThirdParty/Lib/Bullet/Win64/Release",
    "../ThirdParty/Lib/Optick/UWP/Release"
  }
else
  libdirs {
    "../ThirdParty/Lib/Bullet/Win64/Release",
    "../ThirdParty/Lib/GLFW/Win64/Release",
    "../ThirdParty/Lib/Optick/Win64/Release"
  }
end

configuration "Debug*"
if (isUWP) then
  libdirs { (dirPrefix) .. "packages/directxtk_uwp.2018.11.20.1/lib/x64/Debug" }
else
  libdirs { (dirPrefix) .. "packages/directxtk_desktop_2015.2018.11.20.1/lib/x64/Debug" }
end

configuration "Release*"
if (isUWP) then
  libdirs { (dirPrefix) .. "packages/directxtk_uwp.2018.11.20.1/lib/x64/Release" }
else
  libdirs { (dirPrefix) .. "packages/directxtk_desktop_2015.2018.11.20.1/lib/x64/Release" }
end
configuration {}
filter {}

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
dependson { getPlatformPostfix("Dementia") }
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

group "Editor"
project "Havana"
    kind "ConsoleApp"
  systemversion "10.0.14393.0"
language "C++"
targetdir ((dirPrefix) .. "Build/%{cfg.buildcfg}")
location "../Modules/Havana"
includedirs {
  "../Modules/Havana/Source/",
    "."
}
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

  links {
    (getPlatformPostfix("MitchEngine") .. ".lib")
  }
  dependson {
    getPlatformPostfix("MitchEngine")
  }

  configuration "not *Editor"
    kind "StaticLib"
	
  configuration "*Editor"
  links {
    (getPlatformPostfix(ProjectName) .. ".lib")
  }
  dependson {
    getPlatformPostfix(ProjectName)
  }
  configuration {}
------------------------------------------------------- ImGui Project ------------------------------------------------------

group "Engine/ThirdParty"
project (getPlatformPostfix("ImGui"))
kind "StaticLib"
--if (isUWP) then
--	system "windowsuniversal"
--	consumewinrtextension "true"
--end
systemversion "10.0.14393.0"
language "C++"
targetdir "../Build/%{cfg.buildcfg}"
location "../Modules/ImGUI"
removeincludedirs "*"
removelinks "*"
includedirs {
	"../ThirdParty/ImGuiTables",
	"../ThirdParty/ImGuiTables/examples"
}
files {
  "../Modules/ImGUI/Source/**.*",
  "../ThirdParty/ImGuiTables/*.h",
  "../ThirdParty/ImGuiTables/*.cpp",
  "../ThirdParty/ImGuiTables/**/*win32.h",
  "../ThirdParty/ImGuiTables/**/*win32.cpp",
  "../ThirdParty/ImGuiTables/**/*dx11.h",
  "../ThirdParty/ImGuiTables/**/*dx11.cpp",
  "../ThirdParty/ImGuiTables/**/imgui_stdlib.*"
}

vpaths {
  ["ImGUI"] = "../ThirdParty/ImGuiTables/**.*",
  ["Source"] = "../Modules/ImGUI/Source/*.*"
}

end

------------------------------------------------------- Utility Project ------------------------------------------------------

group "Engine/Modules"

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

if withRenderdoc then
  defines { "ME_ENABLE_RENDERDOC" } ---, "__cplusplus_winrt"
  postbuildcommands {
    "xcopy /y /d  \"C:\\Program Files\\RenderDoc\\renderdoc.dll\" \"$(ProjectDir)$(OutDir)\""
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
systemversion "10.0.14393.0"
language "C++"
targetdir "../Build/%{cfg.buildcfg}"
location "../"
pchheader "PCH.h"
pchsource "../Source/PCH.cpp"
files {
  "../Source/**.h",
  "../Source/**.cpp",
  "../Source/**.txt",
  "../Assets/**.hlsl",
  "../Tools/*.lua"
}

filter {}

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
  ["Shaders"] = "Assets/**.hlsl"
}

configuration "with-renderdoc"
defines { "MAN_ENABLE_RENDERDOC", "__cplusplus_winrt" }
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

group "App"
function GenerateGameSolution()
  if (isUWP) then
    project (getPlatformPostfix(ProjectName .. "_EntryPoint"))
    kind "ConsoleApp"
    system "windowsuniversal"
    consumewinrtextension "true"
    systemversion "10.0.14393.0"
    certificatefile (CertificateFile)
    certificatethumbprint (CertificateThumbprint)
    defaultlanguage "en-US"
    language "C++"
    targetdir "Build/%{cfg.buildcfg}"
    location "Game_EntryPoint"
    libdirs {
      "Build/%{cfg.buildcfg}"
    }
    links {
      (getPlatformPostfix(ProjectName) .. ".lib")
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
  end
  project ((getPlatformPostfix(ProjectName)))

  if (isUWP) then
    kind "StaticLib"
    system "windowsuniversal"
    consumewinrtextension "true"
  else
    kind "ConsoleApp"
  end
  systemversion "10.0.14393.0"
  language "C++"
  targetdir "Build/%{cfg.buildcfg}"
  location "Game"
  includedirs {
    "Game/Source"
  }
  links {
    (getPlatformPostfix("MitchEngine") .. ".lib"),
	(getPlatformPostfix("Dementia") .. ".lib")
  }
  dependson {
    getPlatformPostfix("MitchEngine")
  }
  files {
    "Game/**.h",
    "Game/**.cpp",
    "Assets/**.hlsl",
	}
  vpaths {
	["Shaders"] = "Assets/**.hlsl"
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
  
  configuration "*Editor" 
    kind "StaticLib"
	links {
	"ImGui.lib"
	}
if isUWP then
  configuration "Release Editor" 
  postbuildcommands {
    "xcopy /y /d  \"ThirdParty\\UltralightSDK\\bin\\UWP\\*.*\" \"..\\Build\\Release Editor\""
  }
  configuration "Release" 
  postbuildcommands {
    "xcopy /y /d  \"ThirdParty\\UltralightSDK\\bin\\UWP\\*.*\" \"..\\Build\\Release\""
  }
else
  configuration "Debug Editor" 
  postbuildcommands {
    "xcopy /y /d  \"ThirdParty\\UltralightSDK\\bin\\Win64\\*.*\" \"..\\Build\\Debug Editor\""
  }
  configuration "Debug" 
  postbuildcommands {
    "xcopy /y /d  \"ThirdParty\\UltralightSDK\\bin\\Win64\\*.*\" \"..\\Build\\Debug\""
  }
end
end
