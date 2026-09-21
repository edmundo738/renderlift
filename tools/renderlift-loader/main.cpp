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
// Mitigation-policy structs (SIGNATURE/IMAGE_LOAD/…) are NTDDI_WIN8+/WIN10
// gated in the Windows SDK — target Windows 10 explicitly.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000000
#endif
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
        "  --param <ansi> ANSI string staged remotely and passed as the call's LPVOID\n"
        "                 (e.g. --call RenderLiftEntryProbe --param C:\\lab\\probe.out)\n"
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

// Resolve an export's RVA by parsing the PE headers of the file image.
//
// Why not LoadLibraryExW(LOAD_LIBRARY_AS_DATAFILE) + GetProcAddress? That
// path relies on undocumented loader behavior: on modern Windows builds
// GetProcAddress may legitimately return NULL for a datafile-mapped module
// (observed in the wild during the first GTA V lab run — bug report in
// docs/research/d3d11-research-layer.md). Reading the export directory
// ourselves is deterministic on every Windows version and never executes
// foreign DllMain code.
std::uint32_t findExportRva(const std::wstring& dllPath, const char* exportName) {
    HANDLE file = CreateFileW(absolutePath(dllPath).c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "error: cannot open DLL file (%lu)\n", GetLastError());
        return 0;
    }
    const DWORD size = GetFileSize(file, nullptr);
    std::vector<BYTE> data(size);
    DWORD done = 0;
    const BOOL rd = ReadFile(file, data.data(), size, &done, nullptr);
    CloseHandle(file);
    if (rd == 0 || done != size || size < 0x100) {
        std::fprintf(stderr, "error: cannot read DLL file completely\n");
        return 0;
    }
    const BYTE* d = data.data();
    auto dwordAt = [&](DWORD off) -> DWORD {
        return *reinterpret_cast<const DWORD*>(d + off);
    };
    auto wordAt = [&](DWORD off) -> WORD {
        return *reinterpret_cast<const WORD*>(d + off);
    };

    if (d[0] != 'M' || d[1] != 'Z') return 0;
    const DWORD pe = dwordAt(0x3C);
    if (pe + 24 > size || std::memcmp(d + pe, "PE\0\0", 4) != 0) return 0;

    const WORD nSections = wordAt(pe + 6);
    const WORD optSize = wordAt(pe + 20);
    const DWORD opt = pe + 24;
    const WORD magic = wordAt(opt);
    const bool is64 = magic == 0x20B;
    const DWORD dataDir = opt + (is64 ? 112 : 96);
    if (dataDir + 8 > size) return 0;
    const DWORD expRva = dwordAt(dataDir);
    if (expRva == 0) return 0;

    const DWORD secBase = opt + optSize;
    const auto rvaToOffset = [&](DWORD rva) -> DWORD {
        for (WORD i = 0; i < nSections; ++i) {
            const DWORD s = secBase + 40 * i;
            if (s + 40 > size) break;
            const DWORD vSize = dwordAt(s + 8);
            const DWORD vAddr = dwordAt(s + 12);
            const DWORD rawSize = dwordAt(s + 16);
            const DWORD rawPtr = dwordAt(s + 20);
            if (rva >= vAddr && rva < vAddr + (vSize > rawSize ? vSize : rawSize)) {
                return rawPtr + (rva - vAddr);
            }
        }
        return 0;
    };

    const DWORD expOff = rvaToOffset(expRva);
    if (expOff == 0 || expOff + 40 > size) return 0;
    const DWORD nNames = dwordAt(expOff + 24);
    const DWORD namesOff = rvaToOffset(dwordAt(expOff + 32));
    const DWORD ordsOff = rvaToOffset(dwordAt(expOff + 36));
    const DWORD funcsOff = rvaToOffset(dwordAt(expOff + 28));
    if (namesOff == 0 || ordsOff == 0 || funcsOff == 0) return 0;

    for (DWORD i = 0; i < nNames; ++i) {
        if (namesOff + 4 * (i + 1) > size) break;
        const DWORD nameOff = rvaToOffset(dwordAt(namesOff + 4 * i));
        if (nameOff == 0 || nameOff >= size) continue;
        const char* name = reinterpret_cast<const char*>(d + nameOff);
        if (std::memchr(name, '\0', size - nameOff) == nullptr) continue;
        if (std::strcmp(name, exportName) == 0) {
            if (ordsOff + 2 * (i + 1) > size) return 0;
            const WORD ordIdx = *reinterpret_cast<const WORD*>(d + ordsOff + 2 * i);
            if (funcsOff + 4 * (DWORD)ordIdx + 4 > size) return 0;
            return dwordAt(funcsOff + 4 * (DWORD)ordIdx);
        }
    }
    std::fprintf(stderr, "error: export '%s' is not listed in %ls\n", exportName,
                 dllPath.c_str());
    return 0;
}

// ── v3.2 frontier diagnostics ───────────────────────────────────────────────
// Before creating the remote thread we (1) dump the target process'
// mitigation policies that can kill remote execution (ACG / CFG-strict /
// signature / image-load rules), and (2) VirtualQueryEx the entry page so a
// protection problem is diagnosed BEFORE it becomes a 0xC0000005.

const char* stateName(DWORD s) {
    switch (s) {
    case MEM_COMMIT: return "COMMIT";
    case MEM_RESERVE: return "RESERVE";
    case MEM_FREE: return "FREE";
    default: return "?";
    }
}
const char* typeName(DWORD t) {
    switch (t) {
    case MEM_IMAGE: return "IMAGE";
    case MEM_MAPPED: return "MAPPED";
    case MEM_PRIVATE: return "PRIVATE";
    default: return "?";
    }
}
const char* protectName(DWORD p) {
    switch (p & 0xFF) {
    case PAGE_NOACCESS: return "NOACCESS";
    case PAGE_READONLY: return "READONLY";
    case PAGE_READWRITE: return "READWRITE";
    case PAGE_WRITECOPY: return "WRITECOPY";
    case PAGE_EXECUTE: return "EXECUTE";
    case PAGE_EXECUTE_READ: return "EXECUTE_READ";
    case PAGE_EXECUTE_READWRITE: return "EXECUTE_READWRITE";
    case PAGE_EXECUTE_WRITECOPY: return "EXECUTE_WRITECOPY";
    default: return "?";
    }
}
bool isExecutableProtect(DWORD p) {
    return (p & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                 PAGE_EXECUTE_WRITECOPY)) != 0;
}

void printMitigationPolicies(HANDLE process) {
    bool any = false;
    PROCESS_MITIGATION_DEP_POLICY dep{};
    if (GetProcessMitigationPolicy(process, ProcessDEPPolicy, &dep, sizeof dep)) {
        std::printf("mitig DEP: Enable=%d Permanent=%d\n", dep.Enable, dep.Permanent);
        any = true;
    }
    PROCESS_MITIGATION_ASLR_POLICY aslr{};
    if (GetProcessMitigationPolicy(process, ProcessASLRPolicy, &aslr, sizeof aslr)) {
        std::printf("mitig ASLR: ForceRelocate=%d BottomUp=%d HighEntropy=%d StrippedNo=%d\n",
                    aslr.EnableForceRelocateImages, aslr.EnableBottomUpRandomization,
                    aslr.EnableHighEntropy, aslr.DisallowStrippedImages);
        any = true;
    }
    PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY cfg{};
    if (GetProcessMitigationPolicy(process, ProcessControlFlowGuardPolicy, &cfg,
                                   sizeof cfg)) {
        std::printf("mitig CFG: Enable=%d StrictMode=%d ExportSuppress=%d\n",
                    cfg.EnableControlFlowGuard, cfg.StrictMode, cfg.EnableExportSuppression);
        any = true;
    }
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY acg{};
    if (GetProcessMitigationPolicy(process, ProcessDynamicCodePolicy, &acg, sizeof acg)) {
        std::printf("mitig ACG: ProhibitDynamicCode=%d ThreadOptOut=%d RemoteDowngrade=%d\n",
                    acg.ProhibitDynamicCode, acg.AllowThreadOptOut, acg.AllowRemoteDowngrade);
        any = true;
    }
    // NOTE: the SIGNATURE (9) / IMAGE_LOAD (11) policy structs are gated out
    // of winnt.h on some CI SDKs regardless of NTDDI_VERSION. Their layouts
    // are ABI-stable since Windows 8/10 — declared locally; the query takes
    // the documented numeric policy ids.
    struct RLMitigationSignaturePolicy {
        union {
            DWORD Flags;
            struct {
                DWORD MicrosoftSignedOnly : 1;
                DWORD StoreSignedOnly : 1;
                DWORD MitigationOptIn : 1;
                DWORD ReservedFlags : 29;
            } b;
        } u;
    } sig{};
    if (GetProcessMitigationPolicy(process, static_cast<PROCESS_MITIGATION_POLICY>(9),
                                   &sig, sizeof sig)) {
        std::printf("mitig SIGN: MicrosoftSignedOnly=%d StoreSignedOnly=%d OptIn=%d\n",
                    sig.u.b.MicrosoftSignedOnly, sig.u.b.StoreSignedOnly,
                    sig.u.b.MitigationOptIn);
        any = true;
    }
    struct RLMitigationImageLoadPolicy {
        union {
            DWORD Flags;
            struct {
                DWORD NoRemoteImages : 1;
                DWORD NoLowMandatoryLabelImages : 1;
                DWORD PreferSystem32Images : 1;
                DWORD ReservedFlags : 29;
            } b;
        } u;
    } img{};
    if (GetProcessMitigationPolicy(process, static_cast<PROCESS_MITIGATION_POLICY>(11),
                                   &img, sizeof img)) {
        std::printf("mitig IMGLOAD: NoRemoteImages=%d NoLowIL=%d PreferSystem32=%d\n",
                    img.u.b.NoRemoteImages, img.u.b.NoLowMandatoryLabelImages,
                    img.u.b.PreferSystem32Images);
        any = true;
    }
    PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY ext{};
    if (GetProcessMitigationPolicy(process, ProcessExtensionPointDisablePolicy, &ext,
                                   sizeof ext)) {
        std::printf("mitig EXTPT: DisableExtensionPoints=%d\n", ext.DisableExtensionPoints);
        any = true;
    }
    if (!any) {
        std::printf("mitig: GetProcessMitigationPolicy unavailable (%lu)\n", GetLastError());
    }
}

// Inspect the page the remote entry lands on. Returns executable-or-not; the
// caller aborts the remote call when the answer is no (no point crashing the
// target for information we already have).
bool describeRemoteEntry(HANDLE process, const void* va) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQueryEx(process, va, &mbi, sizeof mbi) == 0) {
        std::fprintf(stderr, "warn: VirtualQueryEx(%p) failed (%lu)\n", va, GetLastError());
        return true;  // can't know — let the call proceed
    }
    const bool executable =
        (mbi.State == MEM_COMMIT) && isExecutableProtect(mbi.Protect);
    std::printf(
        "entry page: base=%p region=0x%zx state=%s type=%s protect=%s(0x%02lx) "
        "allocBase=%p allocProtect=0x%02lx guard=%d => %s\n",
        mbi.BaseAddress, mbi.RegionSize, stateName(mbi.State), typeName(mbi.Type),
        protectName(mbi.Protect), static_cast<unsigned long>(mbi.Protect),
        mbi.AllocationBase, static_cast<unsigned long>(mbi.AllocationProtect),
        (mbi.Protect & PAGE_GUARD) != 0 ? 1 : 0,
        executable ? "EXECUTABLE" : "NOT EXECUTABLE");
    return executable;
}

// Stage an ANSI string in the target as the remote call's LPVOID param.
LPVOID writeRemoteParam(HANDLE process, const std::string& ansi) {
    if (ansi.empty()) return nullptr;
    const SIZE_T bytes = ansi.size() + 1;
    LPVOID remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_READWRITE);
    if (remote == nullptr) {
        std::fprintf(stderr, "warn: VirtualAllocEx(param) failed (%lu)\n", GetLastError());
        return nullptr;
    }
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, ansi.c_str(), bytes, &written) ||
        written != bytes) {
        std::fprintf(stderr, "warn: WriteProcessMemory(param) failed (%lu)\n",
                     GetLastError());
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return nullptr;
    }
    std::printf("param staged remotely at %p (\"%s\", %zu bytes)\n", remote, ansi.c_str(),
                written);
    return remote;
}

// Call `exportName` inside the injected module remotely. ASLR-safe: we add
// the export's RVA (parsed from the file image — see above) to the module's
// REMOTE base address, which is where Windows actually mapped it.
bool callRemoteExport(HANDLE process, HMODULE remoteModule, const std::string& dllPath,
                      const char* exportName, LPVOID remoteParam) {
    const std::uint32_t rva = findExportRva(
        std::wstring(dllPath.begin(), dllPath.end()), exportName);
    if (rva == 0) {
        std::fprintf(stderr, "error: export '%s' not found in module\n", exportName);
        return false;
    }
    std::printf("export %s at RVA=0x%08lx, remote VA=0x%p\n", exportName,
                static_cast<unsigned long>(rva),
                reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(remoteModule) + rva));

    const void* remoteEntryVa = reinterpret_cast<void*>(
        reinterpret_cast<std::uintptr_t>(remoteModule) + rva);
    if (!describeRemoteEntry(process, remoteEntryVa)) {
        std::fprintf(stderr, "error: entry page is not executable — refusing the remote "
                             "call (this IS the diagnosis, no crash needed)\n");
        return false;
    }

    const auto remoteEntry = reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteEntryVa);
    bool ok = false;
    HANDLE thread =
        CreateRemoteThread(process, nullptr, 0, remoteEntry, remoteParam, 0, nullptr);
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
    return ok;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    std::wstring exeArg;
    std::string dllArg = "RenderLift.D3D11.dll";
    std::string callArg = "RenderLiftInstall";
    std::string paramArg;  // ANSI path staged remotely for --call (e.g. probe)
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
        } else if (arg == L"--param" && i + 1 < argc) {
            const std::wstring w = argv[++i];
            paramArg.assign(w.begin(), w.end());
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

    printMitigationPolicies(process);

    HMODULE remoteModule = nullptr;
    int rc = 1;
    if (injectDll(process, std::wstring(dllArg.begin(), dllArg.end()), &remoteModule)) {
        std::printf("%s loaded remotely at 0x%p\n", dllArg.c_str(),
                    static_cast<void*>(remoteModule));
        LPVOID remoteParam = writeRemoteParam(process, paramArg);
        if (paramArg.empty() || remoteParam != nullptr) {
            if (callRemoteExport(process, remoteModule, dllArg, callArg.c_str(),
                                 remoteParam)) {
                std::printf("module armed — observation log is RenderLift.D3D11.log\n");
                rc = 0;
            }
        }
        if (remoteParam != nullptr) {
            VirtualFreeEx(process, remoteParam, 0, MEM_RELEASE);
        }
    }

    CloseHandle(process);
    return rc;
}
