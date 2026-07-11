{
  description = "MitchEngine project dev environment";

  inputs = {
    # Track a recent nixpkgs channel: graphics is impure (apps load the
    # driver from the host's /run/opengl-driver on NixOS). A skew between this
    # and the host means the dev-shell glibc/wayland can't load the host Mesa
    # ICDs -> "Vulkan doesn't implement VK_KHR_surface".
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
          config.allowUnfree = true;
        };
        dotnet = pkgs.dotnet-sdk_8;
      in
      {
        # ----------------------------------------------------------
        # MitchEngine Dev Shell
        #
        # - bgfx loads GL/EGL dynamically with dlopen()
        # - Mesa + libglvnd MUST be in LD_LIBRARY_PATH so bgfx finds
        #   64-bit libEGL.so.1 instead of Steam's 32-bit version.
        # - ALWAYS enter this shell with:  nix develop
        # ----------------------------------------------------------
        devShells.default = pkgs.mkShell {
          name = "mitchengine-devshell";

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

            python3

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
            alsa-lib
            curl
            # Optional Vulkan support for bgfx
            vulkan-loader

            # Required by Ultralight AppCore on Linux
            fontconfig

            # Ultralight (latest) links against the system GTK3 stack
            gtk3
            glib
            pango
            cairo
            atk
            gdk-pixbuf
            harfbuzz
          ];

          # Provide GL/EGL so bgfx's dlopen("libEGL.so.1") succeeds.
          # makeLibraryPath only adds each listed package's own lib dir
          # (not transitive deps), so every Ultralight/GTK dependency is
          # listed explicitly.
          LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath [
            pkgs.libglvnd
            pkgs.mesa
            pkgs.SDL2
            pkgs.vulkan-loader

            # X11/XCB runtime libs. These are in buildInputs for compiling, but
            # makeLibraryPath is non-transitive and SDL2 needs libX11 at load
            # time, so they must be listed here too.
            pkgs.xorg.libX11
            pkgs.xorg.libXcursor
            pkgs.xorg.libXext
            pkgs.xorg.libXrandr
            pkgs.xorg.libXinerama
            pkgs.xorg.libXxf86vm
            pkgs.xorg.libXfixes
            pkgs.xorg.libxcb

            # Wayland EGL backend (libwayland-egl.so.1) — in buildInputs for
            # compiling, but needed at load time too now that the binary links it.
            pkgs.wayland

            # Ultralight (latest) GTK3 runtime deps
            pkgs.gtk3
            pkgs.glib
            pkgs.pango
            pkgs.cairo
            pkgs.atk
            pkgs.gdk-pixbuf
            pkgs.harfbuzz
            pkgs.fontconfig  # libfontconfig.so.1 (Ultralight font rendering)
            pkgs.bzip2  # libbz2.so.1.0

            pkgs.alsa-lib
            pkgs.curl
          ];

          shellHook = ''
            export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$(pwd)/Engine/ThirdParty/Optick:$(pwd)/Engine/ThirdParty/FMOD/api/core/lib/x86_64:$(pwd)/Engine/ThirdParty/fmod/api/core/lib/x86_64"
            export DOTNET_ROOT="${dotnet}/share/dotnet"

            _DOTNET_NATIVE=$(find ${dotnet}/share/dotnet/packs/Microsoft.NETCore.App.Host.linux-x64 -mindepth 3 -maxdepth 3 -name "native" -type d 2>/dev/null | head -1)
            if [ -n "$_DOTNET_NATIVE" ]; then
              export DOTNET_LINUX_NATIVE_DIR="$_DOTNET_NATIVE"
            fi
            unset _DOTNET_NATIVE

            echo "🍉 Shell Ready!"
            echo "  Generate: (cd Project && sh GenerateSolution.sh)"
            echo "  Build:    make -f <Project>.make Havana config=editor_debug -j\$(nproc)"
            echo "  Run:      ./.build/Editor_Debug/Havana"
            echo "  Optional: SDL_VIDEODRIVER=wayland ..."
          '';
        };
      });
}
