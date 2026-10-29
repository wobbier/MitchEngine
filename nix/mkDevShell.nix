# Shared SDK environment. Projects return optional additions from devshell.nix.
{ pkgs, config ? { } }:
let
  dotnet = pkgs.dotnet-sdk_8;
in
pkgs.mkShell ((config.env or { }) // {
  name = "mitchengine-devshell";

  packages = with pkgs; [
    python3
    bear
    patchelf
    git
    xorg-server
    xvfb-run
    # Faster iteration: mold links in a fraction of ld.bfd's time; ccache reuses objects
    # across clean builds, regenerations and branch switches.
    mold
    ccache
  ] ++ (config.packages or [ ]);

  nativeBuildInputs = config.nativeBuildInputs or [ ];
  buildInputs = with pkgs; [
    # Core build tools
    gcc
    gdb
    gnumake
    cmake
    pkg-config

    # SDL2 from Nix
    SDL2

    # GL/EGL libs
    mesa        # GL + EGL + DRI drivers
    libglvnd    # GL/Vulkan dispatch loader

    # Wayland (wl_egl_window_create for native Wayland + Vulkan)
    wayland

    dotnet
    # X11 dev headers (needed by SDL_syswm.h and XWayland support)
    libX11
    libXcursor
    libXext
    libXrandr
    libXinerama
    libXxf86vm
    libXfixes
    libxcb

    bzip2  # For decompressing .tar.bz2 files from bgfx releases

    # libcurl headers/libs for Web::DownloadFile (HttpDownload.cpp)
    curl

    # Optional Vulkan support for bgfx
    vulkan-loader
  ] ++ (config.buildInputs or [ ]);

  # Provide GL/EGL so bgfx's dlopen("libEGL.so.1") succeeds.
  # makeLibraryPath only adds each listed package's own lib dir
  # (not transitive deps), so every runtime dependency is listed
  # explicitly.
  LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath ([
    pkgs.libglvnd
    pkgs.mesa
    pkgs.SDL2
    pkgs.vulkan-loader

    # X11/XCB runtime libs. These are in buildInputs for compiling, but
    # makeLibraryPath is non-transitive and SDL2 needs libX11 at link
    # and load time, so list them here too.
    pkgs.libX11
    pkgs.libXcursor
    pkgs.libXext
    pkgs.libXrandr
    pkgs.libXinerama
    pkgs.libXxf86vm
    pkgs.libXfixes
    pkgs.libxcb

    # Linked as -l:lib*.so (no Nix RUNPATH entry), so resolve them here
    pkgs.wayland
    pkgs.bzip2  # libbz2.so.1.0
    pkgs.curl
  ] ++ (config.runtimeLibraries or [ ]));

  # Pin the software renderer used by Headless.sh to the same SDK.
  LAVAPIPE_ICD = "${pkgs.mesa}/share/vulkan/icd.d/lvp_icd.x86_64.json";

  shellHook = ''
    export DOTNET_ROOT="${dotnet}/share/dotnet"
    export DOTNET_LINUX_NATIVE_DIR="$(find ${dotnet}/share/dotnet/packs/Microsoft.NETCore.App.Host.linux-x64 -path "*/runtimes/linux-x64/native" -type d -print -quit)"

    # SDK libraries belong to the live checkout/installation. Never
    # embed a game checkout path into the engine's flake source.
    if [ -z "''${MITCH_ENGINE_ROOT:-}" ] && [ -d "$PWD/Engine/Tools" ]; then
      export MITCH_ENGINE_ROOT="$PWD/Engine"
    fi
    if [ -n "''${MITCH_ENGINE_ROOT:-}" ]; then
      for sdk_lib in "$MITCH_ENGINE_ROOT/ThirdParty/Optick" \
                     "$MITCH_ENGINE_ROOT/ThirdParty/FMOD/api/core/lib/x86_64" \
                     "$MITCH_ENGINE_ROOT/ThirdParty/fmod/api/core/lib/x86_64"; do
        if [ -d "$sdk_lib" ]; then
          export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$sdk_lib"
        fi
      done
      unset sdk_lib
    fi
    # Link with mold. The generated makefiles append to LDFLAGS, so this reaches every link.
    case " ''${LDFLAGS:-} " in
      *" -fuse-ld=mold "*) ;;
      *) export LDFLAGS="''${LDFLAGS:+$LDFLAGS }-fuse-ld=mold" ;;
    esac

    # Compile through ccache (MITCH_NO_CCACHE=1 opts out). The makefiles name the compiler
    # (CXX = g++), so a directory of g++/gcc links to ccache goes first in PATH; ccache then
    # runs the real compiler found after it. The sloppiness lets PCH builds cache too.
    if [ -z "''${MITCH_NO_CCACHE:-}" ] && [ -n "''${MITCH_PROJECT_ROOT:-}" ]; then
      ccache_bin="$MITCH_PROJECT_ROOT/.tmp/ccache-bin"
      mkdir -p "$ccache_bin"
      for tool in gcc g++ cc c++; do
        ln -sf "${pkgs.ccache}/bin/ccache" "$ccache_bin/$tool"
      done
      case ":$PATH:" in
        *":$ccache_bin:"*) ;;
        *) export PATH="$ccache_bin:$PATH" ;;
      esac
      export CCACHE_BASEDIR="$MITCH_PROJECT_ROOT"
      export CCACHE_SLOPPINESS="pch_defines,time_macros,include_file_mtime,include_file_ctime"
      export CCACHE_MAXSIZE="''${CCACHE_MAXSIZE:-20G}"
      unset ccache_bin tool
    fi
    echo "MitchEngine development environment ready."
  '' + (config.shellHook or "");
})
