#include <Utils/PlatformUtils.h>

#if ME_PLATFORM_WIN64
#include <processthreadsapi.h>
#endif

#include <filesystem>
#include "File.h"

void PlatformUtils::RunProcess(const Path& inFilePath, const std::string& inArgs /*= ""*/)
{
#if ME_PLATFORM_WIN64
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	// set the size of the structures
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	// start the program up
	CreateProcess(StringUtils::ToWString(inFilePath.FullPath).c_str(),   // the path
		&StringUtils::ToWString(inArgs)[0],        // Command line
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory 
		&si,            // Pointer to STARTUPINFO structure
		&pi             // Pointer to PROCESS_INFORMATION structure (removed extra parentheses)
	);
	// Close process and thread handles. 
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
#endif
}

void PlatformUtils::SystemCall(const Path& inFilePath, const std::string& inArgs /*= ""*/, bool inRunFromDirectory /*= true*/)
{
#if ME_PLATFORM_WIN64
	auto p = std::filesystem::current_path();
	std::string ProgramPath(std::string(p.generic_string()));
	if (inFilePath.IsFile && inRunFromDirectory)
	{
		SetCurrentDirectory(StringUtils::ToWString(inFilePath.Directory).c_str());
	}

	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));

	if (!CreateProcessW(StringUtils::ToWString(inFilePath.FullPath).c_str(), &StringUtils::ToWString(inArgs)[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
	{
		printf("CreateProcess failed (%d).\n", GetLastError());
		throw std::exception("Could not create child process");
	}

	WaitForSingleObject(pi.hProcess, INFINITE);

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	SetCurrentDirectory(StringUtils::ToWString(ProgramPath).c_str());
#else
	std::string progArgs = "\"" + inFilePath.FullPath + "\" " + inArgs;
	system(progArgs.c_str());
#endif
}

void PlatformUtils::CreateDirectory(const Path& inFilePath)
{
#if ME_PLATFORM_WIN64
	std::filesystem::create_directories(inFilePath.Directory.c_str());
#endif
}

void PlatformUtils::OpenFile(const Path& inFilePath)
{
#if ME_PLATFORM_WIN64
	ShellExecute(NULL, L"open", StringUtils::ToWString(inFilePath.FullPath).c_str(), NULL, NULL, SW_SHOWDEFAULT);
#endif
}

void PlatformUtils::OpenFolder(const Path& inFolderPath)
{
#if ME_PLATFORM_WIN64
	ShellExecute(NULL, L"open", StringUtils::ToWString(inFolderPath.Directory).c_str(), NULL, NULL, SW_SHOWDEFAULT);
#endif
}

void PlatformUtils::DeleteFile(const Path& inFilePath)
{
	std::filesystem::remove(inFilePath.FullPath);
}

char* PlatformUtils::ReadBytes(const Path& inFilePath, uint32_t* outSize)
{
    File path = File(inFilePath);
    const std::string& p = path.Read();
    std::ifstream stream(inFilePath.FullPath, std::ios::binary | std::ios::ate);

    if (!stream)
    {
        // Failed to open the file
        return nullptr;
    }

    std::streampos end = stream.tellg();
    stream.seekg(0, std::ios::beg);
    uint32_t size = end - stream.tellg();

    if (size == 0)
    {
        // File is empty
        return nullptr;
    }

    char* buffer = new char[size];
    stream.read((char*)buffer, size);
    stream.close();

    *outSize = size;
    return buffer;
}
