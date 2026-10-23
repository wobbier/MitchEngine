"""CI bootstrap must preserve a supplied engine revision and report failures."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ENGINE = Path(__file__).resolve().parents[2]


class ProjectSetupTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="mitch setup ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.sdk = self.root / "Engine"
        self.sdk.mkdir()
        self.git(self.root, "init", "-q")
        (self.root / ".gitmodules").write_text("# Existing project settings\n")

    def git(self, directory, *arguments):
        return subprocess.check_output(
            ["git", "-C", str(directory), *arguments], text=True,
            stderr=subprocess.STDOUT,
        ).strip()

    def prepare_engine(self, *, gitfile=False, failure=0):
        arguments = ["init", "-q"]
        if gitfile:
            arguments += ["--separate-git-dir", str(self.root / "engine metadata")]
        self.git(self.sdk, *arguments)
        self.git(self.sdk, "-c", "user.name=CI Test", "-c", "user.email=ci@example.com",
                 "-c", "commit.gpgsign=false", "commit", "--allow-empty", "-qm", "Pinned engine")
        self.revision = self.git(self.sdk, "rev-parse", "HEAD")
        self.git(self.sdk, "checkout", "--detach", "-q", self.revision)
        template = self.sdk / "Tools/ProjectTemplate"
        (template / "Project").mkdir(parents=True)
        (template / "Game.sharpmake.cs").write_text('string name = "EmptyProject";\n')
        (template / "Project/GenerateSolution.command").write_text(
            f"#!/usr/bin/env bash\nprintf generated > ../generated\nexit {failure}\n")
        (self.sdk / "Tools/GenerateProject.sh").write_text(
            f"#!/usr/bin/env bash\nprintf generated > generated\nexit {failure}\n")
        (self.sdk / "ThirdParty").mkdir()
        (self.sdk / "ThirdParty/GenerateSolutions.command").write_text(
            "#!/usr/bin/env bash\nexit 0\n")

    def run_setup(self, script, name="PrivateGame"):
        # The branch intentionally does not exist: CI must use the supplied SHA.
        return subprocess.run(
            ["bash", str(ENGINE / "Tools" / script), name, "nonexistent-branch"],
            cwd=self.root, capture_output=True, text=True, timeout=30,
        )

    def test_existing_checkout_keeps_revision_and_project_settings(self):
        for script in ("NewProjectSetup.sh", "NewProjectSetup.command"):
            with self.subTest(script=script):
                if not (self.sdk / ".git").exists():
                    self.prepare_engine()
                result = self.run_setup(script)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.git(self.sdk, "rev-parse", "HEAD"), self.revision)
                self.assertEqual((self.root / ".gitmodules").read_text(),
                                 "# Existing project settings\n")
                self.assertIn('"PrivateGame"', (self.root / "Game.sharpmake.cs").read_text())
                self.assertTrue((self.root / "generated").is_file())
                (self.root / "generated").unlink()

    def test_existing_submodule_gitfile_is_preserved(self):
        self.prepare_engine(gitfile=True)
        self.assertTrue((self.sdk / ".git").is_file())
        result = self.run_setup("NewProjectSetup.sh")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.git(self.sdk, "rev-parse", "HEAD"), self.revision)

    def test_generation_failure_is_reported(self):
        self.prepare_engine(failure=19)
        for script in ("NewProjectSetup.sh", "NewProjectSetup.command"):
            with self.subTest(script=script):
                self.assertEqual(self.run_setup(script).returncode, 19)

    def test_mac_dependency_failure_stops_before_generation(self):
        self.prepare_engine()
        (self.sdk / "ThirdParty/GenerateSolutions.command").write_text("exit 23\n")
        result = self.run_setup("NewProjectSetup.command")
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertFalse((self.root / "generated").exists())

    def test_invalid_name_does_not_start_generation(self):
        self.prepare_engine()
        for script in ("NewProjectSetup.sh", "NewProjectSetup.command"):
            with self.subTest(script=script):
                result = self.run_setup(script, "Invalid Project")
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertFalse((self.root / "generated").exists())


if __name__ == "__main__":
    unittest.main()
