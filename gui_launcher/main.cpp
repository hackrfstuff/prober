#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        MessageBoxA(NULL, "Failed to determine launcher path.", "prober", MB_ICONERROR);
        return 1;
    }

    std::string dir(exePath, len);
    auto slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir.resize(slash);

    std::string implPath = dir + "\\tools\\gui\\prober_gui_impl.exe";

    DWORD attr = GetFileAttributesA(implPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        std::string msg = "Could not find:\n" + implPath + "\n\nMake sure the distribution is intact.";
        MessageBoxA(NULL, msg.c_str(), "prober", MB_ICONERROR);
        return 1;
    }

    std::string cmdLine = "\"" + implPath + "\"";
    LPSTR origArgs = GetCommandLineA();
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

    std::string guiDir = dir + "\\tools\\gui";
    char oldPath[32768] = {};
    GetEnvironmentVariableA("PATH", oldPath, sizeof(oldPath));
    std::string newPath = guiDir + ";" + oldPath;
    SetEnvironmentVariableA("PATH", newPath.c_str());

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

    WaitForInputIdle(pi.hProcess, 2000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

#else

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

static std::string selfDir() {
    char buf[PATH_MAX] = {};
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return ".";
    std::string path(buf, len);
    auto slash = path.find_last_of('/');
    return (slash != std::string::npos) ? path.substr(0, slash) : ".";
}

int main(int argc, char* argv[]) {
    std::string dir = selfDir();
    std::string implPath = dir + "/tools/gui/prober_gui_impl";

    struct stat st{};
    if (stat(implPath.c_str(), &st) != 0) {
        fprintf(stderr, "prober: could not find GUI implementation:\n  %s\n"
                        "Make sure the distribution is intact.\n", implPath.c_str());
        return 1;
    }

    std::string guiDir = dir + "/tools/gui";
    const char* oldLdPath = getenv("LD_LIBRARY_PATH");
    std::string newLdPath = guiDir + (oldLdPath ? std::string(":") + oldLdPath : "");
    setenv("LD_LIBRARY_PATH", newLdPath.c_str(), 1);

    std::vector<char*> newArgv;
    newArgv.push_back(const_cast<char*>(implPath.c_str()));
    for (int i = 1; i < argc; ++i) newArgv.push_back(argv[i]);
    newArgv.push_back(nullptr);

    execv(implPath.c_str(), newArgv.data());
    perror("prober: execv failed");
    return 1;
}

#endif
