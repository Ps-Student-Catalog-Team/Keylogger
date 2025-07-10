# Windows 键盘记录器

> **注意：本工具仅供学习研究**

基于 [GiacomoLaw/Keylogger](https://github.com/GiacomoLaw/Keylogger) 的 Windows 专属版本，**不支持 Linux/Mac**。

## 重要提示
- 可能触发杀毒软件误报（因使用键盘钩子技术）
    > 在 Windows 系统中，hook 与消息传递密切相关，用于监控和处理系统消息。键盘钩子（keyboard hook）和鼠标钩子（mouse hook）可以拦截用户输入，用于实现全局快捷键或系统级监控。在某些恶意软件（如 rootkit）中，hook 被用来隐藏恶意行为或窃取敏感信息。
- 核心功能依赖 Windows 钩子机制
- **不信任请勿使用**

## 功能
- 日志保存为 `keylogger.log`
- 快捷键控制：
  - **Ctrl+Shift+Alt+P**：开始/暂停键盘录制
  - **Ctrl+Shift+Alt+Q**：安全退出

---

**作者：Yuebi**
