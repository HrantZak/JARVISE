#pragma once
#include "jarvis/tools/ITool.h"
#include <QString>
#include <QFileInfo>
#include <QRegularExpression>
#include <windows.h>
#include <algorithm>
#include <vector>
#include <thread>
#include <chrono>
#include "CommandText.h"

namespace jarvis::app {
struct DesktopWindow { HWND handle{}; DWORD pid{}; QString title, app; };
inline QString windowAlias(QString value) {
    value = commandText(value);
    if (value.startsWith(QStringLiteral("окно "))) value.remove(0,5);
    if (value == "гугл" || value == "гугле" || value == "хром" || value == "хроме" || value == "google chrome") return "chrome";
    if (value == "телеграм") return "telegram";
    if (value == "спотифай") return "spotify";
    if (value == "гугла" || value == "google") return "chrome";
    if (value == "стим") return "steam";
    if (value == "эдж" || value == "edge") return "msedge";
    if (value == "блокнот") return "notepad";
    if (value == "проводник") return "explorer";
    if (value == "дискорд") return "discord";
    return value;
}
inline std::vector<DesktopWindow> desktopWindows() {
    std::vector<DesktopWindow> result;
    EnumWindows([](HWND h, LPARAM data) -> BOOL {
        if ((GetWindow(h, GW_OWNER) && !(GetWindowLongPtrW(h,GWL_EXSTYLE)&WS_EX_APPWINDOW)) || !GetWindowTextLengthW(h)) return TRUE;
        wchar_t cls[128]{}; GetClassNameW(h, cls, 128);
        if (wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) return TRUE;
        DesktopWindow item; item.handle = h;
        GetWindowThreadProcessId(h, &item.pid);
        wchar_t title[2048]{}; GetWindowTextW(h, title, 2048); item.title = QString::fromWCharArray(title);
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, item.pid);
        if (process) {
            wchar_t path[32768]; DWORD size = 32768;
            if (QueryFullProcessImageNameW(process, 0, path, &size)) item.app = QFileInfo(QString::fromWCharArray(path, size)).completeBaseName();
            CloseHandle(process);
        }
        static_cast<std::vector<DesktopWindow>*>(reinterpret_cast<void*>(data))->push_back(item);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
inline bool matchesWindow(const DesktopWindow& w, const QString& target) {
    if (target.startsWith("window:")) return target == QStringLiteral("window:%1:%2").arg(reinterpret_cast<quintptr>(w.handle)).arg(w.pid);
    const auto name = windowAlias(target);
    if (name.isEmpty()) return false;
    const QString app = w.app.toLower();
    const QString title = w.title.toLower();
    return app == name || app.contains(name) || title.contains(name) ||
        (name == QStringLiteral("chrome") && (app.contains("google") || title.contains("chrome") || title.contains("google"))) ||
        (name == QStringLiteral("spotify") && (app.contains("spotify") || title.contains("spotify")));
}
inline bool focusDesktopWindow(HWND handle) {
    if(GetForegroundWindow()==handle) return true;
    MSG message{}; PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE); // worker needs a message queue for AttachThreadInput
    const DWORD own=GetCurrentThreadId(), foreground=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
    const bool attached=foreground && foreground!=own && AttachThreadInput(own,foreground,TRUE);
    if(IsIconic(handle)) ShowWindowAsync(handle,SW_RESTORE);
    SetForegroundWindow(handle);
    if(attached) AttachThreadInput(own,foreground,FALSE);
    for(int i=0;i<20 && GetForegroundWindow()!=handle;++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return GetForegroundWindow()==handle;
}
class WindowTool final : public tools::ITool {
    tools::ToolDefinition def;
public:
    explicit WindowTool(std::string action) {
        def.name = "window_" + action;
        def.description = "Windows desktop action: " + action + ". target is app name or unique window title. current means foreground window. Never guess ambiguous targets. close_except preserves every matching window and Jarvis. close_tab and fullscreen only support Chrome/Edge/Firefox; send keyboard shortcut, not verified page state. move uses monitor number, primary first. maximize fills work area, fullscreen sends F11.";
        def.permission = action == "list" ? tools::PermissionLevel::ReadOnly :
            action == "close_except" ? tools::PermissionLevel::ConfirmRequired : tools::PermissionLevel::SafeAction;
        def.timeoutMs = 5000;
        if (action != "list") {
            tools::ArgumentSpec target; target.name = "target"; target.type = tools::ArgumentType::Text; target.required = true; target.maxTextChars = 200;
            def.arguments.push_back(target);
        }
        if (action == "move") {
            tools::ArgumentSpec monitor; monitor.name = "monitor"; monitor.type = tools::ArgumentType::Integer; monitor.required = true; monitor.minimum = 1; monitor.maximum = 16;
            def.arguments.push_back(monitor);
        }
    }
    const tools::ToolDefinition& definition() const override { return def; }
    tools::ToolResult execute(const tools::ValidatedCall& call, const std::atomic<bool>& cancelled) override {
        const auto fail = [&](const char* message) { return tools::ToolResult::failure(call.toolName(), tools::ToolErrorCode::ExecutionFailed, message); };
        if (cancelled.load()) return tools::ToolResult::failure(call.toolName(), tools::ToolErrorCode::Cancelled, "Cancelled");
        const auto windows = desktopWindows();
        if (def.name == "window_list") {
            QString list;
            for (const auto& w : windows) list += w.app + ": " + w.title + "\n";
            return tools::ToolResult::success(call.toolName(), {{"windows", list.left(12000).toStdString()}});
        }
        const QString target = QString::fromStdString(call.textArgument("target"));
        const bool current = target == "current" || target == "это окно" || target == "текущее окно";
        const HWND foreground = GetForegroundWindow();
        std::vector<DesktopWindow> selected;
        for (const auto& w : windows) if (current ? w.handle == foreground : matchesWindow(w, target)) selected.push_back(w);
        if (selected.empty()) return fail("Окно не найдено. Ничего не изменено.");
        if (def.name == "window_close_except") {
            if (current) return fail("Укажите имя приложения, которое нужно оставить.");
            // Different matching apps are ambiguous: never close anything in this case.
            for (const auto& w : selected) if (w.app != selected.front().app) return fail("Под это название подходят разные приложения. Уточните имя.");
            int sent = 0, failed = 0;
            for (const auto& w : windows) {
                if (cancelled.load()) return fail("Остановлено; часть запросов закрытия уже отправлена.");
                if (w.pid == GetCurrentProcessId() || w.app.startsWith("JARVIS", Qt::CaseInsensitive) || matchesWindow(w, target)) continue;
                DWORD pid{}; GetWindowThreadProcessId(w.handle, &pid);
                if (pid != w.pid || !PostMessageW(w.handle, WM_CLOSE, 0, 0)) ++failed; else ++sent;
            }
            if (failed) return fail("Не всем окнам удалось отправить закрытие. Некоторые запросы уже отправлены.");
            return tools::ToolResult::success(call.toolName(), {{"status", "Запросы закрытия отправлены. Приложения могут запросить сохранение."}, {"count", std::to_string(sent)}});
        }
        if (selected.size() > 1) {
            const auto active=std::find_if(selected.begin(),selected.end(),[&](const auto& item){return item.handle==foreground;});
            if(active!=selected.end()) { const auto chosen=*active; selected={chosen}; }
        }
        if (selected.size() != 1) return fail("Найдено несколько окон. Уточните заголовок нужного окна.");
        const auto& w = selected.front();
        DWORD pid{}; GetWindowThreadProcessId(w.handle, &pid);
        if (pid != w.pid || !IsWindow(w.handle)) return fail("Окно уже изменилось. Повторите команду.");
        if (def.name == "window_close") {
            if (w.pid == GetCurrentProcessId()) return fail("Закройте Jarvis кнопкой выхода.");
            if (!PostMessageW(w.handle, WM_CLOSE, 0, 0)) return fail("Windows не разрешила закрытие.");
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            return tools::ToolResult::success(call.toolName(), {{"status", "Запрос закрытия отправлен. Может потребоваться сохранить работу."}});
        }
        if (def.name == "window_close_tab" || def.name == "window_new_tab" || def.name == "window_fullscreen") {
            if (w.app.compare("chrome", Qt::CaseInsensitive) && w.app.compare("msedge", Qt::CaseInsensitive) && w.app.compare("firefox", Qt::CaseInsensitive)) return fail("Эта команда вкладки или полного экрана поддерживается только в Chrome, Edge и Firefox.");
            if (!focusDesktopWindow(w.handle)) return fail("Windows не дала фокус браузеру. Переключитесь на него и повторите.");
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000 || GetAsyncKeyState(VK_SHIFT) & 0x8000 || GetAsyncKeyState(VK_MENU) & 0x8000) return fail("Отпустите клавиши Ctrl, Shift и Alt и повторите.");
            const WORD letter = def.name == "window_new_tab" ? 'T' : 'W';
            INPUT keys[4]{}; UINT count = def.name != "window_fullscreen" ? 4 : 2;
            for (UINT i=0;i<count;++i) keys[i].type = INPUT_KEYBOARD;
            if (count == 4) { keys[0].ki.wVk=VK_CONTROL; keys[1].ki.wVk=letter; keys[2].ki.wVk=letter; keys[2].ki.dwFlags=KEYEVENTF_KEYUP; keys[3].ki.wVk=VK_CONTROL; keys[3].ki.dwFlags=KEYEVENTF_KEYUP; }
            else { keys[0].ki.wVk=VK_F11; keys[1].ki.wVk=VK_F11; keys[1].ki.dwFlags=KEYEVENTF_KEYUP; }
            if (cancelled.load() || GetForegroundWindow() != w.handle) return fail("Команда отменена или активное окно изменилось.");
            if (SendInput(count, keys, sizeof(INPUT)) != count) {
                INPUT release[2]{};
                for(auto& key : release) { key.type=INPUT_KEYBOARD; key.ki.dwFlags=KEYEVENTF_KEYUP; }
                release[0].ki.wVk=count==4 ? letter : VK_F11; release[1].ki.wVk=VK_CONTROL;
                SendInput(count==4 ? 2 : 1,release,sizeof(INPUT));
                return fail("Windows не приняла все клавиши.");
            }
            return tools::ToolResult::success(call.toolName(), {{"status", "Сочетание клавиш отправлено браузеру; изменение вкладки не проверено."}});
        }
        if (def.name == "window_move") {
            std::vector<MONITORINFO> monitors;
            EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR h, HDC, LPRECT, LPARAM p)->BOOL { MONITORINFO info{sizeof(MONITORINFO)}; if(GetMonitorInfoW(h,&info)) reinterpret_cast<std::vector<MONITORINFO>*>(p)->push_back(info); return TRUE; }, reinterpret_cast<LPARAM>(&monitors));
            std::sort(monitors.begin(),monitors.end(),[](const auto& a,const auto& b){ if ((a.dwFlags & MONITORINFOF_PRIMARY) != (b.dwFlags & MONITORINFOF_PRIMARY)) return (a.dwFlags & MONITORINFOF_PRIMARY) != 0; if(a.rcMonitor.left!=b.rcMonitor.left) return a.rcMonitor.left<b.rcMonitor.left; return a.rcMonitor.top<b.rcMonitor.top; });
            const auto index = call.integerArgument("monitor") - 1;
            if (index < 0 || index >= static_cast<long long>(monitors.size())) return fail("Такой монитор не подключён. Окно не перемещено.");
            const RECT area = monitors[index].rcWork;
            RECT old{}; if(!GetWindowRect(w.handle,&old)) return fail("Не удалось прочитать положение окна.");
            ShowWindowAsync(w.handle,SW_RESTORE);
            if(!SetWindowPos(w.handle,nullptr,area.left,area.top,std::min(old.right-old.left,area.right-area.left),std::min(old.bottom-old.top,area.bottom-area.top),SWP_NOZORDER|SWP_NOACTIVATE)) return fail("Windows не разрешила перемещение.");
            return tools::ToolResult::success(call.toolName(), {{"status", "Окно перемещено."}});
        }
        const int mode = def.name == "window_minimize" ? SW_MINIMIZE : def.name == "window_restore" ? SW_RESTORE : SW_MAXIMIZE;
        if(!ShowWindowAsync(w.handle, mode)) return fail("Windows не приняла изменение окна.");
        return tools::ToolResult::success(call.toolName(), {{"status", "Запрос изменения окна отправлен."}});
    }
};
}
