-- premake5.lua
workspace "MitchEngine"
   configurations { "Debug", "Release" }
   location "../"
   includedirs {
   "../MitchEngine/Source",
   "../ThirdParty/GLM/glm",
   "../ThirdParty/GLFW/include",
   "../ThirdParty/SOIL/src",
   "../ThirdParty/GLEW/include",
   "../ThirdParty/Boost",
   "../ThirdParty/JsonCPP/include",
   "../ThirdParty/GLEW/auto/src"
   }
   libdirs {
	"../ThirdParty/SOIL/**/%{cfg.buildcfg}",
	"../ThirdParty/GLEW/lib/%{cfg.buildcfg}/Win32",
	"../ThirdParty/GLFW/lib-vc2015",
	"../ThirdParty/GLFW/**/%{cfg.buildcfg}",
	"../ThirdParty/JsonCPP/**/%{cfg.buildcfg}"
   }
   
   links {
   	   "SOIL",
	   "glfw3",
	   "opengl32",
	   "lib_json"
   }

   filter "configurations:Debug"
	   links {
		   "glew32d"
	   }
   
   filter "configurations:Release"
   links {
	   "glew32"
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
   files { "**.h", "**.c" }

   filter "configurations:Debug"
      defines { "DEBUG" }
      symbols "On"

   filter "configurations:Release"
      defines { "NDEBUG" }
      optimize "On"

group "ThirdParty"
externalproject "lib_json"
   location "../ThirdParty/JsonCPP/makefiles/msvc2010"
   uuid "57940020-8E99-AEB6-271F-61E0F7F6B73B"
   kind "StaticLib"
   language "C++"
   toolset "v141"
   targetdir "../Build/%{cfg.buildcfg}"
   
externalproject "SOIL"
   location "../ThirdParty/SOIL/projects/VC9"
   uuid "57940020-8E99-AEB6-271F-61E0F7F6B73C"
   kind "StaticLib"
   language "C++"
   toolset "v141"
   targetdir "../Build/%{cfg.buildcfg}"

--externalproject "glew_shared"
   --location "../ThirdParty/GLEW/build/vc15/"
   --uuid "57940020-8E99-AEB6-271F-61E0F7F6B737"
   --kind "StaticLib"
   --language "C"
   --toolset "v141"