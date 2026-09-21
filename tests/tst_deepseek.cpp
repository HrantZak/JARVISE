#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "../src/app/DeepSeekBackend.h"
#include "../src/app/ResponsePolicy.h"
#include "../src/app/SpokenCommands.h"

class TestDeepSeek : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void conciseSpeech() {
        using jarvis::app::briefSpeech;
        QCOMPARE(briefSpeech(QStringLiteral("Вы спросили про GPU. Он обрабатывает графику. Дальше подробности.")),QStringLiteral("Он обрабатывает графику."));
        QCOMPARE(briefSpeech(QStringLiteral("Сколько будет два плюс два? Ответ — четыре.\n2 + 2 = 4"),QStringLiteral("Сколько будет два плюс два?")),QStringLiteral("Ответ — четыре."));
        QCOMPARE(briefSpeech(QStringLiteral("2 + 2 = 4\n```code```")),QStringLiteral("Подробности в чате."));
        QCOMPARE(briefSpeech(QStringLiteral("Открыто.")),QStringLiteral("Открыто."));
        QVERIFY(briefSpeech(QString(800,'x')).size()<=140);
    }
    void implicitReviewSearch() {
        for (const auto& phrase : {QStringLiteral("открой обзор на новый айфон"),
                                   QStringLiteral("Жарвис, пожалуйста, открой обзор на новый айфон"),
                                   QStringLiteral("хочу посмотреть обзор на новый айфон"),
                                   QStringLiteral("можешь открыть обзор на новый айфон")}) {
            const auto url = jarvis::app::spokenSearch(phrase);
            QCOMPARE(url.host(), QStringLiteral("www.youtube.com"));
            QCOMPARE(QUrlQuery(url).queryItemValue("search_query"), QStringLiteral("обзор на новый айфон"));
        }
        QCOMPARE(jarvis::app::spokenSearch(QStringLiteral("найди видео про тигра")).host(), QStringLiteral("www.youtube.com"));
        QCOMPARE(jarvis::app::spokenSearch(QStringLiteral("открой обзор в гугле на айфон")).host(), QStringLiteral("www.google.com"));
        for (const auto& phrase : {QStringLiteral("напиши обзор на айфон"), QStringLiteral("создай Word документ с обзором"),
                                   QStringLiteral("открой калькулятор")}) {
            QVERIFY(jarvis::app::spokenSearch(phrase).isEmpty());
        }
    }
    void wakeWordAndSearch() {
        jarvis::app::WakeGate gate;
        QVERIFY(!gate.consume(QStringLiteral("открой калькулятор"), 100).accepted);
        QVERIFY(gate.consume(QStringLiteral("Жарвис!"), 200).awakened);
        QVERIFY(gate.consume(QStringLiteral("открой калькулятор"), 300).accepted);
        QVERIFY(gate.consume(QStringLiteral("Джервис, открой Spotify"), 500).accepted);
        QVERIFY(!gate.consume(QStringLiteral("ещё команда"), 400).accepted);
        QCOMPARE(gate.consume(QStringLiteral("Джарвис, открой Telegram"), 500).command, QStringLiteral("открой Telegram"));
        QVERIFY(!gate.consume(QStringLiteral("Джарвисов компьютер"), 600).accepted);
        QVERIFY(gate.consume(QStringLiteral("Jarvis"), 1000).awakened);
        QVERIFY(!gate.consume(QStringLiteral("команда"), 17000).accepted);
        const auto youtube = jarvis::app::spokenSearch(QStringLiteral("хочу на ютубе посмотреть обзор на новый айфон"));
        QCOMPARE(youtube.host(), QStringLiteral("www.youtube.com"));
        QCOMPARE(QUrlQuery(youtube).queryItemValue("search_query"), QStringLiteral("обзор на новый айфон"));
        const auto google = jarvis::app::spokenSearch(QStringLiteral("информация про новый айфон"));
        QCOMPARE(google.host(), QStringLiteral("www.google.com"));
        QCOMPARE(QUrlQuery(google).queryItemValue("q"), QStringLiteral("новый айфон"));
        QVERIFY(jarvis::app::spokenSearch(QStringLiteral("привет")).isEmpty());
        const auto escaped = jarvis::app::spokenSearch(QStringLiteral("найди в гугле a&b #c"));
        QCOMPARE(QUrlQuery(escaped).queryItems().size(), 1);
        QVERIFY(escaped.fragment().isEmpty());
        QVERIFY(jarvis::app::briefSpeech(QString(800, QChar('x'))).size() < 400);
    }
    void stream_data() {
        QTest::addColumn<bool>("complete");
        QTest::addColumn<bool>("reasoning");
        QTest::newRow("fragmented-utf8-and-usage") << true << false;
        QTest::newRow("interrupted-stream-is-error") << false << false;
        QTest::newRow("reasoning-stays-out-of-answer") << true << true;
    }
    void stream() {
        QFETCH(bool, complete);
        QFETCH(bool, reasoning);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray captured;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                captured += socket->readAll();
                const auto split = captured.indexOf("\r\n\r\n");
                if (split < 0 || socket->property("sent").toBool()) return;
                int length = 0;
                for (const auto& line : captured.left(split).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (captured.size() < split + 4 + length) return;
                socket->setProperty("sent", true);
                QByteArray body = ": keepalive\n\ndata: {\"choices\":[{\"delta\":{\"content\":\"Привет\"},\"finish_reason\":null}]}\n\n"
                    "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\n"
                    "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":12,\"completion_tokens\":3}}\n\n";
                if (complete) body += "data: [DONE]\n\n";
                if (reasoning) body.prepend("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"private reasoning\"}}]}\n\n");
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n");
                const auto cut = body.indexOf(QStringLiteral("Привет").toUtf8()) + 1;
                socket->write(body.left(cut));
                QTimer::singleShot(10, socket, [socket, tail = body.mid(cut)] {
                    socket->write(tail);
                    socket->disconnectFromHost();
                });
            });
        });
        jarvis::app::DeepSeekBackend backend{"test-only-key", "deepseek-v4-flash",
            QUrl{QStringLiteral("http://127.0.0.1:%1/chat/completions").arg(server.serverPort())}};
        jarvis::llm::GenerationRequest request;
        request.reasoning = reasoning;
        request.messages = {{jarvis::llm::ChatMessage::Role::System, "Be brief"},
                            {jarvis::llm::ChatMessage::Role::User, "Hello"}};
        std::string output;
        const auto result = backend.generate(request, [&](std::string_view text) { output += text; return true; });
        QCOMPARE(result.has_value(), complete);
        QCOMPARE(QString::fromStdString(output), QStringLiteral("Привет"));
        QVERIFY(captured.contains("Authorization: Bearer test-only-key"));
        const auto body = QJsonDocument::fromJson(captured.mid(captured.indexOf("\r\n\r\n") + 4)).object();
        QCOMPARE(body.value("model").toString(), QStringLiteral("deepseek-v4-flash"));
        QCOMPARE(body.value("thinking").toObject().value("type").toString(), reasoning ? QStringLiteral("enabled") : QStringLiteral("disabled"));
        if (reasoning) { QVERIFY(!body.contains("temperature")); QCOMPARE(body.value("reasoning_effort").toString(), QStringLiteral("high")); }
        QCOMPARE(body.value("messages").toArray().first().toObject().value("role").toString(), QStringLiteral("system"));
        if (result) { QCOMPARE(result->promptTokens, 12); QCOMPARE(result->generatedTokens, 3); }
    }
    void balancedRouting() {
        using jarvis::app::needsReasoning;
        QVERIFY(!needsReasoning(QStringLiteral("Открой Блокнот"), "balanced"));
        QVERIFY(!needsReasoning(QStringLiteral("Привет, Джарвис"), "balanced"));
        QVERIFY(needsReasoning(QStringLiteral("Почему процессор занят и как это исправить?"), "balanced"));
        QVERIFY(needsReasoning(QStringLiteral("Compare these two implementations"), "balanced"));
        QVERIFY(!needsReasoning(QStringLiteral("Compare these two implementations"), "fast"));
        QVERIFY(needsReasoning(QStringLiteral("Hello"), "thorough"));
        QVERIFY(jarvis::app::assistantStyle(true).find("сэр") != std::string::npos);
        QVERIFY(jarvis::app::assistantStyle(false).find("сэр") == std::string::npos);
    }
    void authenticationFailureDoesNotLeakTheServerBody() {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            auto* socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                if (socket->property("sent").toBool()) return;
                socket->setProperty("sent", true);
                const QByteArray body{"test-secret-must-not-appear"};
                socket->write("HTTP/1.1 401 Unauthorized\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        jarvis::app::DeepSeekBackend backend{"test-secret-must-not-appear", "deepseek-v4-flash",
            QUrl{QStringLiteral("http://127.0.0.1:%1/chat/completions").arg(server.serverPort())}};
        const auto result = backend.generate({}, [](std::string_view) { return true; });
        QVERIFY(!result);
        QVERIFY(result.error().message().find("401") != std::string::npos);
        QVERIFY(result.error().message().find("test-secret") == std::string::npos);
    }
};
QTEST_GUILESS_MAIN(TestDeepSeek)
#include "tst_deepseek.moc"
