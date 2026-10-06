#include <Windows.h>
#include <TlHelp32.h>
#include <CommCtrl.h>
#include <Psapi.h>
#include <dbghelp.h>
#include <fstream>
#include <vector>
#include <string>
#include <shellapi.h>
#include <filesystem>
#include <ctime>
#include <wininet.h>
#include <sstream>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace fs = std::filesystem;

#define VERSION L"1.0.2"
#define GITHUB_OWNER "socolata"
#define GITHUB_REPO  "socolata-injector"
#define CONFIG_FILE  "config.json"

#define ID_PROCESS_EDIT     101
#define ID_DLL_LIST         102
#define ID_ADD_DLL_BTN      103
#define ID_REMOVE_DLL_BTN   104
#define ID_CLEAR_DLL_BTN    105
#define ID_NATIVE_BTN       106
#define ID_MANUAL_BTN       107
#define ID_HOOK_BTN         108
#define ID_VALO_BTN         109
#define ID_JAR_BTN          110
#define ID_DISCORD_BTN      111
#define ID_SETTINGS_BTN     112
#define ID_THEME_BTN        113
#define ID_UPDATE_BTN       114
#define ID_LOG_EDIT         115
#define ID_STATUS_STATIC    116
#define ID_ICON_STATIC      117
#define ID_ARCH_STATIC      118
#define ID_CHECK_TIMER      119

#define ID_CMB_METHOD       201
#define ID_CHK_ERASE_PE     202
#define ID_CHK_UNLINK       203
#define ID_CHK_CLOSE        204
#define ID_CHK_STEALTH      205
#define ID_EDT_DELAY        206
#define ID_BTN_OK           207
#define ID_BTN_CANCEL       208

struct Theme {
    COLORREF bg;
    COLORREF btn;
    COLORREF btnHover;
    COLORREF border;
    COLORREF text;
    COLORREF editBg;
};

Theme g_Light = { RGB(248,249,250), RGB(255,255,255), RGB(240,240,240), RGB(200,200,200), RGB(33,37,41), RGB(255,255,255) };
Theme g_Dark = { RGB(30,30,30), RGB(45,45,45), RGB(60,60,60), RGB(70,70,70), RGB(230,230,230), RGB(40,40,40) };

Theme* g_Theme = &g_Light;
bool g_IsDark = false;

typedef NTSTATUS(NTAPI* pNtCreateThreadEx)(PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);
typedef NTSTATUS(NTAPI* pNtAllocateVirtualMemory)(HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG);
typedef NTSTATUS(NTAPI* pNtWriteVirtualMemory)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
typedef NTSTATUS(NTAPI* pNtProtectVirtualMemory)(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG);

struct InjectSettings {
    int method = 0;
    bool erasePE = false;
    bool unlinkModule = false;
    bool closeAfter = false;
    bool stealth = false;
    int delayMs = 0;
} g_Settings;

HWND hProcessEdit, hDllList, hLogEdit, hStatus, hIconStatic, hArchStatic, hMainWnd;
HFONT hFont, hFontBold, hFontTitle, hFontSmall;
HICON hCurrentIcon = nullptr;
HBRUSH hBgBrush = nullptr;
HBRUSH hEditBrush = nullptr;
std::vector<std::string> g_Dlls;
std::wstring g_LastProcess = L"notepad.exe";

std::wstring GetExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    size_t pos = full.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? full.substr(0, pos + 1) : L"";
}

void SaveConfig() {
    std::wstring path = GetExeDir() + L"config.json";
    std::ofstream file(path);
    if (!file.is_open()) return;

    wchar_t procBuf[256]{};
    GetWindowTextW(hProcessEdit, procBuf, 256);
    g_LastProcess = procBuf;

    file << "{\n";
    file << "  \"last_process\": \"";
    for (auto c : g_LastProcess) {
        if (c == L'\\') file << "\\\\";
        else if (c == L'"') file << "\\\"";
        else file << (char)c;
    }
    file << "\",\n";
    file << "  \"theme\": \"" << (g_IsDark ? "dark" : "light") << "\",\n";
    file << "  \"dlls\": [\n";
    for (size_t i = 0; i < g_Dlls.size(); ++i) {
        file << "    \"";
        for (char c : g_Dlls[i]) {
            if (c == '\\') file << "\\\\";
            else if (c == '"') file << "\\\"";
            else file << c;
        }
        file << "\"";
        if (i + 1 < g_Dlls.size()) file << ",";
        file << "\n";
    }
    file << "  ]\n";
    file << "}\n";
    file.close();
}

void LoadConfig() {
    std::wstring path = GetExeDir() + L"config.json";
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    size_t pos = content.find("\"last_process\"");
    if (pos != std::string::npos) {
        pos = content.find('"', pos + 14);
        size_t end = content.find('"', pos + 1);
        if (pos != std::string::npos && end != std::string::npos) {
            std::string proc = content.substr(pos + 1, end - pos - 1);
            g_LastProcess = std::wstring(proc.begin(), proc.end());
        }
    }

    pos = content.find("\"theme\"");
    if (pos != std::string::npos && content.find("dark", pos) != std::string::npos) {
        g_IsDark = true;
        g_Theme = &g_Dark;
    }

    pos = content.find("\"dlls\"");
    if (pos != std::string::npos) {
        size_t arrStart = content.find('[', pos);
        size_t arrEnd = content.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::string arr = content.substr(arrStart + 1, arrEnd - arrStart - 1);
            size_t start = 0;
            while ((start = arr.find('"', start)) != std::string::npos) {
                size_t end = arr.find('"', start + 1);
                if (end == std::string::npos) break;
                std::string dll = arr.substr(start + 1, end - start - 1);
                std::string cleaned;
                for (size_t i = 0; i < dll.size(); ++i) {
                    if (dll[i] == '\\' && i + 1 < dll.size() && dll[i + 1] == '\\') {
                        cleaned += '\\';
                        ++i;
                    }
                    else {
                        cleaned += dll[i];
                    }
                }
                if (!cleaned.empty()) g_Dlls.push_back(cleaned);
                start = end + 1;
            }
        }
    }
}

DWORD GetPID(const std::wstring& name) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (!_wcsicmp(pe.szExeFile, name.c_str())) {
                CloseHandle(hSnap);
                return pe.th32ProcessID;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return 0;
}

bool IsProcess64Bit(DWORD pid) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return true;
    BOOL isWow64 = FALSE;
    IsWow64Process(hProc, &isWow64);
    CloseHandle(hProc);
    return !isWow64;
}

std::wstring GetProcessPath(DWORD pid) {
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return L"";
    wchar_t path[MAX_PATH]{};
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProc, 0, path, &size)) {
        CloseHandle(hProc);
        return path;
    }
    CloseHandle(hProc);
    return L"";
}

void UpdateProcessIconAndArch() {
    if (hCurrentIcon) {
        DestroyIcon(hCurrentIcon);
        hCurrentIcon = nullptr;
    }
    wchar_t buf[256]{};
    GetWindowTextW(hProcessEdit, buf, 256);
    if (!wcslen(buf)) {
        SendMessageW(hIconStatic, STM_SETICON, 0, 0);
        SetWindowTextW(hArchStatic, L"");
        return;
    }
    DWORD pid = GetPID(buf);
    if (!pid) {
        SendMessageW(hIconStatic, STM_SETICON, 0, 0);
        SetWindowTextW(hArchStatic, L"Not found");
        return;
    }
    std::wstring path = GetProcessPath(pid);
    if (!path.empty()) {
        HICON hIcon = nullptr;
        ExtractIconExW(path.c_str(), 0, &hIcon, nullptr, 1);
        if (hIcon) {
            hCurrentIcon = hIcon;
            SendMessageW(hIconStatic, STM_SETICON, (WPARAM)hIcon, 0);
        }
    }
    SetWindowTextW(hArchStatic, IsProcess64Bit(pid) ? L"x64" : L"x86");
}

void Log(const std::wstring& msg) {
    time_t now = time(nullptr);
    tm t;
    localtime_s(&t, &now);
    wchar_t timeBuf[32];
    wcsftime(timeBuf, 32, L"[%H:%M:%S] ", &t);
    std::wstring full = timeBuf + msg + L"\r\n";
    int len = GetWindowTextLengthW(hLogEdit);
    SendMessageW(hLogEdit, EM_SETSEL, len, len);
    SendMessageW(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)full.c_str());
    SendMessageW(hLogEdit, EM_SCROLLCARET, 0, 0);
}

void SetStatus(const wchar_t* text) {
    SetWindowTextW(hStatus, text);
    Log(text);
}

void ApplyTheme(HWND hwnd) {
    if (hBgBrush) DeleteObject(hBgBrush);
    if (hEditBrush) DeleteObject(hEditBrush);
    hBgBrush = CreateSolidBrush(g_Theme->bg);
    hEditBrush = CreateSolidBrush(g_Theme->editBg);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void ToggleTheme(HWND hwnd) {
    g_IsDark = !g_IsDark;
    g_Theme = g_IsDark ? &g_Dark : &g_Light;
    ApplyTheme(hwnd);
    SetWindowTextW(GetDlgItem(hwnd, ID_THEME_BTN), g_IsDark ? L"Light Mode" : L"Dark Mode");
    SaveConfig();
    Log(g_IsDark ? L"Switched to Dark theme" : L"Switched to Light theme");
}

std::string HttpGet(const std::string& url) {
    std::string result;
    HINTERNET hInternet = InternetOpenA("SocolataInjector", INTERNET_OPEN_TYPE_DIRECT, nullptr, nullptr, 0);
    if (!hInternet) return "";
    HINTERNET hConnect = InternetOpenUrlA(hInternet, url.c_str(), nullptr, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE, 0);
    if (!hConnect) { InternetCloseHandle(hInternet); return ""; }
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hConnect, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0) {
        buffer[bytesRead] = 0;
        result += buffer;
    }
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return result;
}

void CheckForUpdates(HWND hwnd) {
    SetStatus(L"[*] Checking for updates...");
    std::string url = "https://api.github.com/repos/" + std::string(GITHUB_OWNER) + "/" + std::string(GITHUB_REPO) + "/releases/latest";
    std::string response = HttpGet(url);
    if (response.empty()) {
        SetStatus(L"[-] Failed to check for updates");
        MessageBoxW(hwnd, L"Could not connect to GitHub.", L"Update Check", MB_OK | MB_ICONWARNING);
        return;
    }
    size_t pos = response.find("\"tag_name\"");
    if (pos == std::string::npos) { SetStatus(L"[-] No release found"); return; }
    pos = response.find('"', pos + 10);
    size_t end = response.find('"', pos + 1);
    if (pos == std::string::npos || end == std::string::npos) { SetStatus(L"[-] Failed to parse version"); return; }
    std::string latest = response.substr(pos + 1, end - pos - 1);
    if (!latest.empty() && (latest[0] == 'v' || latest[0] == 'V')) latest = latest.substr(1);
    std::wstring current = VERSION;
    std::wstring latestW(latest.begin(), latest.end());
    if (latestW > current) {
        std::wstring msg = L"New version available: v" + latestW + L"\n\nCurrent: v" + current + L"\n\nOpen download page?";
        if (MessageBoxW(hwnd, msg.c_str(), L"Update Available", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            ShellExecuteA(nullptr, "open", ("https://github.com/" + std::string(GITHUB_OWNER) + "/" + std::string(GITHUB_REPO) + "/releases/latest").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        SetStatus((L"[+] New version found: v" + latestW).c_str());
    }
    else {
        SetStatus(L"[+] You have the latest version");
        MessageBoxW(hwnd, L"You are using the latest version.", L"Update Check", MB_OK | MB_ICONINFORMATION);
    }
}

bool NativeInject(DWORD pid, const std::string& dllPath) {
    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hProc) return false;

    size_t pathSize = dllPath.size() + 1;

    pNtAllocateVirtualMemory NtAllocate = (pNtAllocateVirtualMemory)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtAllocateVirtualMemory");
    pNtWriteVirtualMemory NtWrite = (pNtWriteVirtualMemory)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtWriteVirtualMemory");
    pNtProtectVirtualMemory NtProtect = (pNtProtectVirtualMemory)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtProtectVirtualMemory");

    PVOID pRemote = nullptr;
    SIZE_T regionSize = pathSize;

    if (NtAllocate) {
        NtAllocate(hProc, &pRemote, 0, &regionSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    else {
        pRemote = VirtualAllocEx(hProc, nullptr, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    if (!pRemote) { CloseHandle(hProc); return false; }

    SIZE_T written = 0;
    if (NtWrite) {
        NtWrite(hProc, pRemote, (PVOID)dllPath.c_str(), pathSize, &written);
    }
    else {
        WriteProcessMemory(hProc, pRemote, dllPath.c_str(), pathSize, &written);
    }
    if (written != pathSize) {
        VirtualFreeEx(hProc, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    ULONG oldProtect = 0;
    SIZE_T protSize = pathSize;
    if (NtProtect) {
        NtProtect(hProc, &pRemote, &protSize, PAGE_READONLY, &oldProtect);
    }
    else {
        DWORD old = 0;
        VirtualProtectEx(hProc, pRemote, pathSize, PAGE_READONLY, &old);
    }

    FARPROC pLoadLib = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (!pLoadLib) {
        VirtualFreeEx(hProc, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    HANDLE hThread = nullptr;
    pNtCreateThreadEx NtCreateThreadEx = (pNtCreateThreadEx)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateThreadEx");
    if (NtCreateThreadEx) {
        NtCreateThreadEx(&hThread, THREAD_ALL_ACCESS, nullptr, hProc, (PVOID)pLoadLib, pRemote, FALSE, 0, 0, 0, nullptr);
    }
    if (!hThread) {
        hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)pLoadLib, pRemote, 0, nullptr);
    }
    if (!hThread) {
        VirtualFreeEx(hProc, pRemote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    WaitForSingleObject(hThread, 12000);
    CloseHandle(hThread);

    if (g_Settings.stealth) {
        VirtualFreeEx(hProc, pRemote, 0, MEM_RELEASE);
    }

    CloseHandle(hProc);
    return true;
}

struct MANUAL_MAPPING_DATA {
    BYTE* pBase;
    HINSTANCE(WINAPI* pLoadLibraryA)(LPCSTR);
    FARPROC(WINAPI* pGetProcAddress)(HMODULE, LPCSTR);
    BOOL(WINAPI* pRtlAddFunctionTable)(PRUNTIME_FUNCTION, DWORD, DWORD64);
};

DWORD WINAPI Shellcode(MANUAL_MAPPING_DATA* pData) {
    if (!pData || !pData->pBase) return 0;
    BYTE* pBase = pData->pBase;
    auto* pDos = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
    auto* pNt = reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + pDos->e_lfanew);
    auto* pOpt = &pNt->OptionalHeader;

    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
        auto* pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
        DWORD_PTR delta = (DWORD_PTR)pBase - pOpt->ImageBase;
        while (pReloc->VirtualAddress) {
            WORD* pRelInfo = reinterpret_cast<WORD*>(pReloc + 1);
            DWORD count = (pReloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
            for (DWORD i = 0; i < count; ++i) {
                if (pNt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64) {
                    if (HIWORD(pRelInfo[i]) == IMAGE_REL_BASED_DIR64) {
                        DWORD_PTR* pPatch = reinterpret_cast<DWORD_PTR*>(pBase + pReloc->VirtualAddress + LOWORD(pRelInfo[i]));
                        *pPatch += delta;
                    }
                }
                else {
                    if (HIWORD(pRelInfo[i]) == IMAGE_REL_BASED_HIGHLOW) {
                        DWORD* pPatch = reinterpret_cast<DWORD*>(pBase + pReloc->VirtualAddress + LOWORD(pRelInfo[i]));
                        *pPatch += (DWORD)delta;
                    }
                }
            }
            pReloc = reinterpret_cast<IMAGE_BASE_RELOCATION*>((BYTE*)pReloc + pReloc->SizeOfBlock);
        }
    }

    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
        auto* pImport = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        while (pImport->Name) {
            char* modName = (char*)(pBase + pImport->Name);
            HMODULE hMod = pData->pLoadLibraryA(modName);
            if (hMod) {
                auto* pThunk = reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImport->FirstThunk);
                auto* pOrig = pImport->OriginalFirstThunk ? reinterpret_cast<IMAGE_THUNK_DATA*>(pBase + pImport->OriginalFirstThunk) : pThunk;
                while (pOrig->u1.AddressOfData) {
                    if (IMAGE_SNAP_BY_ORDINAL(pOrig->u1.Ordinal))
                        *(FARPROC*)&pThunk->u1.Function = pData->pGetProcAddress(hMod, (LPCSTR)IMAGE_ORDINAL(pOrig->u1.Ordinal));
                    else {
                        auto* pName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + pOrig->u1.AddressOfData);
                        *(FARPROC*)&pThunk->u1.Function = pData->pGetProcAddress(hMod, pName->Name);
                    }
                    ++pThunk; ++pOrig;
                }
            }
            ++pImport;
        }
    }

    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
        auto* pTLS = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
        auto* pCallback = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(pTLS->AddressOfCallBacks);
        if (pCallback) while (*pCallback) { (*pCallback)((LPVOID)pBase, DLL_PROCESS_ATTACH, nullptr); ++pCallback; }
    }

    if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size && pData->pRtlAddFunctionTable) {
        auto* pException = reinterpret_cast<RUNTIME_FUNCTION*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].VirtualAddress);
        DWORD count = pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size / sizeof(RUNTIME_FUNCTION);
        pData->pRtlAddFunctionTable(pException, count, (DWORD64)pBase);
    }

    if (pOpt->AddressOfEntryPoint) {
        auto DllMain = (BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID))(pBase + pOpt->AddressOfEntryPoint);
        return DllMain((HINSTANCE)pBase, DLL_PROCESS_ATTACH, nullptr);
    }
    return TRUE;
}

bool ManualMap(DWORD pid, const std::string& dllPath, bool is64) {
    std::ifstream file(dllPath, std::ios::binary | std::ios::ate);
    if (!file) return false;
    auto size = file.tellg();
    if (size < 0x100) return false;
    file.seekg(0);
    std::vector<BYTE> raw((size_t)size);
    if (!file.read((char*)raw.data(), size)) return false;
    file.close();

    auto* pDos = (IMAGE_DOS_HEADER*)raw.data();
    if (pDos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* pNt = (IMAGE_NT_HEADERS*)(raw.data() + pDos->e_lfanew);
    if (pNt->Signature != IMAGE_NT_SIGNATURE) return false;
    if (is64 && pNt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return false;
    if (!is64 && pNt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return false;

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) return false;

    BYTE* pRemoteBase = (BYTE*)VirtualAllocEx(hProc, nullptr, pNt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!pRemoteBase) { CloseHandle(hProc); return false; }

    WriteProcessMemory(hProc, pRemoteBase, raw.data(), pNt->OptionalHeader.SizeOfHeaders, nullptr);
    auto* pSec = IMAGE_FIRST_SECTION(pNt);
    for (WORD i = 0; i < pNt->FileHeader.NumberOfSections; ++i, ++pSec)
        if (pSec->SizeOfRawData)
            WriteProcessMemory(hProc, pRemoteBase + pSec->VirtualAddress, raw.data() + pSec->PointerToRawData, pSec->SizeOfRawData, nullptr);

    if (g_Settings.erasePE) {
        BYTE zeros[0x1000]{};
        WriteProcessMemory(hProc, pRemoteBase, zeros, min(pNt->OptionalHeader.SizeOfHeaders, 0x1000u), nullptr);
    }

    MANUAL_MAPPING_DATA data{};
    data.pBase = pRemoteBase;
    data.pLoadLibraryA = LoadLibraryA;
    data.pGetProcAddress = GetProcAddress;
    data.pRtlAddFunctionTable = (BOOL(WINAPI*)(PRUNTIME_FUNCTION, DWORD, DWORD64))GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlAddFunctionTable");

    void* pData = VirtualAllocEx(hProc, nullptr, sizeof(data), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pData) { VirtualFreeEx(hProc, pRemoteBase, 0, MEM_RELEASE); CloseHandle(hProc); return false; }
    WriteProcessMemory(hProc, pData, &data, sizeof(data), nullptr);

    void* pCode = VirtualAllocEx(hProc, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!pCode) {
        VirtualFreeEx(hProc, pRemoteBase, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, pData, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }
    WriteProcessMemory(hProc, pCode, (void*)Shellcode, 0x1000, nullptr);

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, (LPTHREAD_START_ROUTINE)pCode, pData, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProc, pRemoteBase, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, pData, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, pCode, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    WaitForSingleObject(hThread, 15000);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, pData, 0, MEM_RELEASE);
    VirtualFreeEx(hProc, pCode, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return true;
}

bool HookInject(DWORD pid, const std::string& dllPath) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te{ sizeof(te) };
    DWORD threadId = 0;
    if (Thread32First(hSnap, &te)) {
        do { if (te.th32OwnerProcessID == pid) { threadId = te.th32ThreadID; break; } } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);
    if (!threadId) return false;

    HMODULE hMod = LoadLibraryA(dllPath.c_str());
    if (!hMod) return false;
    HOOKPROC addr = (HOOKPROC)GetProcAddress(hMod, "NextHook");
    if (!addr) addr = (HOOKPROC)GetProcAddress(hMod, "HookProc");
    if (!addr) addr = (HOOKPROC)hMod;

    HHOOK hHook = SetWindowsHookExA(WH_GETMESSAGE, addr, hMod, threadId);
    if (!hHook) { FreeLibrary(hMod); return false; }
    PostThreadMessageA(threadId, WM_NULL, 0, 0);
    Sleep(800);
    UnhookWindowsHookEx(hHook);
    return true;
}

std::string ResolveExport(const std::wstring& path) {
    HMODULE h = LoadLibraryExW(path.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!h) return "";
    ULONG s = 0;
    auto d = (PIMAGE_EXPORT_DIRECTORY)ImageDirectoryEntryToData(h, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &s);
    if (!d) { FreeLibrary(h); return ""; }
    DWORD* names = (DWORD*)((BYTE*)h + d->AddressOfNames);
    for (DWORD i = 0; i < d->NumberOfNames; i++) {
        char* name = (char*)((BYTE*)h + names[i]);
        if (strcmp(name, "DllMain") != 0) {
            std::string ret = name;
            FreeLibrary(h);
            return ret;
        }
    }
    FreeLibrary(h);
    return "";
}

bool ValorantHookInject(const std::string& dllPath) {
    std::wstring wpath(dllPath.begin(), dllPath.end());
    if (!fs::exists(wpath)) return false;
    std::string entry = ResolveExport(wpath);
    if (entry.empty()) return false;
    HWND hw = FindWindowW(L"VALORANTUnrealWindow", NULL);
    if (!hw) return false;
    DWORD tid = 0;
    GetWindowThreadProcessId(hw, &tid);
    if (!tid) return false;
    HMODULE hDll = LoadLibraryExW(wpath.c_str(), NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!hDll) return false;
    HOOKPROC proc = (HOOKPROC)GetProcAddress(hDll, entry.c_str());
    if (!proc) { FreeLibrary(hDll); return false; }
    HHOOK hhk = SetWindowsHookExW(WH_GETMESSAGE, proc, hDll, tid);
    if (!hhk) { FreeLibrary(hDll); return false; }
    PostThreadMessageW(tid, WM_NULL, 0, 0);
    Sleep(1000);
    return true;
}

bool JarInject(DWORD pid, const std::string& jarPath) {
    std::wstring wJar(jarPath.begin(), jarPath.end());
    std::wstring cmd = L"javaw.exe -javaagent:\"" + wJar + L"\" -jar \"" + wJar + L"\"";
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, (LPWSTR)cmd.c_str(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }
    return NativeInject(pid, jarPath);
}

void RefreshDllList() {
    SendMessageW(hDllList, LB_RESETCONTENT, 0, 0);
    for (const auto& dll : g_Dlls) {
        SendMessageA(hDllList, LB_ADDSTRING, 0, (LPARAM)dll.c_str());
    }
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"Injection Method:", WS_VISIBLE | WS_CHILD, 20, 20, 140, 20, hwnd, nullptr, nullptr, nullptr);
        HWND hCmb = CreateWindowW(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST, 20, 42, 260, 200, hwnd, (HMENU)ID_CMB_METHOD, nullptr, nullptr);
        SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"Native Inject");
        SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"Manual Map");
        SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"SetWindowsHook");
        SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"Valorant Hook");
        SendMessageW(hCmb, CB_ADDSTRING, 0, (LPARAM)L"JAR Inject");
        SendMessageW(hCmb, CB_SETCURSEL, g_Settings.method, 0);

        CreateWindowW(L"BUTTON", L"Erase PE Headers", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 20, 85, 160, 22, hwnd, (HMENU)ID_CHK_ERASE_PE, nullptr, nullptr);
        if (g_Settings.erasePE) SendMessageW(GetDlgItem(hwnd, ID_CHK_ERASE_PE), BM_SETCHECK, BST_CHECKED, 0);
        CreateWindowW(L"BUTTON", L"Unlink Module", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 20, 112, 160, 22, hwnd, (HMENU)ID_CHK_UNLINK, nullptr, nullptr);
        if (g_Settings.unlinkModule) SendMessageW(GetDlgItem(hwnd, ID_CHK_UNLINK), BM_SETCHECK, BST_CHECKED, 0);
        CreateWindowW(L"BUTTON", L"Close after injection", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 20, 139, 180, 22, hwnd, (HMENU)ID_CHK_CLOSE, nullptr, nullptr);
        if (g_Settings.closeAfter) SendMessageW(GetDlgItem(hwnd, ID_CHK_CLOSE), BM_SETCHECK, BST_CHECKED, 0);
        CreateWindowW(L"BUTTON", L"Stealth Mode", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 20, 166, 140, 22, hwnd, (HMENU)ID_CHK_STEALTH, nullptr, nullptr);
        if (g_Settings.stealth) SendMessageW(GetDlgItem(hwnd, ID_CHK_STEALTH), BM_SETCHECK, BST_CHECKED, 0);

        CreateWindowW(L"STATIC", L"Inject Delay (ms):", WS_VISIBLE | WS_CHILD, 20, 200, 130, 20, hwnd, nullptr, nullptr, nullptr);
        HWND hDelay = CreateWindowW(L"EDIT", L"0", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER, 150, 198, 80, 24, hwnd, (HMENU)ID_EDT_DELAY, nullptr, nullptr);
        wchar_t buf[16]; wsprintfW(buf, L"%d", g_Settings.delayMs);
        SetWindowTextW(hDelay, buf);

        CreateWindowW(L"BUTTON", L"OK", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 80, 250, 90, 30, hwnd, (HMENU)ID_BTN_OK, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Cancel", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 190, 250, 90, 30, hwnd, (HMENU)ID_BTN_CANCEL, nullptr, nullptr);
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == ID_BTN_OK) {
            g_Settings.method = (int)SendMessageW(GetDlgItem(hwnd, ID_CMB_METHOD), CB_GETCURSEL, 0, 0);
            g_Settings.erasePE = SendMessageW(GetDlgItem(hwnd, ID_CHK_ERASE_PE), BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_Settings.unlinkModule = SendMessageW(GetDlgItem(hwnd, ID_CHK_UNLINK), BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_Settings.closeAfter = SendMessageW(GetDlgItem(hwnd, ID_CHK_CLOSE), BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_Settings.stealth = SendMessageW(GetDlgItem(hwnd, ID_CHK_STEALTH), BM_GETCHECK, 0, 0) == BST_CHECKED;
            wchar_t delayBuf[16]{};
            GetWindowTextW(GetDlgItem(hwnd, ID_EDT_DELAY), delayBuf, 16);
            g_Settings.delayMs = _wtoi(delayBuf);
            DestroyWindow(hwnd);
        }
        else if (LOWORD(wParam) == ID_BTN_CANCEL) DestroyWindow(hwnd);
        break;
    }
    case WM_CLOSE: DestroyWindow(hwnd); break;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void OpenSettings(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = SettingsProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"SocolataSettings";
        RegisterClassExW(&wc);
        registered = true;
    }
    HWND hSet = CreateWindowExW(WS_EX_DLGMODALFRAME, L"SocolataSettings", L"Advanced Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0, 360, 330, parent, nullptr, GetModuleHandle(nullptr), nullptr);
    RECT rcParent;
    GetWindowRect(parent, &rcParent);
    SetWindowPos(hSet, nullptr, rcParent.left + 40, rcParent.top + 40, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(hSet, SW_SHOW);
}

void DrawButton(HDC hdc, RECT rc, const wchar_t* text, bool pressed) {
    COLORREF bg = pressed ? g_Theme->btnHover : g_Theme->btn;
    HBRUSH br = CreateSolidBrush(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, g_Theme->border);
    SelectObject(hdc, br);
    SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    DeleteObject(br);
    DeleteObject(pen);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, g_Theme->text);
    SelectObject(hdc, hFontBold);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        hMainWnd = hwnd;
        LoadConfig();

        hBgBrush = CreateSolidBrush(g_Theme->bg);
        hEditBrush = CreateSolidBrush(g_Theme->editBg);
        hFont = CreateFontW(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        hFontBold = CreateFontW(12, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        hFontTitle = CreateFontW(17, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        hFontSmall = CreateFontW(11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

        std::wstring title = L"Socolata Injector  v" + std::wstring(VERSION);
        HWND hTitle = CreateWindowW(L"STATIC", title.c_str(), WS_VISIBLE | WS_CHILD | SS_CENTER, 0, 10, 640, 26, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFontTitle, TRUE);

        hIconStatic = CreateWindowW(L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_ICON | SS_CENTERIMAGE, 20, 48, 36, 36, hwnd, (HMENU)ID_ICON_STATIC, nullptr, nullptr);
        hArchStatic = CreateWindowW(L"STATIC", L"", WS_VISIBLE | WS_CHILD, 62, 58, 50, 18, hwnd, (HMENU)ID_ARCH_STATIC, nullptr, nullptr);
        SendMessageW(hArchStatic, WM_SETFONT, (WPARAM)hFontBold, TRUE);

        HWND l1 = CreateWindowW(L"STATIC", L"Process Name", WS_VISIBLE | WS_CHILD, 120, 45, 100, 16, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(l1, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

        hProcessEdit = CreateWindowW(L"EDIT", g_LastProcess.c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 120, 63, 300, 26, hwnd, (HMENU)ID_PROCESS_EDIT, nullptr, nullptr);
        SendMessageW(hProcessEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        CreateWindowW(L"BUTTON", L"Settings", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 435, 61, 90, 30, hwnd, (HMENU)ID_SETTINGS_BTN, nullptr, nullptr);
        CreateWindowW(L"BUTTON", g_IsDark ? L"Light Mode" : L"Dark Mode", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 535, 61, 90, 30, hwnd, (HMENU)ID_THEME_BTN, nullptr, nullptr);

        HWND l2 = CreateWindowW(L"STATIC", L"Files (check the ones you want to inject)  –  Drag & Drop", WS_VISIBLE | WS_CHILD, 20, 105, 400, 16, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(l2, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

        hDllList = CreateWindowW(L"LISTBOX", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | LBS_MULTIPLESEL,
            20, 124, 430, 90, hwnd, (HMENU)ID_DLL_LIST, nullptr, nullptr);
        SendMessageW(hDllList, WM_SETFONT, (WPARAM)hFont, TRUE);
        DragAcceptFiles(hDllList, TRUE);
        DragAcceptFiles(hwnd, TRUE);

        RefreshDllList();

        CreateWindowW(L"BUTTON", L"Add File", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 465, 124, 160, 28, hwnd, (HMENU)ID_ADD_DLL_BTN, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Remove", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 465, 158, 160, 28, hwnd, (HMENU)ID_REMOVE_DLL_BTN, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Clear", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 465, 192, 160, 28, hwnd, (HMENU)ID_CLEAR_DLL_BTN, nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Native", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 20, 230, 100, 34, hwnd, (HMENU)ID_NATIVE_BTN, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Manual Map", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 130, 230, 100, 34, hwnd, (HMENU)ID_MANUAL_BTN, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Hook", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 240, 230, 100, 34, hwnd, (HMENU)ID_HOOK_BTN, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Valorant", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 350, 230, 100, 34, hwnd, (HMENU)ID_VALO_BTN, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"JAR", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 460, 230, 100, 34, hwnd, (HMENU)ID_JAR_BTN, nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Join Discord", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 20, 275, 300, 32, hwnd, (HMENU)ID_DISCORD_BTN, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Check for Updates", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 340, 275, 285, 32, hwnd, (HMENU)ID_UPDATE_BTN, nullptr, nullptr);

        HWND l3 = CreateWindowW(L"STATIC", L"Log", WS_VISIBLE | WS_CHILD, 20, 320, 40, 16, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(l3, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

        hLogEdit = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            20, 338, 605, 90, hwnd, (HMENU)ID_LOG_EDIT, nullptr, nullptr);
        SendMessageW(hLogEdit, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

        std::wstring status = L"Ready  •  v" + std::wstring(VERSION) + L"  •  Made by Socolata";
        hStatus = CreateWindowW(L"STATIC", status.c_str(), WS_VISIBLE | WS_CHILD | SS_CENTER, 20, 440, 605, 20, hwnd, (HMENU)ID_STATUS_STATIC, nullptr, nullptr);
        SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetTimer(hwnd, ID_CHECK_TIMER, 800, nullptr);
        UpdateProcessIconAndArch();
        Log(L"Socolata Injector v" + std::wstring(VERSION) + L" ready");
        Log(L"Config loaded (" + std::to_wstring(g_Dlls.size()) + L" files)");
        break;
    }

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            char path[MAX_PATH]{};
            DragQueryFileA(hDrop, i, path, MAX_PATH);
            std::string ext = path;
            size_t dot = ext.find_last_of('.');
            if (dot != std::string::npos) {
                ext = ext.substr(dot);
                for (auto& c : ext) c = (char)tolower(c);
                if (ext == ".dll" || ext == ".jar") {
                    g_Dlls.push_back(path);
                    SendMessageA(hDllList, LB_ADDSTRING, 0, (LPARAM)path);
                    Log(L"Dropped: " + std::wstring(path, path + strlen(path)));
                }
            }
        }
        DragFinish(hDrop);
        SaveConfig();
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType != ODT_BUTTON) break;
        bool pressed = dis->itemState & ODS_SELECTED;
        wchar_t text[64]{};
        GetWindowTextW(dis->hwndItem, text, 64);
        DrawButton(dis->hDC, dis->rcItem, text, pressed);
        return TRUE;
    }

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_Theme->text);
        SetBkColor(hdc, g_Theme->editBg);
        return (LRESULT)hEditBrush;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, g_Theme->text);
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)hBgBrush;
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, hBgBrush);
        return 1;
    }

    case WM_TIMER:
        if (wParam == ID_CHECK_TIMER) UpdateProcessIconAndArch();
        break;

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_SETTINGS_BTN: OpenSettings(hwnd); break;
        case ID_THEME_BTN: ToggleTheme(hwnd); break;
        case ID_UPDATE_BTN: CheckForUpdates(hwnd); break;
        case ID_ADD_DLL_BTN: {
            OPENFILENAMEA ofn{};
            char file[MAX_PATH]{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = "DLL / JAR Files\0*.dll;*.jar\0DLL Files\0*.dll\0JAR Files\0*.jar\0All Files\0*.*\0";
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
            if (GetOpenFileNameA(&ofn)) {
                g_Dlls.push_back(file);
                SendMessageA(hDllList, LB_ADDSTRING, 0, (LPARAM)file);
                Log(L"Added: " + std::wstring(file, file + strlen(file)));
                SaveConfig();
            }
            break;
        }
        case ID_REMOVE_DLL_BTN: {
            int sel = (int)SendMessageW(hDllList, LB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                SendMessageW(hDllList, LB_DELETESTRING, sel, 0);
                if (sel < (int)g_Dlls.size()) g_Dlls.erase(g_Dlls.begin() + sel);
                Log(L"Removed file");
                SaveConfig();
            }
            break;
        }
        case ID_CLEAR_DLL_BTN:
            SendMessageW(hDllList, LB_RESETCONTENT, 0, 0);
            g_Dlls.clear();
            Log(L"List cleared");
            SaveConfig();
            break;
        case ID_DISCORD_BTN:
            ShellExecuteA(nullptr, "open", "https://discord.gg/qvPz6ggxnZ", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ID_NATIVE_BTN:
        case ID_MANUAL_BTN:
        case ID_HOOK_BTN:
        case ID_VALO_BTN:
        case ID_JAR_BTN: {
            int count = (int)SendMessageW(hDllList, LB_GETCOUNT, 0, 0);
            if (count == 0) {
                SetStatus(L"[-] Add at least one file");
                break;
            }

            std::vector<std::string> selected;
            for (int i = 0; i < count; ++i) {
                if (SendMessageW(hDllList, LB_GETSEL, i, 0) > 0) {
                    if (i < (int)g_Dlls.size()) selected.push_back(g_Dlls[i]);
                }
            }
            if (selected.empty()) {
                SetStatus(L"[-] Select at least one file (click to highlight)");
                break;
            }

            wchar_t procBuf[256]{};
            GetWindowTextW(hProcessEdit, procBuf, 256);
            SaveConfig();

            DWORD pid = 0;
            bool is64 = true;
            if (LOWORD(wParam) != ID_VALO_BTN && LOWORD(wParam) != ID_JAR_BTN) {
                if (!wcslen(procBuf)) { SetStatus(L"[-] Enter process name"); break; }
                pid = GetPID(procBuf);
                if (!pid) { SetStatus(L"[-] Process not found"); break; }
                is64 = IsProcess64Bit(pid);
            }

            if (g_Settings.delayMs > 0) {
                Log(L"Waiting " + std::to_wstring(g_Settings.delayMs) + L" ms...");
                Sleep(g_Settings.delayMs);
            }

            int success = 0;
            for (const auto& file : selected) {
                Log(L"Injecting: " + std::wstring(file.begin(), file.end()));
                bool ok = false;
                if (LOWORD(wParam) == ID_NATIVE_BTN) ok = NativeInject(pid, file);
                else if (LOWORD(wParam) == ID_MANUAL_BTN) ok = ManualMap(pid, file, is64);
                else if (LOWORD(wParam) == ID_HOOK_BTN) ok = HookInject(pid, file);
                else if (LOWORD(wParam) == ID_VALO_BTN) ok = ValorantHookInject(file);
                else if (LOWORD(wParam) == ID_JAR_BTN) ok = JarInject(pid, file);

                if (ok) { success++; Log(L"[+] Success"); }
                else Log(L"[-] Failed");
            }

            std::wstring result = L"Finished: " + std::to_wstring(success) + L"/" + std::to_wstring(selected.size()) + L" succeeded";
            SetStatus(result.c_str());
            if (success > 0 && g_Settings.closeAfter) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            break;
        }
        }
        break;
    }

    case WM_DESTROY:
        SaveConfig();
        KillTimer(hwnd, ID_CHECK_TIMER);
        if (hCurrentIcon) DestroyIcon(hCurrentIcon);
        if (hBgBrush) DeleteObject(hBgBrush);
        if (hEditBrush) DeleteObject(hEditBrush);
        DeleteObject(hFont); DeleteObject(hFontBold); DeleteObject(hFontTitle); DeleteObject(hFontSmall);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    HICON hIcon = (HICON)LoadImageW(nullptr, L"icon.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!hIcon) hIcon = LoadIcon(nullptr, IDI_APPLICATION);

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"SocolataInjector";
    wc.hIcon = hIcon;
    wc.hIconSm = hIcon;
    RegisterClassExW(&wc);

    std::wstring windowTitle = L"Socolata Injector v" + std::wstring(VERSION);
    HWND hwnd = CreateWindowExW(0, L"SocolataInjector", windowTitle.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 660, 520, nullptr, nullptr, hInst, nullptr);

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
