-- premake5.lua
workspace "MitchEngine"
	configurations { "Debug", "Release" }
	startproject "MitchGame"
	location "../"
	includedirs {
		"../MitchEngine/Source",
		"../ThirdParty/AssIMP/include",
		"../ThirdParty/Bullet/src",
		"../ThirdParty/GLAD/include/",
		"../ThirdParty/GLAD/src/",
		"../ThirdParty/GLM/glm",
		"../ThirdParty/GLFW/include",
		"../ThirdParty/STB",
		"../ThirdParty/Boost/boost-1.64.0"
	}
	libdirs {
		"../ThirdParty/GLFW/src/%{cfg.buildcfg}",
		"../ThirdParty/GLFW/src/src/%{cfg.buildcfg}",
		"../ThirdParty/Bullet/lib/%{cfg.buildcfg}"
	}

	links {
		"glfw3",
		"opengl32",
		"LibAssIMP"
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
	includedirs {
		"../MitchGame/Source"
	}
	vpaths {
		["Build"] = "../Tools/*.lua"
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

group "ThirdParty"
externalproject "glfw"
	location "../ThirdParty/GLFW/src"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D5802C"
	kind "StaticLib"
	language "C++"
	toolset "v141"
	targetdir "../Build/%{cfg.buildcfg}"

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

externalproject "LibAssIMP"
	location "../ThirdParty/AssIMP/code"
	filename "Assimp"
	uuid "8A0313E9-F6C0-4C24-9258-65C9F6D58021"
	kind "StaticLib"
	language "C++"
	toolset "v141"
	targetdir "../Build/%{cfg.buildcfg}"