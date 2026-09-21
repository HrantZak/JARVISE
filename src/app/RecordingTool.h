#pragma once
#include "jarvis/tools/ITool.h"
#include "jarvis/tools/CreateArtifactTool.h"
#include <windows.h>
#include <shlobj.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QThread>
#include <QUuid>
#include <mutex>

namespace jarvis::app {
class RecordingTool final : public tools::ITool {
    tools::ToolDefinition def;
    std::mutex mutex;
    bool pending = false, stopSent = false;
    QString captures;
    QSet<QString> baseline;
    static QString captureDirectory() {
        PWSTR path = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(FOLDERID_AppCaptures, 0, nullptr, &path);
        QString result;
        if (SUCCEEDED(hr)) result = QString::fromWCharArray(path);
        CoTaskMemFree(path);
        return result;
    }
    static bool toggle() {
        INPUT keys[6]{};
        const WORD codes[] = {VK_LWIN, VK_MENU, 'R', 'R', VK_MENU, VK_LWIN};
        for (int i=0; i<6; ++i) {
            keys[i].type = INPUT_KEYBOARD;
            keys[i].ki.wVk = codes[i];
            if (i>=3) keys[i].ki.dwFlags = KEYEVENTF_KEYUP;
        }
        if (SendInput(6, keys, sizeof(INPUT)) == 6) return true;
        SendInput(3, keys+3, sizeof(INPUT));
        return false;
    }
public:
    RecordingTool() {
        def.name = "windows_recording";
        def.description = "Request Xbox Game Bar recording start or stop ONLY on explicit user request. Start is unverified; check Windows indicator. Stop copies a new completed MP4 to JARVIS/Recordings. Never retry automatically. Desktop and Explorer are not supported by Game Bar.";
        def.permission = tools::PermissionLevel::SafeAction;
        tools::ArgumentSpec action;
        action.name = "action"; action.type = tools::ArgumentType::Enumeration;
        action.required = true; action.allowedValues = {"start", "stop"};
        def.arguments.push_back(action);
    }
    const tools::ToolDefinition& definition() const override { return def; }
    tools::ToolResult execute(const tools::ValidatedCall& call, const std::atomic<bool>& cancelled) override {
        std::lock_guard<std::mutex> lock(mutex);
        const auto fail = [&](const char* message) { return tools::ToolResult::failure(call.toolName(), tools::ToolErrorCode::Unavailable, message); };
        if (cancelled.load()) return fail("Команда отменена.");
        const bool start = call.enumerationArgument("action") == "start";
        if (start && pending) return fail("Запрос записи уже отправлен. Проверьте индикатор Windows и остановите запись перед новой.");
        if (!start && !pending) return fail("Нет записи, запрошенной JARVIS. Если запись включена вручную, остановите её через панель Windows.");
        if (start || !stopSent) {
            for (int key : {VK_MENU, VK_CONTROL, VK_SHIFT, VK_LWIN, VK_RWIN, int('R')})
                if (GetAsyncKeyState(key) & 0x8000) return fail("Отпустите клавиши и повторите команду.");
        }
        if (start) {
            captures = captureDirectory();
            if (captures.isEmpty() || !QDir().mkpath(captures)) return fail("Папка записей Windows недоступна.");
            baseline.clear();
            for (const auto& file : QDir(captures).entryList({"*.mp4"}, QDir::Files)) baseline.insert(file);
            pending = true; stopSent = false;
            if (!toggle()) return fail("Не все клавиши отправлены. Проверьте запись в панели Windows перед следующей командой.");
            return tools::ToolResult::success(call.toolName(), {{"state", "start_requested"}});
        }
        QStringList candidates;
        for (const auto& file : QDir(captures).entryList({"*.mp4"}, QDir::Files))
            if (!baseline.contains(file)) candidates.append(file);
        // Without evidence of a recording, another toggle might START a recording.
        if (candidates.size() != 1) {
            pending = false;
            return fail("Новая запись Windows не найдена однозначно. Проверьте панель Game Bar (Win+G); переключение не отправлено.");
        }
        const QString source = QDir(captures).filePath(candidates.first());
        if (!stopSent) {
            HANDLE probe = CreateFileW(reinterpret_cast<LPCWSTR>(source.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (probe != INVALID_HANDLE_VALUE) { CloseHandle(probe); stopSent = true; }
            else {
                stopSent = true;
                if (!toggle()) return fail("Проверьте остановку в панели Windows. Повтор команды попробует только сохранить файл.");
            }
        }
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 20000 && !cancelled.load()) {
            HANDLE probe = CreateFileW(reinterpret_cast<LPCWSTR>(source.utf16()), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (probe != INVALID_HANDLE_VALUE) {
                CloseHandle(probe);
                QFile input(source);
                if (input.open(QIODevice::ReadOnly) && input.size() > 0) {
                    const QString directory = tools::CreateArtifactTool::defaultRoot()+"/Recordings";
                    if (!QDir().mkpath(directory)) return fail("Не удалось создать папку Recordings.");
                    const QString destination = directory+"/"+QFileInfo(source).completeBaseName()+"-"+QUuid::createUuid().toString(QUuid::Id128)+".mp4";
                    QSaveFile output(destination);
                    if (!output.open(QIODevice::WriteOnly)) return fail("Не удалось сохранить видео.");
                    while (!input.atEnd()) {
                        if (cancelled.load()) return fail("Сохранение отменено. Исходное видео осталось в папке Windows.");
                        const auto data = input.read(1024*1024);
                        if (data.isEmpty() || output.write(data) != data.size()) return fail("Ошибка копирования видео. Исходный файл сохранён.");
                    }
                    if (!output.commit()) return fail("Не удалось завершить сохранение видео.");
                    pending = false;
                    return tools::ToolResult::success(call.toolName(), {{"state", "saved"}, {"path", QDir::toNativeSeparators(destination).toStdString()}});
                }
            }
            QThread::msleep(250);
        }
        return fail("Windows ещё не завершила файл. Повторите остановку: JARVIS только проверит сохранение, без повторного переключения.");
    }
};
}
