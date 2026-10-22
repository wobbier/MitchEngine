{
  description = "MitchEngine Linux development environment";

  inputs = {
    # Keep this pin in the engine SDK. Updating an engine selects its toolchain.
    # GPU drivers still come from the host, so test graphics when updating it.
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { nixpkgs, flake-utils, ... }:
    let
      mkDevShell = { system, projectConfig ? ({ pkgs }: { }) }:
        let
          pkgs = import nixpkgs {
            inherit system;
            config.allowUnfree = true;
          };
        in
        import ./mkDevShell.nix {
          inherit pkgs;
          config = projectConfig { inherit pkgs; };
        };
    in
    (flake-utils.lib.eachSystem [ "x86_64-linux" ] (system: {
      devShells.default = mkDevShell { inherit system; };
    })) // { lib.mkDevShell = mkDevShell; };
}
