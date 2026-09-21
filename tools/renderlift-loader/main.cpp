// ─────────────────────────────────────────────────────────────────────────────
// RenderLift.Loader (Windows) — minimal local injection for authorized labs.
//
// Loads a RenderLift backend module (RenderLift.D3D11.dll) into a running
// game process and calls its RenderLiftInstall export. Everything else —
// observing, classifying, steering — happens inside the module itself.
//
// Technique (all documented Win32 APIs, standard for mod tools):
//   1. Find the target process by executable name (Toolhelp32) or use --pid.
//   2. OpenProcess (create-thread / VM access — the minimum required).
//   3. VirtualAllocEx + WriteProcessMemory: stash the DLL's absolute path.
//   4. CreateRemoteThread(LoadLibraryW): the loader trick itself.
//   5. EnumProcessModules: find the freshly loaded module's remote base.
//   6. Export RVA from a local DATAFILE copy → remote absolute address.
//   7. CreateRemoteThread(remoteRenderLiftInstall): arm the module.
//
// DISCLAIMER: this loader is for SINGLE-PLAYER, AUTHORIZED test labs only
// (see SECURITY.md). Anti-cheat systems treat remote threads as hostile by
// design; RenderLift is not, and will never be, anti-cheat compatible.
//
// STATUS (0.2): vetted by code review against the documented API contracts
// and exercised through `RenderLift.CLI`, but NOT yet run against a real
// game process — the first live run is a Research Layer deliverable.
// ─────────────────────────────────────────────────────────────────────────────

#if !defined(_WIN32)
#error "renderlift-loader is Windows-only."
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <psapi.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "psapi.lib")

namespace {

void usage() {
    std::printf(
        "RenderLift.Loader — inject a RenderLift backend module into a game\n"
        "\n"
        "Usage:\n"
        "  RenderLift.Loader --exe GTA5.exe --dll C:\\path\\RenderLift.D3D11.dll\n"
        "  RenderLift.Loader --pid 4242 --dll RenderLift.D3D11.dll\n"
        "\n"
        "Options:\n"
        "  --exe <name>   find process by executable name (Toolhelp32 snapshot)\n"
        "  --pid <id>     attach to an explicit process id\n"
        "  --call <name>  entry point to invoke after load (default RenderLiftInstall)\n"
        "\n"
        "Requires the game to be running and authorized for testing (SECURITY.md).\n");
}

DWORD findPidByExeName(const std::wstring& exeName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    DWORD found = 0;
    for (BOOL ok = Process32FirstW(snapshot, &entry);
         ok && found == 0; ok = Process32NextW(snapshot, &entry)) {
        if (_wcsicmp(entry.szExeFile, exeName.c_str()) == 0) {
            found = entry.th32ProcessID;
        }
    }
    CloseHandle(snapshot);
    return found;
}

// Full absolute path (LoadLibrary needs it inside the remote process).
std::wstring absolutePath(const std::wstring& path) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH, buffer, nullptr);
    return n == 0 ? path : std::wstring(buffer, n);
}

// Inject the DLL with the remote-LoadLibraryW trick; on success returns true
// and fills remoteModule with the DLL's base address inside the target.
bool injectDll(HANDLE process, const std::wstring& dllPath, HMODULE* remoteModule) {
    const std::wstring fullPath = absolutePath(dllPath);
    const std::size_t bytes = (fullPath.size() + 1) * sizeof(wchar_t);

    LPVOID remotePath = VirtualAllocEx(process, nullptr, bytes,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remotePath == nullptr) {
        std::fprintf(stderr, "error: VirtualAllocEx failed (%lu)\n", GetLastError());
        return false;
    }

    bool ok = false;
    do {
        if (!WriteProcessMemory(process, remotePath, fullPath.c_str(), bytes, nullptr)) {
            std::fprintf(stderr, "error: WriteProcessMemory failed (%lu)\n", GetLastError());
            break;
        }

        // kernel32 is loaded at the same address in every process of the same
        // bitness on Windows, so our LoadLibraryW address is valid remotely.
        const auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        if (loadLibraryW == nullptr) break;

        HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryW, remotePath, 0,
                                           nullptr);
        if (thread == nullptr) {
            std::fprintf(stderr, "error: CreateRemoteThread(LoadLibraryW) failed (%lu)\n",
                         GetLastError());
            break;
        }
        WaitForSingleObject(thread, INFINITE);

        DWORD remoteHandle = 0;
        GetExitCodeThread(thread, &remoteHandle);
        CloseHandle(thread);
        if (remoteHandle == 0) {
            std::fprintf(stderr, "error: remote LoadLibraryW returned NULL\n");
            break;
        }

        *remoteModule = reinterpret_cast<HMODULE>(static_cast<uintptr_t>(remoteHandle));
        ok = true;
    } while (false);

    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    return ok;
}

// Call `exportName` inside the injected module remotely (ASLR-safe: compute
// the export's RVA locally, add the module's REMOTE base address).
bool callRemoteExport(HANDLE process, HMODULE remoteModule, const std::string& dllPath,
                      const char* exportName) {
    HMODULE localCopy = LoadLibraryExW(absolutePath(
        std::wstring(dllPath.begin(), dllPath.end())).c_str(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE | DONT_RESOLVE_DLL_REFERENCES);
    if (localCopy == nullptr) {
        std::fprintf(stderr, "error: cannot map local copy of the DLL (%lu)\n",
                     GetLastError());
        return false;
    }

    bool ok = false;
    const FARPROC localExport = GetProcAddress(localCopy, exportName);
    if (localExport == nullptr) {
        std::fprintf(stderr, "error: export '%s' not found in module\n", exportName);
    } else {
        const std::uintptr_t rva = reinterpret_cast<std::uintptr_t>(localExport) -
                                   reinterpret_cast<std::uintptr_t>(localCopy);
        const auto remoteEntry = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            reinterpret_cast<std::uintptr_t>(remoteModule) + rva);

        HANDLE thread = CreateRemoteThread(process, nullptr, 0, remoteEntry, nullptr, 0,
                                           nullptr);
        if (thread == nullptr) {
            std::fprintf(stderr, "error: CreateRemoteThread(%s) failed (%lu)\n", exportName,
                         GetLastError());
        } else {
            WaitForSingleObject(thread, INFINITE);
            DWORD exitCode = 0;
            GetExitCodeThread(thread, &exitCode);
            CloseHandle(thread);
            std::printf("remote %s returned 0x%08lx\n", exportName, exitCode);
            ok = SUCCEEDED(static_cast<HRESULT>(exitCode));
        }
    }

    FreeLibrary(localCopy);
    return ok;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    std::wstring exeArg;
    std::string dllArg = "RenderLift.D3D11.dll";
    std::string callArg = "RenderLiftInstall";
    DWORD explicitPid = 0;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--exe" && i + 1 < argc) {
            exeArg = argv[++i];
        } else if (arg == L"--pid" && i + 1 < argc) {
            explicitPid = static_cast<DWORD>(_wtoi(argv[++i]));
        } else if (arg == L"--dll" && i + 1 < argc) {
            const std::wstring w = argv[++i];
            dllArg.assign(w.begin(), w.end());
        } else if (arg == L"--call" && i + 1 < argc) {
            const std::wstring w = argv[++i];
            callArg.assign(w.begin(), w.end());
        } else if (arg == L"--help" || arg == L"/?") {
            usage();
            return 0;
        }
    }

    DWORD pid = explicitPid;
    if (pid == 0 && !exeArg.empty()) {
        pid = findPidByExeName(exeArg);
    }
    if (pid == 0) {
        std::fprintf(stderr, "error: no target process (use --exe or --pid)\n");
        usage();
        return 1;
    }

    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                     PROCESS_VM_OPERATION | PROCESS_VM_READ |
                                     PROCESS_VM_WRITE,
                                 FALSE, pid);
    if (process == nullptr) {
        std::fprintf(stderr, "error: OpenProcess(%lu) failed (%lu) — running elevated?\n",
                     pid, GetLastError());
        return 1;
    }
    std::printf("target pid %lu opened\n", pid);

    HMODULE remoteModule = nullptr;
    int rc = 1;
    if (injectDll(process, std::wstring(dllArg.begin(), dllArg.end()), &remoteModule)) {
        std::printf("%s loaded remotely at 0x%p\n", dllArg.c_str(),
                    static_cast<void*>(remoteModule));
        if (callRemoteExport(process, remoteModule, dllArg, callArg.c_str())) {
            std::printf("module armed — observation log is RenderLift.D3D11.log\n");
            rc = 0;
        }
    }

    CloseHandle(process);
    return rc;
}
