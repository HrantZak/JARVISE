#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QFile>
#include <QTimer>
#include <future>
#include "../src/app/WindowTool.h"
#include "../src/app/InstalledApplicationTool.h"
#include "../src/app/LaunchCommand.h"
#include "../src/app/WindowCommand.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"
using namespace jarvis;
static QString token(const app::DesktopWindow& w) {return QStringLiteral("window:%1:%2").arg(reinterpret_cast<quintptr>(w.handle)).arg(w.pid);}
static tools::ToolResult invokeWindow(const QString& action,const QString& target,int monitor=0) {
    tools::ToolRegistry registry;
    registry.add(std::make_unique<app::WindowTool>(action.toStdString()));
    tools::ToolValidator validator(registry);
    QJsonObject args{{"target",target}}; if(monitor)args["monitor"]=monitor;
    const auto raw=QJsonDocument(QJsonObject{{"tool","window_"+action},{"arguments",args}}).toJson(QJsonDocument::Compact);
    const auto call=validator.validate(raw.toStdString());
    if(!call)return tools::ToolResult::failure("test",tools::ToolErrorCode::MalformedJson,"Invalid test call");
    std::atomic<bool> cancelled{false};
    return std::async(std::launch::async,[&]{return registry.lookup(call->toolName())->execute(*call,cancelled);}).get();
}
class TestDesktopLive : public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){if(!qEnvironmentVariableIsSet("JARVIS_DESKTOP_LIVE"))QSKIP("Explicit desktop test opt-in required");}
    void realWindow() {
        QProcess child; child.start(QCoreApplication::applicationFilePath(),{"--window-helper"}); QVERIFY(child.waitForStarted());
        struct Stop {QProcess& p;~Stop(){if(p.state()!=QProcess::NotRunning){p.kill();p.waitForFinished(3000);}}} stop{child};
        app::DesktopWindow w;
        QTRY_VERIFY_WITH_TIMEOUT(([&]{for(const auto& item:app::desktopWindows())if(item.pid==child.processId()){w=item;return true;}return false;})(),5000);
        auto result=invokeWindow("maximize",token(w)); QVERIFY2(result.ok(),result.errorMessage().c_str());
        QTRY_VERIFY(IsZoomed(w.handle));
        result=invokeWindow("restore",token(w)); QVERIFY2(result.ok(),result.errorMessage().c_str());
        QTRY_VERIFY(!IsZoomed(w.handle));
        result=invokeWindow("move",token(w),1); QVERIFY2(result.ok(),result.errorMessage().c_str());
        result=invokeWindow("close",token(w)); QVERIFY2(result.ok(),result.errorMessage().c_str());
        QVERIFY(child.waitForFinished(5000));
    }
    void installedNames() {
        tools::ToolRegistry registry;registry.add(std::make_unique<app::InstalledApplicationTool>());
        const auto& allowed=registry.lookup("open_installed_application")->definition().arguments.front().allowedValues;
        qInfo()<<"catalogue size"<<allowed.size();
        for(const auto& name:allowed)if(QString::fromStdString(name).contains("Spotify",Qt::CaseInsensitive)||QString::fromStdString(name).contains("Grand",Qt::CaseInsensitive))qInfo()<<QString::fromStdString(name);
        for(const auto& phrase : {QStringLiteral("Ну-ка, открой Google."),QStringLiteral("Открой маленькую окну Spotify."),QStringLiteral("Открой командную строку."),QStringLiteral("открой gta 5"),QStringLiteral("открой ГТА 5 Legacy")}) {
            const auto call=app::resolveLaunchCommand(phrase,registry);
            QVERIFY2(call.matched && call.error.isEmpty(),qPrintable(phrase+": "+call.error));
        }
    }
    void chromeTabs() {
        const QString chrome="C:/Program Files/Google/Chrome/Application/chrome.exe";
        if(!QFileInfo::exists(chrome))QSKIP("Chrome not installed at expected test path");
        QTemporaryDir profile;QVERIFY(profile.isValid());
        QFile page(profile.path()+"/jarvis-test.html");QVERIFY(page.open(QIODevice::WriteOnly));page.write("<title>JARVIS_BROWSER_TEST_9164389</title><p>Jarvis test</p>");page.close();
        QProcess browser;browser.start(chrome,{"--user-data-dir="+profile.path(),"--remote-debugging-port=0","--no-first-run","--no-default-browser-check","--disable-search-engine-choice-screen",QUrl::fromLocalFile(page.fileName()).toString()});QVERIFY(browser.waitForStarted());
        struct Stop {QProcess& p;~Stop(){if(p.state()!=QProcess::NotRunning){p.terminate();if(!p.waitForFinished(3000)){p.kill();p.waitForFinished(3000);}}}} stop{browser};
        QFile portFile(profile.path()+"/DevToolsActivePort");QTRY_VERIFY_WITH_TIMEOUT(portFile.exists(),15000);QVERIFY(portFile.open(QIODevice::ReadOnly));
        const int port=portFile.readLine().trimmed().toInt();QVERIFY(port>0);
        auto pages=[&] {
            QNetworkAccessManager manager;auto* reply=manager.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/json/list").arg(port))));
            QEventLoop loop;QTimer timer;timer.setSingleShot(true);connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.start(1500);loop.exec();
            QJsonArray result;if(reply->isFinished())for(const auto& entry:QJsonDocument::fromJson(reply->readAll()).array())if(entry.toObject()["type"]=="page")result.append(entry);
            return result;
        };
        QTRY_COMPARE_WITH_TIMEOUT(pages().size(),1,10000);
        qInfo()<<"Test browser pages:"<<QJsonDocument(pages()).toJson(QJsonDocument::Compact);
        for(const auto& item:app::desktopWindows())if(item.app=="chrome")qInfo()<<"Chrome window:"<<item.title<<item.pid;
        app::DesktopWindow w;
        QTRY_VERIFY_WITH_TIMEOUT(([&]{for(const auto& item:app::desktopWindows())if(item.app.compare("chrome",Qt::CaseInsensitive)==0){w=item;return true;}return false;})(),10000);
        auto result=invokeWindow("new_tab",token(w));QVERIFY2(result.ok(),result.errorMessage().c_str());
        QTRY_COMPARE_WITH_TIMEOUT(pages().size(),2,5000);
        result=invokeWindow("close_tab",token(w));QVERIFY2(result.ok(),result.errorMessage().c_str());
        QTRY_COMPARE_WITH_TIMEOUT(pages().size(),1,5000);
        QCOMPARE(pages().first().toObject()["title"].toString(),QStringLiteral("JARVIS_BROWSER_TEST_9164389"));
        result=invokeWindow("close",token(w));QVERIFY2(result.ok(),result.errorMessage().c_str());
        QVERIFY(browser.waitForFinished(5000));
    }
};
static LRESULT CALLBACK testWindowProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);}
int main(int argc,char** argv){
    if(argc>1 && QByteArray(argv[1])=="--window-helper"){
        WNDCLASSW cls{};cls.lpfnWndProc=testWindowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"JarvisTestWindow";RegisterClassW(&cls);
        HWND w=CreateWindowW(cls.lpszClassName,L"JARVIS_CHILD_WINDOW_9164389",WS_OVERLAPPEDWINDOW,40,40,400,250,nullptr,nullptr,cls.hInstance,nullptr);if(!w)return 2;ShowWindow(w,SW_SHOWNOACTIVATE);
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
    }
    QCoreApplication application(argc,argv);TestDesktopLive test;return QTest::qExec(&test,argc,argv);
}
#include "tst_desktop_live.moc"
