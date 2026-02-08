// prober_gui launcher — tiny Win32 exe that launches the real Qt GUI
// from tools\gui\prober_gui_impl.exe, keeping dist root clean.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    // Get directory of this launcher exe
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        MessageBoxA(NULL, "Failed to determine launcher path.", "prober", MB_ICONERROR);
        return 1;
    }

    std::string dir(exePath, len);
    auto slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir.resize(slash);

    // Build path to real GUI impl
    std::string implPath = dir + "\\tools\\gui\\prober_gui_impl.exe";

    // Check if impl exists
    DWORD attr = GetFileAttributesA(implPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        std::string msg = "Could not find:\n" + implPath + "\n\nMake sure the distribution is intact.";
        MessageBoxA(NULL, msg.c_str(), "prober", MB_ICONERROR);
        return 1;
    }

    // Forward command line arguments
    std::string cmdLine = "\"" + implPath + "\"";
    LPSTR origArgs = GetCommandLineA();
    // Skip past argv[0] in original command line
    if (origArgs) {
        bool inQuote = false;
        const char* p = origArgs;
        while (*p) {
            if (*p == '"') inQuote = !inQuote;
            else if (*p == ' ' && !inQuote) break;
            ++p;
        }
        while (*p == ' ') ++p;
        if (*p) {
            cmdLine += " ";
            cmdLine += p;
        }
    }

    // Set PATH to include tools\gui so Qt DLLs are found
    std::string guiDir = dir + "\\tools\\gui";
    char oldPath[32768] = {};
    GetEnvironmentVariableA("PATH", oldPath, sizeof(oldPath));
    std::string newPath = guiDir + ";" + oldPath;
    SetEnvironmentVariableA("PATH", newPath.c_str());

    // Launch the real GUI
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    if (!CreateProcessA(
            implPath.c_str(),
            const_cast<char*>(cmdLine.c_str()),
            NULL, NULL, FALSE,
            0, NULL,
            guiDir.c_str(),  // working directory = tools\gui
            &si, &pi)) {
        DWORD err = GetLastError();
        char errBuf[256];
        wsprintfA(errBuf, "Failed to launch GUI (error %lu).\n\n%s", err, implPath.c_str());
        MessageBoxA(NULL, errBuf, "prober", MB_ICONERROR);
        return 1;
    }

    // Wait for the GUI to exit
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (int)exitCode;
}
