--- FMOD SDK Directories ---
--FMODDirectoryWin64 = "C:\\Program Files (x86)\\FMOD SoundSystem\\FMOD Studio API Windows\\";
--FMODDirectoryUWP   = "C:\\Program Files (x86)\\FMOD SoundSystem\\FMOD Studio API Universal Windows Platform\\";
--FMODDirectoryMacOS = "~\\FMOD Programmers API\\";
----------------------------

dofile "Engine/Tools/premake.lua"
GenerateGameSolution()
-- Enter any premake commands here to further modify the game solution
