// ============================================================================
// Antigravity Remote Debug Injector - Simple Inline Hook Version
// ============================================================================
// 不使用 MinHook，使用手动内联 Hook，更简单更可靠
// ============================================================================

#include <windows.h>
#include <stdio.h>
#include "config.h"

#include <winternl.h>

// ============================================================================
// 全局变量
// ============================================================================

static wchar_t g_modifiedCommandLine[32768];
static BOOL g_commandLineModified = FALSE;

static int g_remoteDebugPort = DEFAULT_REMOTE_DEBUG_PORT;
static BOOL g_injectEnabled = TRUE;
static HMODULE g_hSelfModule = NULL;

// ============================================================================
// 辅助函数
// ============================================================================

static void GetCurrentDllDirectory(wchar_t* buffer, size_t bufferSize) {
    if (g_hSelfModule && buffer && bufferSize > 0) {
        GetModuleFileNameW(g_hSelfModule, buffer, (DWORD)bufferSize);
        wchar_t* lastSlash = wcsrchr(buffer, L'\\');
        if (lastSlash) {
            *(lastSlash + 1) = L'\0';
        }
    }
}

static void LoadConfiguration(void) {
    wchar_t configPath[MAX_PATH];
    GetCurrentDllDirectory(configPath, MAX_PATH);
    wcscat(configPath, CONFIG_FILE_NAME);
    
    g_remoteDebugPort = GetPrivateProfileIntW(
        CONFIG_SECTION_NAME, CONFIG_KEY_PORT,
        DEFAULT_REMOTE_DEBUG_PORT, configPath);
    
    g_injectEnabled = GetPrivateProfileIntW(
        CONFIG_SECTION_NAME, CONFIG_KEY_ENABLED, 1, configPath) != 0;
    
    if (g_remoteDebugPort < 1 || g_remoteDebugPort > 65535) {
        g_remoteDebugPort = DEFAULT_REMOTE_DEBUG_PORT;
    }
}

// ============================================================================
// PEB 命令行注入
// ============================================================================

static BOOL InjectCommandLine(void) {
    if (g_commandLineModified) {
        return TRUE;
    }
    
    if (!g_injectEnabled) {
        g_commandLineModified = TRUE;
        return TRUE;
    }
    
    // 获取 PEB
#ifdef _WIN64
    PPEB pPeb = (PPEB)__readgsqword(0x60);
#else
    PPEB pPeb = (PPEB)__readfsdword(0x30);
#endif
    
    if (!pPeb || !pPeb->ProcessParameters) {
        return FALSE;
    }
    
    PRTL_USER_PROCESS_PARAMETERS params = pPeb->ProcessParameters;
    LPWSTR originalCmd = params->CommandLine.Buffer;
    
    if (!originalCmd) {
        return FALSE;
    }
    
    // 复制原命令行
    wcsncpy(g_modifiedCommandLine, originalCmd, 32767);
    g_modifiedCommandLine[32767] = L'\0';
    
    // 只对主进程注入（不包含 --type= 参数）
    if (wcsstr(g_modifiedCommandLine, SUBPROCESS_TYPE_ARG) == NULL) {
        if (wcsstr(g_modifiedCommandLine, L"--remote-debugging-port") == NULL) {
            wchar_t argBuffer[64];
            swprintf(argBuffer, 64, REMOTE_DEBUG_ARG_TEMPLATE, g_remoteDebugPort);

            size_t currentLen = wcslen(g_modifiedCommandLine);
            size_t appendLen = wcslen(argBuffer);

            // 安全追加
            if (currentLen + appendLen < 32767) {
                wcscat(g_modifiedCommandLine, argBuffer);
            }
        }
    }
    
    // 替换 PEB 中的命令行指针
    params->CommandLine.Buffer = g_modifiedCommandLine;
    params->CommandLine.Length = (USHORT)(wcslen(g_modifiedCommandLine) * sizeof(wchar_t));
    params->CommandLine.MaximumLength = (USHORT)(sizeof(g_modifiedCommandLine) > 65535 ? 65535 : sizeof(g_modifiedCommandLine));
    
    g_commandLineModified = TRUE;
    return TRUE;
}

// ============================================================================
// DLL 入口点
// ============================================================================

extern "C" BOOL InitializeForwarding(void);
extern "C" void CleanupForwarding(void);

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            g_hSelfModule = hModule;
            DisableThreadLibraryCalls(hModule);
            
            // InitializeForwarding is now lazy
            
            // 加载配置
            LoadConfiguration();
            
            // 修改 PEB 命令行
            InjectCommandLine();
            break;
            
        case DLL_PROCESS_DETACH:
            CleanupForwarding();
            break;
            
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }
    
    return TRUE;
}
