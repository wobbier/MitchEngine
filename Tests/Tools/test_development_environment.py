"""Exercise the SDK entry points from independent, private game directories."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ENGINE = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("nix"), "Nix is required for SDK integration checks")
class DevelopmentEnvironmentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="mitch-sdk-test-")
        cls.root = Path(cls.temporary.name)
        cls.sdk = cls.root / "Installed SDK with spaces"
        shutil.copytree(ENGINE / "nix", cls.sdk / "nix")
        (cls.sdk / "Tools").mkdir()
        for name in ("DevShell.sh", "NixEnv.sh", "NixGdb.sh"):
            shutil.copy2(ENGINE / "Tools" / name, cls.sdk / "Tools" / name)
        cls.plain = cls.root / "Plain private game"
        cls.custom = cls.root / "Extended private game"
        cls.broken = cls.root / "Broken private game"
        cls.cwd = cls.root / "Separate runtime directory"
        cls.cwd.mkdir()
        for project in (cls.plain, cls.custom, cls.broken):
            project.mkdir()
            (project / "Game.sharpmake.cs").write_text("// Private game source\n")
        (cls.custom / "devshell.nix").write_text('''{ pkgs }: {
          packages = [ pkgs.bash ];
          runtimeLibraries = [ pkgs.zlib ];
          env = { GAME_PRIVATE_VALUE = "project-only"; };
          shellHook = ''export GAME_PRIVATE_HOOK="enabled"'';
        }''')
        (cls.broken / "devshell.nix").write_text('throw "broken project extension"')

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def run_sdk(self, tool, project, *arguments):
        return subprocess.run(
            [str(self.sdk / "Tools" / tool), "--project", str(project), *arguments],
            cwd=self.cwd, capture_output=True, text=True, timeout=180,
        )

    def test_default_sdk_without_project_nix_files(self):
        result = self.run_sdk("NixEnv.sh", self.plain, "python3", "-c", '''
import json, os
from pathlib import Path
print(json.dumps({
  "native": Path(os.environ["DOTNET_LINUX_NATIVE_DIR"], "nethost.h").is_file(),
  "icd": Path(os.environ["LAVAPIPE_ICD"]).is_file(),
  "project": os.environ["MITCH_PROJECT_ROOT"],
  "cwd": os.getcwd(),
  "private": os.environ.get("GAME_PRIVATE_VALUE"),
}))
''')
        self.assertEqual(result.returncode, 0, result.stderr)
        data = json.loads(result.stdout)
        self.assertTrue(data["native"])
        self.assertTrue(data["icd"])
        self.assertEqual(data["project"], str(self.plain))
        self.assertEqual(data["cwd"], str(self.cwd))
        self.assertIsNone(data["private"])

    def test_private_packages_environment_and_hook(self):
        result = self.run_sdk("NixEnv.sh", self.custom, "bash", "-c", '''
test -f "$DOTNET_LINUX_NATIVE_DIR/libnethost.so" || exit 1
case "$LD_LIBRARY_PATH" in *zlib*) ;; *) exit 2 ;; esac
printf '%s|%s|%s' "$GAME_PRIVATE_VALUE" "$GAME_PRIVATE_HOOK" "$1"
''', "argument-zero", "argument with spaces ' and $literal")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout,
                         "project-only|enabled|argument with spaces ' and $literal")

    def test_command_exit_status_is_preserved(self):
        result = self.run_sdk("DevShell.sh", self.plain, "--command", "bash", "-c", "exit 17")
        self.assertEqual(result.returncode, 17, result.stderr)

    def test_failed_environment_does_not_execute_command(self):
        result = self.run_sdk("NixEnv.sh", self.broken, "printf", "SHOULD_NOT_RUN")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("SHOULD_NOT_RUN", result.stdout)
        self.assertIn("broken project extension", result.stderr)

    def test_debugger_protocol_has_no_shell_banner(self):
        # VS Code may prepend its MI flags to the configured debugger arguments.
        result = subprocess.run(
            [str(self.sdk / "Tools/NixGdb.sh"), "--interpreter=mi2",
             "--project", str(self.custom), "--batch", "-ex", "show version"],
            cwd=self.cwd, capture_output=True, text=True, timeout=180,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.startswith("=thread-group-added"), result.stdout)
        self.assertNotIn("development environment ready", result.stdout)


if __name__ == "__main__":
    unittest.main()
