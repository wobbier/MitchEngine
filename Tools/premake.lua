-- premake5.lua
workspace "MitchEngine"
   configurations { "Debug", "Release" }
   location "../"
   includedirs {
   "../MitchEngine/Source",
   "../ThirdParty/GLAD/include/",
   "../ThirdParty/GLAD/src/",
   "../ThirdParty/GLM/glm",
   "../ThirdParty/GLFW/include",
   "../ThirdParty/SOIL/src",
   "../ThirdParty/Boost/boost-1.64.0"
   }
   libdirs {
	"../ThirdParty/SOIL/**/%{cfg.buildcfg}",
	"../ThirdParty/GLFW/lib-vc2015",
	"../ThirdParty/GLFW/**/%{cfg.buildcfg}",
	"../ThirdParty/JsonCPP/**/%{cfg.buildcfg}"
   }

   links {
   	   "SOIL",
	   "glfw3",
	   "opengl32"
   }

project "MitchEngine"
   kind "StaticLib"
   language "C++"
   targetdir "../Build/%{cfg.buildcfg}"
   location "../MitchEngine"
	files {
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
externalproject "SOIL"
   location "../ThirdParty/SOIL/projects/VC9"
   uuid "57940020-8E99-AEB6-271F-61E0F7F6B73C"
   kind "StaticLib"
   language "C++"
   toolset "v141"
   targetdir "../Build/%{cfg.buildcfg}"

externalproject "glfw"
   location "../ThirdParty/GLFW/src"
   uuid "57940020-8E99-AEB6-271F-61E0F7F6B73E"
   kind "StaticLib"
   language "C++"
   toolset "v141"
   targetdir "../Build/%{cfg.buildcfg}"