# The SDK launcher evaluates the project's private extension outside the flake.
# Only the SDK's small nix/ directory is a flake input; game sources stay local.
{ engineRoot, projectRoot, system ? builtins.currentSystem }:
let
  # Import only these SDK files, with a stable store name. Local installation
  # paths can contain spaces or URL-significant characters such as # and ?.
  sdkNix = builtins.path {
    path = builtins.toPath "${engineRoot}/nix";
    name = "mitchengine-nix";
  };
  engine = builtins.getFlake (builtins.unsafeDiscardStringContext (toString sdkNix));
  extension = builtins.toPath "${projectRoot}/devshell.nix";
in
engine.lib.mkDevShell {
  inherit system;
  projectConfig =
    if builtins.pathExists extension then import extension else ({ pkgs }: { });
}
