#include <QtTest>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include "jarvis/tools/CreateArtifactTool.h"
#include "jarvis/tools/ToolRegistry.h"
#include "jarvis/tools/ToolValidator.h"
using namespace jarvis::tools;
class TestArtifacts:public QObject {
 Q_OBJECT
private slots:
 void createAndProtect() {
    QTemporaryDir root; ToolRegistry registry;
    for(auto kind:{CreateArtifactTool::Kind::Folder,CreateArtifactTool::Kind::Text,CreateArtifactTool::Kind::Word}) QVERIFY(registry.add(std::make_unique<CreateArtifactTool>(kind,root.path())));
    ToolValidator validator(registry); std::atomic<bool> cancel{false};
    const auto run=[&](QString tool,QJsonObject args) {
        const auto call=validator.validate(QJsonDocument(QJsonObject{{"tool",tool},{"arguments",args}}).toJson().toStdString());
        if(!call)return ToolResult::failure(tool.toStdString(),call.error().code,call.error().message);
        return registry.lookup(call->toolName())->execute(*call,cancel);
    };
    QVERIFY(run("create_folder",{{"name",QStringLiteral("Проекты")}}).ok());
    const QString code=QStringLiteral("print('Привет, мир!')\n");
    QVERIFY(run("create_text_file",{{"name",QStringLiteral("Проекты/hello.py")},{"content",code}}).ok());
    QFile source(root.filePath(QStringLiteral("Проекты/hello.py"))); QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(),code.toUtf8()); source.close();
    QVERIFY(!run("create_text_file",{{"name",QStringLiteral("Проекты/hello.py")},{"content","replaced"}}).ok());
    QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(),code.toUtf8()); source.close();
    for(const QString name:{QStringLiteral("../escape.txt"),QStringLiteral("C:/escape.txt"),QStringLiteral("NUL.txt"),QStringLiteral("file:stream"),QStringLiteral("folder/../escape.txt")}) QVERIFY(!run("create_text_file",{{"name",name},{"content","data"}}).ok());
    const QString content=QStringLiteral("Тигр — крупная хищная кошка. Его полосатая окраска помогает скрываться среди растительности.\n# Образ жизни\nТигры обычно живут поодиночке. Они хорошо плавают и используют большую территорию для поиска пищи.\n# Особенности\n- Полосы каждого тигра образуют свой узор.\n- Для охоты важны слух и зрение.\n# Почему тиграм нужна защита\nСохранение лесов и других мест обитания помогает тиграм находить пищу и выращивать потомство. Борьба с браконьерством также важна для их сохранения.");
    const auto word=run("create_word_document",{{"name",QStringLiteral("Тигр.docx")},{"title",QStringLiteral("Тигр: жизнь полосатого хищника")},{"content",content}});
    QVERIFY2(word.ok(),word.errorMessage().c_str());
    QFile doc(root.filePath(QStringLiteral("Тигр.docx"))); QVERIFY(doc.open(QIODevice::ReadOnly));const auto bytes=doc.readAll();
    QVERIFY(bytes.startsWith("PK\x03\x04")); QVERIFY(bytes.contains("word/document.xml")); QVERIFY(bytes.contains("word/styles.xml"));
    const QString sample=qEnvironmentVariable("JARVIS_DOCX_SAMPLE");
    if(!sample.isEmpty()){QFile output(sample);QVERIFY(output.open(QIODevice::WriteOnly));QCOMPARE(output.write(bytes),bytes.size());}
    cancel=true; QVERIFY(!run("create_folder",{{"name","cancelled"}}).ok()); QVERIFY(!QDir(root.filePath("cancelled")).exists());
 }
};
QTEST_GUILESS_MAIN(TestArtifacts)
#include "tst_artifacts.moc"
