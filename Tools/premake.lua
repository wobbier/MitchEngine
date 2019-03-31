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

if isUWP then
  startproject (getPlatformPostfix(ProjectName .. "_EntryPoint"))
else
  startproject (getPlatformPostfix(ProjectName))
  links {
    "Havana"
  }
end

location (dirPrefix)
includedirs {
  "../MitchEngine/",
  "../MitchEngine/Source",
  "../ThirdParty/Bullet/src",
  "../ThirdParty/GLM/glm",
  "C:/Program Files/RenderDoc",
  "../ThirdParty/Brofiler/BrofilerCore",
  "../Modules/Moonlight/Source",
  "../Modules/Dementia/Source",
  "../Modules/Havana/Source",
  "../ThirdParty/Assimp/include",
  "../ThirdParty/ImGUI",
  "../ThirdParty/PerlinNoise"
}

if isUWP then
  defines { "ME_PLATFORM_UWP" }
else
  defines { "ME_PLATFORM_WIN64" }
end

libdirs {
  "../Build/%{cfg.buildcfg}"
}

links {
  "BrofilerCore",
  getPlatformPostfix("Dementia"),
  "assimp-vc140-mt",
  "IrrXML"
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

filter "configurations:*Editor"
defines {
	"ME_EDITOR=1"
}

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
  libdirs {
    "../ThirdParty/Lib/Bullet/Win64/Debug",
    "../ThirdParty/Lib/Brofiler/UWP/Debug"
  }
else
  libdirs {
    "../ThirdParty/Lib/Bullet/Win64/Debug",
    "../ThirdParty/Lib/GLFW/Win64/Debug",
    "../ThirdParty/Lib/Brofiler/Win64/Debug"
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
  libdirs {
    "../ThirdParty/Lib/Bullet/Win64/Release",
    "../ThirdParty/Lib/Brofiler/UWP/Release"
  }
else
  libdirs {
    "../ThirdParty/Lib/Bullet/Win64/Release",
    "../ThirdParty/Lib/GLFW/Win64/Release",
    "../ThirdParty/Lib/Brofiler/Win64/Release"
  }
end

------------------------------------------------------- Renderer Project -----------------------------------------------------

group "Engine/Modules"
project (getPlatformPostfix("Moonlight"))
kind "StaticLib"
if (isUWP) then
  system "windowsuniversal"
  consumewinrtextension "true"
  --nuget { "directxtk_uwp:2018.11.20.1" }
  includedirs { (dirPrefix) .. "packages/directxtk_uwp.2018.11.20.1/include" }
else
  --nuget { "directxtk_desktop_2015:2018.11.20.1" }
  includedirs { (dirPrefix) .. "packages/directxtk_desktop_2015.2018.11.20.1/include" }
end
links { "DirectXTK" }
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

if not isUWP then
------------------------------------------------------- Editor Project -----------------------------------------------------

group "Engine/Modules"
project (getPlatformPostfix("Havana"))
    kind "StaticLib"
  systemversion "10.0.14393.0"
if (isUWP) then
  system "windowsuniversal"
  consumewinrtextension "true"
end
language "C++"
targetdir "../Build/%{cfg.buildcfg}"
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

  removelinks {
  	"MitchEngine",
	"Havana"
  }

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
location "../ThirdParty/ImGUI"
removeincludedirs "*"
removelinks "*"
includedirs {
	"../ThirdParty/ImGUI",
	"../ThirdParty/ImGUI/examples"
}
files {
  "../ThirdParty/ImGUI/*.h",
  "../ThirdParty/ImGUI/*.cpp",
  "../ThirdParty/ImGUI/**/*win32.h",
  "../ThirdParty/ImGUI/**/*win32.cpp",
  "../ThirdParty/ImGUI/**/*dx11.*"
}

end

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

if withRenderdoc then
  defines { "ME_ENABLE_RENDERDOC", "__cplusplus_winrt" }
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

includedirs {
	"../Modules/Havana/Source"
}

links {
  (getPlatformPostfix("Moonlight") .. ".lib")
}

vpaths {
  ["Build"] = "../Tools/*.lua",
  ["Source"] = "../Source/**.*"
}
if isUWP then
  postbuildcommands {
    "fxc /T ps_5_0 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\Assets\\Shaders\\SimplePixelShader.cso ..\\Assets\\Shaders\\SimplePixelShader.hlsl",
    "fxc /T vs_5_0 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\Assets\\Shaders\\SimpleVertexShader.cso ..\\Assets\\Shaders\\SimpleVertexShader.hlsl",
    "fxc /T vs_5_0 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\Assets\\Shaders\\GridVertexShader.cso ..\\Assets\\Shaders\\GridVertexShader.hlsl",
    "fxc /T ps_5_0 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\Assets\\Shaders\\GridPixelShader.cso ..\\Assets\\Shaders\\GridPixelShader.hlsl",
    "fxc /T vs_5_0 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\Assets\\Shaders\\DepthVertexShader.cso ..\\Assets\\Shaders\\DepthVertexShader.hlsl",
    "fxc /T ps_5_0 /Fo ..\\Build\\%{cfg.buildcfg}\\AppX\\Assets\\Shaders\\DepthPixelShader.cso ..\\Assets\\Shaders\\DepthPixelShader.hlsl"
  }
else
  excludes {
    "../MitchEngine/**/Graphics/Common/*.*",
    "../MitchEngine/**/Graphics/Content/*.*"
  }
  links {
    (getPlatformPostfix("Havana") .. ".lib")
  }
  postbuildcommands {
    "fxc /T ps_5_0 /Fo ..\\Assets\\Shaders\\SimplePixelShader.cso ..\\Assets\\Shaders\\SimplePixelShader.hlsl",
    "fxc /T vs_5_0 /Fo ..\\Assets\\Shaders\\SimpleVertexShader.cso ..\\Assets\\Shaders\\SimpleVertexShader.hlsl",
    "fxc /T vs_5_0 /Fo ..\\Assets\\Shaders\\GridVertexShader.cso ..\\Assets\\Shaders\\GridVertexShader.hlsl",
    "fxc /T ps_5_0 /Fo ..\\Assets\\Shaders\\GridPixelShader.cso ..\\Assets\\Shaders\\GridPixelShader.hlsl",
    "fxc /T vs_5_0 /Fo ..\\Assets\\Shaders\\DepthVertexShader.cso ..\\Assets\\Shaders\\DepthVertexShader.hlsl",
    "fxc /T ps_5_0 /Fo ..\\Assets\\Shaders\\DepthPixelShader.cso ..\\Assets\\Shaders\\DepthPixelShader.hlsl"
  }
end
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

filter {}

group "App"
if not _OPTIONS["project-name"] then
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

  if (isUWP) then
    project (getPlatformPostfix("MitchGame_EntryPoint"))
    kind "ConsoleApp"
    system "windowsuniversal"
    consumewinrtextension "true"
    systemversion "10.0.14393.0"
    certificatefile (CertificateFile)
    certificatethumbprint (CertificateThumbprint)
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
end

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
    (getPlatformPostfix("MitchEngine") .. ".lib")
  }
  dependson {
    getPlatformPostfix("MitchEngine")
  }
  files {
    "Game/**.h",
    "Game/**.cpp"
  }
  if (isUWP) then
    excludes {
      "Game/Source/main.cpp"
    }
  end

  includedirs {
    "Game/Source",
    "."
  }
end
