#pragma once
#include "jarvis/tools/ITool.h"
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <windows.h>
#include <objbase.h>
#include <UIAutomation.h>
#include <oleauto.h>
#include <vector>
#include "WindowTool.h"

namespace jarvis::app {
inline bool isBrowserWindow(HWND window) {
    if (!window) return false;
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    wchar_t path[32768]{}; DWORD size=32768;
    const bool read=QueryFullProcessImageNameW(process,0,path,&size);
    CloseHandle(process);
    if (!read) return false;
    const QString app=QFileInfo(QString::fromWCharArray(path,size)).completeBaseName().toLower();
    return app == "chrome" || app == "msedge" || app == "firefox" || app == "brave" || app == "opera";
}

inline bool sendUnicodeText(const QString& text) {
    std::vector<INPUT> inputs;
    inputs.reserve(static_cast<size_t>(text.size()) * 2);
    for (const QChar ch : text.left(2000)) {
        if (ch == QChar('\n') || ch == QChar('\r')) {
            INPUT down{}; down.type=INPUT_KEYBOARD; down.ki.wVk=VK_RETURN;
            INPUT up=down; up.ki.dwFlags=KEYEVENTF_KEYUP; inputs.push_back(down); inputs.push_back(up); continue;
        }
        INPUT down{}; down.type=INPUT_KEYBOARD; down.ki.dwFlags=KEYEVENTF_UNICODE; down.ki.wScan=ch.unicode();
        INPUT up=down; up.ki.dwFlags=KEYEVENTF_UNICODE|KEYEVENTF_KEYUP; inputs.push_back(down); inputs.push_back(up);
    }
    return !inputs.empty() && SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) == inputs.size();
}

inline bool sendEnter() {
    INPUT keys[2]{}; keys[0].type=INPUT_KEYBOARD; keys[0].ki.wVk=VK_RETURN;
    keys[1]=keys[0]; keys[1].ki.dwFlags=KEYEVENTF_KEYUP;
    return SendInput(2,keys,sizeof(INPUT)) == 2;
}

inline bool invokeBrowserLink(int requestedIndex, QString* chosenName, QString* error) {
    if (requestedIndex < 1 || requestedIndex > 20) { if(error) *error="Номер ссылки должен быть от 1 до 20."; return false; }
    const HWND active=GetForegroundWindow();
    if(!isBrowserWindow(active)) { if(error) *error="Активное окно не является поддерживаемым браузером."; return false; }
    HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(init) && init!=RPC_E_CHANGED_MODE) { if(error) *error="Windows Automation недоступна."; return false; }
    IUIAutomation* automation=nullptr; IUIAutomationElement* root=nullptr; IUIAutomationCondition* condition=nullptr; IUIAutomationElementArray* links=nullptr;
    bool success=false; int visibleIndex=0;
    do {
        if(FAILED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)))) break;
        if(FAILED(automation->ElementFromHandle(active,&root))) break;
        VARIANT hyperlinkType{};
        VariantInit(&hyperlinkType);
        hyperlinkType.vt=VT_I4;
        hyperlinkType.lVal=UIA_HyperlinkControlTypeId;
        const HRESULT conditionResult=automation->CreatePropertyCondition(UIA_ControlTypePropertyId,hyperlinkType,&condition);
        VariantClear(&hyperlinkType);
        if(FAILED(conditionResult)) break;
        // Google and other search pages may still be building their UIA tree
        // when QDesktopServices reports that the browser opened. Poll briefly
        // so "open the first link" works immediately after a voice search.
        for (int attempt=0; attempt<20 && !success; ++attempt) {
            if (attempt > 0) Sleep(200);
            if (links) { links->Release(); links=nullptr; }
            if (FAILED(root->FindAll(TreeScope_Subtree,condition,&links))) break;
            int length=0;
            if (FAILED(links->get_Length(&length))) break;
            visibleIndex=0;
            for(int i=0;i<length;++i) {
                IUIAutomationElement* element=nullptr; if(FAILED(links->GetElement(i,&element)) || !element) continue;
                VARIANT offscreen{}; element->GetCurrentPropertyValue(UIA_IsOffscreenPropertyId,&offscreen);
                const bool hidden=offscreen.vt==VT_BOOL && offscreen.boolVal;
                BSTR rawName=nullptr; element->get_CurrentName(&rawName); const QString name=rawName ? QString::fromWCharArray(rawName) : QString{}; if(rawName) SysFreeString(rawName);
                if(!hidden && !name.trimmed().isEmpty()) {
                    const QString normalized=name.simplified().toLower();
                    static const QStringList utilityLinks={QStringLiteral("картинки"),QStringLiteral("видео"),QStringLiteral("новости"),QStringLiteral("карты"),QStringLiteral("images"),QStringLiteral("videos"),QStringLiteral("news"),QStringLiteral("maps"),QStringLiteral("shopping"),QStringLiteral("more"),QStringLiteral("about this result")};
                    if(!utilityLinks.contains(normalized) && !normalized.startsWith(QStringLiteral("об этом результате"))) {
                        IUIAutomationInvokePattern* invoke=nullptr;
                        if(SUCCEEDED(element->GetCurrentPattern(UIA_InvokePatternId,reinterpret_cast<IUnknown**>(&invoke))) && invoke) {
                            ++visibleIndex;
                            if(visibleIndex==requestedIndex) {
                                success=SUCCEEDED(invoke->Invoke());
                                invoke->Release();
                                if(chosenName) *chosenName=name;
                                element->Release(); break;
                            }
                            invoke->Release();
                        }
                    }
                }
                element->Release();
            }
        }
    } while(false);
    if(!success && error) {
        if (visibleIndex==0) *error="На странице не найдено доступных ссылок.";
        else *error=QStringLiteral("На странице найдено только %1 доступных ссылок.").arg(visibleIndex);
    }
    if(links) links->Release(); if(condition) condition->Release(); if(root) root->Release(); if(automation) automation->Release();
    if(init==S_OK) CoUninitialize();
    return success;
}

class BrowserTool final : public tools::ITool {
    tools::ToolDefinition def;
public:
    explicit BrowserTool(std::string action) {
        def.name="browser_"+action;
        def.description="Browser action: "+action+". Open safe http(s) links, search Google, type plain text into the active browser field, or press Enter. Never reads passwords, page contents or cookies.";
        def.permission=tools::PermissionLevel::SafeAction;
        def.timeoutMs=action=="open_link" ? 10000 : 8000;
        if(action=="open_url") { tools::ArgumentSpec arg; arg.name="url"; arg.type=tools::ArgumentType::Text; arg.required=true; arg.maxTextChars=2048; def.arguments.push_back(arg); }
        if(action=="search" || action=="type") { tools::ArgumentSpec arg; arg.name="text"; arg.type=tools::ArgumentType::Text; arg.required=true; arg.maxTextChars=2000; def.arguments.push_back(arg); }
        if(action=="open_link") { tools::ArgumentSpec arg; arg.name="index"; arg.type=tools::ArgumentType::Integer; arg.required=true; arg.minimum=1; arg.maximum=20; def.arguments.push_back(arg); }
    }
    const tools::ToolDefinition& definition() const override{return def;}
    tools::ToolResult execute(const tools::ValidatedCall& call,const std::atomic<bool>& cancelled) override {
        const auto fail=[&](const char* message){return tools::ToolResult::failure(call.toolName(),tools::ToolErrorCode::ExecutionFailed,message);};
        if(cancelled.load()) return tools::ToolResult::failure(call.toolName(),tools::ToolErrorCode::Cancelled,"Cancelled");
        if(def.name=="browser_open_url") {
            const QUrl url(QString::fromStdString(call.textArgument("url")));
            if(!url.isValid() || (url.scheme()!="http" && url.scheme()!="https") || url.host().isEmpty()) return fail("Разрешены только ссылки http и https.");
            if(!QDesktopServices::openUrl(url)) return fail("Windows не смогла открыть ссылку.");
            return tools::ToolResult::success(call.toolName(),{{"status","Ссылка открыта."},{"url",url.toString().toStdString()}});
        }
        if(def.name=="browser_search") {
            const QString text=QString::fromStdString(call.textArgument("text")).trimmed();
            if(text.isEmpty()) return fail("Пустой поисковый запрос.");
            QUrl url("https://www.google.com/search"); QUrlQuery query; query.addQueryItem("q",text.left(1000)); url.setQuery(query);
            if(!QDesktopServices::openUrl(url)) return fail("Windows не смогла открыть Google.");
            return tools::ToolResult::success(call.toolName(),{{"status","Поиск Google открыт."},{"query",text.toStdString()}});
        }
        if(def.name=="browser_open_link") {
            QString name,error;
            if(!invokeBrowserLink(static_cast<int>(call.integerArgument("index")),&name,&error)) {
                if(error.isEmpty()) return fail("Не удалось открыть ссылку.");
                return tools::ToolResult::failure(call.toolName(),tools::ToolErrorCode::ExecutionFailed,error.toStdString());
            }
            return tools::ToolResult::success(call.toolName(),{{"status","Ссылка открыта."},{"title",name.toStdString()}});
        }
        HWND active=GetForegroundWindow();
        if(!isBrowserWindow(active)) return fail("Активное окно не является Chrome, Edge, Firefox, Brave или Opera.");
        if(def.name=="browser_type") {
            const QString text=QString::fromStdString(call.textArgument("text"));
            if(!sendUnicodeText(text)) return fail("Windows не приняла текст для ввода.");
            return tools::ToolResult::success(call.toolName(),{{"status","Текст введён в активное поле браузера."}});
        }
        if(def.name=="browser_submit") {
            if(!sendEnter()) return fail("Windows не приняла клавишу Enter.");
            return tools::ToolResult::success(call.toolName(),{{"status","Enter отправлен браузеру."}});
        }
        return fail("Неизвестная команда браузера.");
    }
};
}
