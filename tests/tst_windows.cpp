#include <QtTest>
#include <QJsonDocument>
#include "../src/app/WindowTool.h"
#include "../src/app/WindowCommand.h"
#include "../src/app/BrowserCommand.h"
#include "../src/app/MusicCommand.h"
#include "../src/app/SpotifyCommand.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"
class TestWindows : public QObject {
    Q_OBJECT
private slots:
    void movesOnlyTestWindow() {
        using namespace jarvis;
        const HWND handle=CreateWindowExW(0,L"STATIC",L"JARVIS_WINDOW_TEST_9164389",WS_OVERLAPPEDWINDOW,30,30,320,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        QVERIFY(handle != nullptr);
        struct Cleanup { HWND h; ~Cleanup(){ DestroyWindow(h); } } cleanup{handle};
        ShowWindow(handle,SW_SHOWNOACTIVATE);
        tools::ToolRegistry registry;
        QVERIFY(registry.add(std::make_unique<app::WindowTool>("move")));
        tools::ToolValidator validator(registry);
        const auto call=validator.validate(R"({"tool":"window_move","arguments":{"target":"JARVIS_WINDOW_TEST_9164389","monitor":1}})");
        QVERIFY(call); std::atomic<bool> cancelled{false};
        const auto result=registry.lookup("window_move")->execute(*call,cancelled);
        QVERIFY(result.ok());
        MONITORINFO monitor{sizeof(MONITORINFO)};
        QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle,MONITOR_DEFAULTTOPRIMARY),&monitor));
        QVERIFY(monitor.dwFlags & MONITORINFOF_PRIMARY);
        RECT rect{}; QVERIFY(GetWindowRect(handle,&rect));
        QCOMPARE(rect.left,monitor.rcWork.left);
        QCOMPARE(rect.top,monitor.rcWork.top);
    }
    void routing() {
        const auto command = jarvis::app::resolveWindowCommand(QStringLiteral("Жарвис, закрой все окна кроме Telegram"));
        const auto object = QJsonDocument::fromJson(command.toUtf8()).object();
        QCOMPARE(object["tool"].toString(),QStringLiteral("window_close_except"));
        QCOMPARE(object["arguments"].toObject()["target"].toString(),QStringLiteral("telegram"));
        const auto move = QJsonDocument::fromJson(jarvis::app::resolveWindowCommand(QStringLiteral("перенеси это окно на 2 моник")).toUtf8()).object();
        QCOMPARE(move["arguments"].toObject()["monitor"].toInt(),2);
        QVERIFY(jarvis::app::resolveWindowCommand(QStringLiteral("создай документ про окна")).isEmpty());
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveWindowCommand(QStringLiteral("закрой вкладку в гугле")).toUtf8()).object()["tool"].toString(),QStringLiteral("window_close_tab"));
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveBrowserCommand(QStringLiteral("напиши в гугле новый айфон")).toUtf8()).object()["tool"].toString(),QStringLiteral("browser_search"));
        const auto openLink=jarvis::app::resolveBrowserCommand(QStringLiteral("открой ссылку https://example.com/test"));
        QCOMPARE(QJsonDocument::fromJson(openLink.toUtf8()).object()["tool"].toString(),QStringLiteral("browser_open_url"));
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveBrowserCommand(QStringLiteral("введи в браузере привет мир")).toUtf8()).object()["tool"].toString(),QStringLiteral("browser_type"));
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveBrowserCommand(QStringLiteral("нажми enter")).toUtf8()).object()["tool"].toString(),QStringLiteral("browser_submit"));
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveBrowserCommand(QStringLiteral("открой первую ссылку")).toUtf8()).object()["arguments"].toObject()["index"].toInt(),1);
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveBrowserCommand(QStringLiteral("открой вторую ссылку")).toUtf8()).object()["arguments"].toObject()["index"].toInt(),2);
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveBrowserCommand(QStringLiteral("открой пятую ссылку")).toUtf8()).object()["arguments"].toObject()["index"].toInt(),5);
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveBrowserCommand(QStringLiteral("открой 6 ссылку")).toUtf8()).object()["arguments"].toObject()["index"].toInt(),6);
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveBrowserCommand(QStringLiteral("открой ссылку номер 6")).toUtf8()).object()["arguments"].toObject()["index"].toInt(),6);
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveBrowserCommand(QStringLiteral("открой ссылку номер один")).toUtf8()).object()["arguments"].toObject()["index"].toInt(),1);
        QCOMPARE(QJsonDocument::fromJson(jarvis::app::resolveBrowserCommand(QStringLiteral("зайди в первую ссылку")).toUtf8()).object()["arguments"].toObject()["index"].toInt(),1);
        const auto music=QJsonDocument::fromJson(jarvis::app::resolveMusicCommand(QStringLiteral("открой музыку")).toUtf8()).object();
        QCOMPARE(music["tool"].toString(),QStringLiteral("open_local_music"));
        const auto genericWithIntent=QJsonDocument::fromJson(jarvis::app::resolveMusicCommand(QStringLiteral("открой музыку ты и я хочу")).toUtf8()).object();
        QCOMPARE(genericWithIntent["tool"].toString(),QStringLiteral("open_local_music"));
        QVERIFY(!genericWithIntent["arguments"].toObject().contains("query"));
        const auto genericWithPunctuation=QJsonDocument::fromJson(jarvis::app::resolveMusicCommand(QStringLiteral("открой музыку хочу. армельки!")).toUtf8()).object();
        QVERIFY(!genericWithPunctuation["arguments"].toObject().contains("query"));
        const auto song=QJsonDocument::fromJson(jarvis::app::resolveMusicCommand(QStringLiteral("открой песню моя песня.mp3")).toUtf8()).object();
        QCOMPARE(song["arguments"].toObject()["query"].toString(),QStringLiteral("моя песня.mp3"));
        const auto file=QJsonDocument::fromJson(jarvis::app::resolveMusicCommand(QStringLiteral("открой конкретный файл трек.flac")).toUtf8()).object();
        QCOMPARE(file["arguments"].toObject()["query"].toString(),QStringLiteral("трек.flac"));
        const auto spotify=QJsonDocument::fromJson(jarvis::app::resolveSpotifyCommand(QStringLiteral("поищи в спотифай imagine dragons")).toUtf8()).object();
        QCOMPARE(spotify["tool"].toString(),QStringLiteral("spotify_search"));
        QCOMPARE(spotify["arguments"].toObject()["query"].toString(),QStringLiteral("imagine dragons"));
        const auto spotifySearch=QJsonDocument::fromJson(jarvis::app::resolveSpotifyCommand(QStringLiteral("напиши в поиске Spotify the weeknd")).toUtf8()).object();
        QCOMPARE(spotifySearch["arguments"].toObject()["query"].toString(),QStringLiteral("the weeknd"));
        const auto spotifyNatural=QJsonDocument::fromJson(jarvis::app::resolveSpotifyCommand(QStringLiteral("открой Spotify и найди billie eilish")).toUtf8()).object();
        QCOMPARE(spotifyNatural["arguments"].toObject()["query"].toString(),QStringLiteral("billie eilish"));
    }
    void permissionsAndMissingTarget() {
        using namespace jarvis;
        QCOMPARE(app::WindowTool("close_except").definition().permission,tools::PermissionLevel::ConfirmRequired);
        for (const auto* action : {"close", "close_tab", "new_tab", "maximize"})
            QCOMPARE(app::WindowTool(action).definition().permission,tools::PermissionLevel::SafeAction);
        tools::ToolRegistry registry;
        QVERIFY(registry.add(std::make_unique<app::WindowTool>("move")));
        tools::ToolValidator validator(registry);
        const auto call=validator.validate(R"({"tool":"window_move","arguments":{"target":"JARVIS_TEST_NONEXISTENT_9164389","monitor":2}})");
        QVERIFY(call); std::atomic<bool> cancelled{false};
        const auto result=registry.lookup("window_move")->execute(*call,cancelled);
        QVERIFY(!result.ok());
        cancelled=true;
        QCOMPARE(registry.lookup("window_move")->execute(*call,cancelled).errorCode(),tools::ToolErrorCode::Cancelled);
        QVERIFY(!validator.validate(R"({"tool":"window_move","arguments":{"target":"current","monitor":0}})"));
    }
};
QTEST_GUILESS_MAIN(TestWindows)
#include "tst_windows.moc"
