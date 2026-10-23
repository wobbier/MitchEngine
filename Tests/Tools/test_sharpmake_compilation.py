"""Compile the C# source/reference set used by the generated Windows project."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


ENGINE = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("dotnet"), ".NET SDK is required")
class SharpmakeCompilationTests(unittest.TestCase):
    def test_windows_project_sources_compile_with_bundled_assemblies(self):
        definition = (ENGINE / "Tools/SharpmakeProject.sharpmake.cs").read_text()
        project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
        properties = ET.SubElement(project, "PropertyGroup")
        for key, value in {
            "TargetFramework": "net8.0",
            "EnableDefaultCompileItems": "false",
            "DefineConstants": "WIN64",
            "EnableNETAnalyzers": "false",
        }.items():
            ET.SubElement(properties, key).text = value
        items = ET.SubElement(project, "ItemGroup")
        sources = [ENGINE / "Engine.sharpmake.cs"]
        for source in re.findall(
                r'SourceFiles.Add\(@"\[project.SharpmakeCsPath\]/([^"]+)"\)', definition):
            if source.startswith("Engine/"):
                # Windows paths are case insensitive; retain that behavior on
                # Linux, where the vendored ImGUI directory has different case.
                path = ENGINE
                for part in Path(source).parts[1:]:
                    path = next(child for child in path.iterdir()
                                if child.name.casefold() == part.casefold())
            else:
                path = ENGINE / "Tools/ProjectTemplate" / source
            sources.append(path)
        for path in sources:
            ET.SubElement(items, "Compile", Include=str(path))
        for reference in re.findall(
                r'ReferencesByPath.Add\(@"\[project.SharpmakeCsPath\]/Engine/([^"]+)"\)',
                definition):
            assembly = ET.SubElement(items, "Reference", Include=Path(reference).stem)
            ET.SubElement(assembly, "HintPath").text = str(ENGINE / reference)

        with tempfile.TemporaryDirectory(prefix="mitch sharpmake compile ") as directory:
            filename = Path(directory) / "SharpmakeSmoke.csproj"
            ET.ElementTree(project).write(filename, encoding="unicode")
            result = subprocess.run(
                ["dotnet", "build", "--nologo", "--verbosity", "quiet", str(filename)],
                capture_output=True, text=True, timeout=120,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
