-- premake5.lua
workspace "MitchEngine"
   configurations { "Debug", "Release" }
   startproject "MitchGame"
   location "../"
   includedirs {
   "../MitchEngine/Source",
   "../ThirdParty/GLAD/include/",
   "../ThirdParty/GLAD/src/",
   "../ThirdParty/GLM/glm",
   "../ThirdParty/GLFW/include",
   "../ThirdParty/STB",
   "../ThirdParty/Boost/boost-1.64.0"
   }
   libdirs {
	"../ThirdParty/GLFW/src/%{cfg.buildcfg}"
   }

   links {
	   "glfw3",
	   "opengl32"
   }

project "MitchEngine"
   kind "StaticLib"
   language "C++"
   targetdir "../Build/%{cfg.buildcfg}"
   location "../MitchEngine"
	files {
			"../MitchEngine/Assets/**.*",
			"../MitchEngine/Source/**.h",
			"../MitchEngine/Source/**.cpp",
			"../MitchEngine/Source/**.txt",
			"../Tools/**.lua"
	}
	vpaths {
		["Build"] = "../Tools/*.lua"
	}
   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

project "MitchGame"
   kind "ConsoleApp"
   language "C++"
   targetdir "../Build/%{cfg.buildcfg}"
   location "../MitchGame"
   links "MitchEngine"
   files {
   "../MitchGame/Assets/**.frag",
   "../MitchGame/Assets/**.vert",
   "../MitchGame/**.h",
   "../MitchGame/**.cpp"
   }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

group "ThirdParty"
externalproject "glfw"
   location "../ThirdParty/GLFW/src"
   uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802C"
   kind "StaticLib"
   language "C++"
   toolset "v141"
   targetdir "../Build/%{cfg.buildcfg}"

externalproject "Bullet3Collision"
   location "../ThirdParty/Bullet/build3/vs2010"
   uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802D"
   kind "StaticLib"
   language "C++"
   toolset "v141"
   targetdir "../Build/%{cfg.buildcfg}"
