-- premake5.lua
workspace "MitchEngine"
	configurations { "Debug", "Release" }
	platforms { "Win32", "Win64" }
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
		"../ThirdParty/STB"
	}
	libdirs {
		"../ThirdParty/GLFW/src/%{cfg.buildcfg}",
		"../ThirdParty/GLFW/src/src/%{cfg.buildcfg}",
		"../ThirdParty/Bullet/lib/%{cfg.buildcfg}"
	}

	links {
		"glfw3",
		"opengl32"
	}

	filter "configurations:Debug"
	defines { "DEBUG" }
	symbols "On"
	links {
		"BulletDynamics_Debug",
		"BulletCollision_Debug",
		"LinearMath_Debug"
	}

	filter "configurations:Release"
	defines { "NDEBUG" }
	optimize "On"
	links {
		"BulletDynamics",
		"BulletCollision",
		"LinearMath"
	}
	filter {}

	filter "platforms:Win32"
		architecture "x32"
	
	filter "platforms:Win64"
		architecture "x64"

	filter {}

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
	postbuildcommands {
		"xcopy /y /d  \"..\\ThirdParty\\AssIMP\\bin\\%{cfg.buildcfg}\\*.dll\" \"$(ProjectDir)$(OutDir)\"",
		"xcopy /y /d  \"..\\ThirdParty\\GLFW\\src\\%{cfg.buildcfg}\\*.dll\" \"$(ProjectDir)$(OutDir)\""
	}

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
	includedirs {
		"../MitchGame/Source",
		"."
	}

group "ThirdParty"
externalproject "glfw"
	location "../ThirdParty/GLFW/src"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802C"
	kind "SharedLib"
	language "C++"
	toolset "v141"
	targetdir "../Build/%{cfg.buildcfg}"
	
group "ThirdParty/Bullet"
externalproject "LibBulletCollision"
	location "../ThirdParty/Bullet/src/BulletCollision"
	filename "BulletCollision"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802D"
	kind "StaticLib"
	language "C++"
	toolset "v141"
	targetdir "../Build/%{cfg.buildcfg}"

externalproject "LibLinearMath"
	location "../ThirdParty/Bullet/src/LinearMath"
	filename "LinearMath"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802E"
	kind "StaticLib"
	language "C++"
	toolset "v141"
	targetdir "../Build/%{cfg.buildcfg}"

externalproject "LibBulletDynamics"
	location "../ThirdParty/Bullet/src/BulletDynamics"
	filename "BulletDynamics"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802F"
	kind "StaticLib"
	language "C++"
	toolset "v141"
	targetdir "../Build/%{cfg.buildcfg}"
	
group "ThirdParty/Assimp"
externalproject "LibAssIMP"
	location "../ThirdParty/AssIMP/code"
	filename "Assimp"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D58021"
	kind "SharedLib"
	language "C++"
	toolset "v141"
	targetdir "../Build/%{cfg.buildcfg}"
externalproject "LibAssimpIrrXML"
	location "../ThirdParty/AssIMP/contrib/irrXML"
	filename "irrXML"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D58022"
	kind "SharedLib"
	language "C++"
	toolset "v141"
	targetdir "../Build/%{cfg.buildcfg}"
externalproject "LibAssimpZLibStatic"
	location "../ThirdParty/AssIMP/contrib/zlib"
	filename "zlibstatic"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D58023"
	kind "SharedLib"
	language "C++"
	toolset "v141"
	targetdir "../Build/%{cfg.buildcfg}"