# Windows 键盘记录器

> **注意：本工具仅供学习研究使用，请勿用于非法用途**

基于 [GiacomoLaw/Keylogger](https://github.com/GiacomoLaw/Keylogger) 的 Windows 专属版本，**不支持 Linux/Mac**。

---

## 目录

- [功能特性](#功能特性)
- [系统要求](#系统要求)
- [编译说明](#编译说明)
- [使用方法](#使用方法)
  - [快捷键控制](#快捷键控制)
  - [日志文件](#日志文件)
  - [自动复制功能](#自动复制功能)
- [技术原理](#技术原理)
- [安全提示](#安全提示)
- [项目结构](#项目结构)
- [依赖库](#依赖库)
- [作者](#作者)

---

## 功能特性

- **键盘记录**：记录所有键盘输入，包括特殊按键
- **窗口追踪**：自动记录当前活动窗口标题和时间戳
- **快捷键控制**：支持多种快捷键进行功能控制
- **开机自启**：可设置/取消开机自动启动
- **静默模式**：支持静默启动，不显示任何提示
- **日志上传**：支持自动上传日志到远程服务器
- **智能存储**：日志按 IP 地址和日期自动分类存储
- **自我复制**：自动复制到 `%appdata%\Keylogger` 目录运行

---

## 系统要求

- **操作系统**：Windows 7/8/10/11
- **架构**：x86/x64
- **编译器**：Visual Studio 2019 或更高版本
- **依赖**：libcurl、nlohmann/json

---

## 编译说明

### 使用 Visual Studio

1. 打开 `Keylogger.sln` 解决方案文件
2. 确保已安装 vcpkg 并配置好以下依赖：
   ```bash
   vcpkg install curl
   vcpkg install nlohmann-json
   ```
3. 选择 Release/x64 配置
4. 点击"生成" → "生成解决方案"

### 编译宏定义

| 宏定义 | 说明 | 可选值 |
|--------|------|--------|
| `invisible` / `visible` | 控制程序窗口是否可见 | `invisible` (默认隐藏) |
| `bootwait` / `nowait` | 系统启动时是否等待 | `bootwait` (默认等待) |
| `FORMAT` | 日志输出格式 | `0`=可读格式, `10`=十进制, `16`=十六进制 |
| `mouseignore` | 是否忽略鼠标点击 | 定义即忽略 |

---

## 使用方法

### 启动程序

直接运行编译生成的可执行文件：
```bash
Keylogger.exe
```

首次运行时会：
1. 自动复制自身到 `%appdata%\Keylogger` 目录
2. 在原位置创建快捷方式
3. 从目标位置重新启动

### 快捷键控制

| 快捷键 | 功能 | 说明 |
|--------|------|------|
| `Ctrl + Shift + Alt + P` | 开始/暂停录制 | 切换录制状态 |
| `Ctrl + Shift + Alt + Q` | 安全退出 | 保存日志并退出程序 |
| `Ctrl + Shift + Alt + S` | 设置/取消开机自启 | 写入注册表，需管理员权限 |
| `Ctrl + Shift + Alt + D` | 设置/取消静默启动 | 静默启动时不显示提示窗口 |
| `Ctrl + Shift + Alt + U` | 开启/关闭上传功能 | 自动上传日志到远程服务器 |

> **提示**：开机自启和静默启动状态会被保存到注册表，重启后依然生效。

### 日志文件

- **存储位置**：`%appdata%\Keylogger\`
- **文件名格式**：`<IP地址>_<YYYYMMDD>.log`
  - 示例：`192.168.1.100_20260331.log`
- **日志内容示例**：
  ```
  [Window: 记事本 - at 2026-03-31T14:30:25+0800] 
  Hello World[TAB]This is a test
  [Window: 浏览器 - at 2026-03-31T14:31:10+0800] 
  www.example.com
  ```

### 自动复制功能

程序启动时会自动执行以下操作：

1. 检查当前运行位置
2. 如果不在 `%appdata%\Keylogger` 目录，则：
   - 复制自身到目标目录
   - 在原位置创建快捷方式
   - 启动目标位置的副本
   - 删除原始文件

---

## 技术原理

### 键盘钩子机制

本程序使用 Windows 低级键盘钩子 (`WH_KEYBOARD_LL`) 实现全局键盘监控：

```cpp
_hook = SetWindowsHookEx(WH_KEYBOARD_LL, HookCallback, NULL, 0);
```

钩子回调函数处理所有键盘事件，包括：
- 按键按下/释放
- 特殊按键（Shift、Ctrl、Alt 等）
- 快捷键组合检测

### 窗口追踪

通过 `GetForegroundWindow()` 获取当前活动窗口，记录窗口标题切换：

```cpp
HWND foreground = GetForegroundWindow();
GetWindowTextA(foreground, window_title, 256);
```

### 日志上传

使用 libcurl 实现 HTTP 文件上传：
1. 通过 API 获取认证 Token
2. 筛选最新的日志文件
3. 使用 PUT 请求上传到服务器

---

## 安全提示

### 杀毒软件误报

> **注意：可能触发杀毒软件误报（因使用键盘钩子技术）**

在 Windows 系统中，hook 与消息传递密切相关，用于监控和处理系统消息。键盘钩子（keyboard hook）和鼠标钩子（mouse hook）可以拦截用户输入，用于实现全局快捷键或系统级监控。在某些恶意软件（如 rootkit）中，hook 被用来隐藏恶意行为或窃取敏感信息。

### 使用建议

1. **仅供学习研究**：请勿在未经授权的计算机上使用
2. **添加白名单**：如被误报，可将程序添加到杀毒软件白名单
3. **不信任请勿使用**：本程序涉及系统级钩子，请确保来源可信

---

## 项目结构

```
keylogger/
├── Keylogger/
│   ├── Keylogger/
│   │   ├── Keylogger.cpp      # 主程序源代码
│   │   └── Keylogger.vcxproj.filters  # 项目筛选器
│   ├── LogUploader.h          # 日志上传类头文件
│   ├── Keylogger.sln          # Visual Studio 解决方案
│   ├── .gitignore             # Git 忽略文件
│   ├── .gitattributes         # Git 属性配置
│   └── README.md              # 项目说明
└── README.md                  # 根目录说明
```

---

## 依赖库

| 库名 | 用途 | 版本 |
|------|------|------|
| [libcurl](https://curl.se/libcurl/) | HTTP 请求/文件上传 | 最新版 |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON 解析 | 最新版 |

---

## 作者

**Yuebi**

---

## 许可证

本项目仅供学习研究使用，请遵守当地法律法规。

---


**请合法使用本工具**

