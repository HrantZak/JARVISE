#include <QCoreApplication>
#include <QDir>
#include <QFontDatabase>
#include <QRawFont>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTranslator>

#include <memory>

#include "jarvis/config/AppPaths.h"
#include "jarvis/config/ConfigStore.h"
#include "jarvis/i18n/Language.h"
#include "jarvis/i18n/LanguagePolicy.h"

/// Typography and localisation guarantees.
///
/// The font checks are not decoration: JARVIS ships a Cyrillic-first interface,
/// and a display face without Cyrillic coverage would silently fall back to a
/// different family mid-sentence. That must fail the build, not the user's eye.
class TestI18n : public QObject {
    Q_OBJECT

private slots:
    // Typography
    void displayFontCoversCyrillic();
    void monoFontCoversCyrillic();
    void bodyFontCoversCyrillic();
    void everyRussianLetterHasAGlyph();
    void punctuationUsedByTheInterfaceHasGlyphs();

    // Language identity
    void languageCodesRoundTrip();
    void localeVariantsAreAccepted();
    void unknownCodesAreRejected();
    void russianIsTheDefault();

    // Catalogues
    void bothCataloguesAreEmbedded();
    void russianCatalogueTranslatesInterfaceStrings();
    void russianCatalogueTranslatesCoreStates();
    void switchingLanguageBackAndForthIsClean();
    void technicalNamesAreNeverTranslated();

    // The model-facing language rules
    void systemPromptNamesTheReplyLanguage();
    void systemPromptProtectsProperNames();
    void systemPromptDiffersPerLanguage();

    // UTF-8
    void configRoundTripsCyrillicValues();
    void configLivesUnderACyrillicPath();
    void sourceLiteralsSurviveTheToolchain();

private:
    static void requireCyrillic(const QString& family);
    static void requireGlyphs(const QString& family, const QString& text);

    /// Installs a catalogue and returns it; the caller keeps it alive.
    static std::unique_ptr<QTranslator> install(jarvis::i18n::Language language);
};

void TestI18n::requireCyrillic(const QString& family) {
    QVERIFY2(QFontDatabase::families().contains(family),
             qPrintable(QStringLiteral("font family '%1' is not installed").arg(family)));

    const QList<QFontDatabase::WritingSystem> systems =
        QFontDatabase::writingSystems(family);

    QVERIFY2(systems.contains(QFontDatabase::Cyrillic),
             qPrintable(QStringLiteral("font family '%1' does not declare Cyrillic support")
                            .arg(family)));
}

void TestI18n::requireGlyphs(const QString& family, const QString& text) {
    QRawFont font = QRawFont::fromFont(QFont{family, 14});
    QVERIFY2(font.isValid(), qPrintable(QStringLiteral("no raw font for '%1'").arg(family)));

    QString missing;
    for (const QChar ch : text) {
        if (!font.supportsCharacter(ch)) {
            missing.append(ch);
        }
    }

    QVERIFY2(missing.isEmpty(),
             qPrintable(QStringLiteral("font '%1' is missing glyphs for: %2")
                            .arg(family, missing)));
}

void TestI18n::displayFontCoversCyrillic() {
    requireCyrillic(QStringLiteral("Bahnschrift"));
}

void TestI18n::monoFontCoversCyrillic() {
    requireCyrillic(QStringLiteral("Cascadia Mono"));
}

void TestI18n::bodyFontCoversCyrillic() {
    requireCyrillic(QStringLiteral("Segoe UI"));
}

void TestI18n::everyRussianLetterHasAGlyph() {
    const QString alphabet = QStringLiteral(
        "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ"
        "абвгдеёжзийклмнопрстуфхцчшщъыьэюя");

    for (const QString& family : {QStringLiteral("Bahnschrift"),
                                  QStringLiteral("Cascadia Mono"),
                                  QStringLiteral("Segoe UI")}) {
        requireGlyphs(family, alphabet);
    }
}

void TestI18n::punctuationUsedByTheInterfaceHasGlyphs() {
    // Ё is the letter most often missing from a font that claims Cyrillic
    // coverage, and Russian text needs the numero sign, an em dash and guillemets.
    const QString russianPunctuation = QStringLiteral("Ёё№—«»·");

    for (const QString& family : {QStringLiteral("Bahnschrift"),
                                  QStringLiteral("Cascadia Mono"),
                                  QStringLiteral("Segoe UI")}) {
        requireGlyphs(family, russianPunctuation);
    }

    // The HUD draws throughput arrows in the mono face and the warning marker in
    // the body face. Bahnschrift has no triangles, which is why neither of those
    // is ever set in the display face.
    requireGlyphs(QStringLiteral("Cascadia Mono"), QStringLiteral("▼▲"));
    requireGlyphs(QStringLiteral("Segoe UI"), QStringLiteral("▲"));

    QRawFont display = QRawFont::fromFont(QFont{QStringLiteral("Bahnschrift"), 14});
    QVERIFY2(!display.supportsCharacter(QChar{u'▼'}),
             "Bahnschrift gained triangles; the arrow-font rule can be relaxed");
}

// ---------------------------------------------------------------------------
// Language identity
// ---------------------------------------------------------------------------

using jarvis::i18n::Language;

void TestI18n::languageCodesRoundTrip() {
    for (const Language language : jarvis::i18n::kSupportedLanguages) {
        const std::string_view code = jarvis::i18n::languageCode(language);
        const auto parsed = jarvis::i18n::languageFromCode(code);
        QVERIFY2(parsed.has_value(), std::string{code}.c_str());
        QVERIFY(*parsed == language);

        QVERIFY(!jarvis::i18n::languageEndonym(language).empty());
        QVERIFY(!jarvis::i18n::qtLocaleName(language).empty());
    }

    // The endonym is the language's own name for itself, never a translation.
    QCOMPARE(QString::fromUtf8(std::string{
                 jarvis::i18n::languageEndonym(Language::Russian)}.c_str()),
             QStringLiteral("Русский"));
    QCOMPARE(QString::fromUtf8(std::string{
                 jarvis::i18n::languageEndonym(Language::English)}.c_str()),
             QStringLiteral("English"));
}

void TestI18n::localeVariantsAreAccepted() {
    // All four spellings turn up: in config files, in Qt locale names, and in
    // whatever a user types by hand.
    for (const char* spelling : {"ru", "RU", "ru_RU", "ru-RU"}) {
        const auto parsed = jarvis::i18n::languageFromCode(spelling);
        QVERIFY2(parsed.has_value(), spelling);
        QVERIFY(*parsed == Language::Russian);
    }

    QVERIFY(jarvis::i18n::languageFromCode("en_US").value() == Language::English);
}

void TestI18n::unknownCodesAreRejected() {
    // A typo must be reported, not silently swallowed into the default.
    for (const char* code : {"", "xx", "russian", "de", "ru!"}) {
        QVERIFY2(!jarvis::i18n::languageFromCode(code).has_value(), code);
    }
}

void TestI18n::russianIsTheDefault() {
    QVERIFY(jarvis::i18n::kDefaultLanguage == Language::Russian);
    QCOMPARE(jarvis::i18n::translationCatalogue(Language::Russian),
             std::string{"jarvis_ru"});
}

// ---------------------------------------------------------------------------
// Catalogues
// ---------------------------------------------------------------------------

std::unique_ptr<QTranslator> TestI18n::install(Language language) {
    auto translator = std::make_unique<QTranslator>();
    const QString catalogue =
        QString::fromStdString(jarvis::i18n::translationCatalogue(language));
    if (!translator->load(catalogue, QStringLiteral(":/i18n"))) {
        return nullptr;
    }
    QCoreApplication::installTranslator(translator.get());
    return translator;
}

void TestI18n::bothCataloguesAreEmbedded() {
    // The .qm files must be inside the binary: a portable build carries its
    // languages with it rather than depending on files next to the exe.
    for (const Language language : jarvis::i18n::kSupportedLanguages) {
        const QString path =
            QStringLiteral(":/i18n/%1.qm")
                .arg(QString::fromStdString(jarvis::i18n::translationCatalogue(language)));
        QVERIFY2(QFile::exists(path), qPrintable(path));
    }
}

void TestI18n::russianCatalogueTranslatesInterfaceStrings() {
    auto translator = install(Language::Russian);
    QVERIFY2(translator != nullptr, "the Russian catalogue failed to load");

    QCOMPARE(QCoreApplication::translate("Main", "HOME"), QStringLiteral("ГЛАВНАЯ"));
    QCOMPARE(QCoreApplication::translate("Main", "SETTINGS"), QStringLiteral("НАСТРОЙКИ"));
    QCOMPARE(QCoreApplication::translate("SystemPage", "OPERATING SYSTEM"),
             QStringLiteral("ОПЕРАЦИОННАЯ СИСТЕМА"));
    QCOMPARE(QCoreApplication::translate("jarvis::app::SystemMonitor", "GB"),
             QStringLiteral("ГБ"));

    QCoreApplication::removeTranslator(translator.get());
}

void TestI18n::russianCatalogueTranslatesCoreStates() {
    auto translator = install(Language::Russian);
    QVERIFY(translator != nullptr);

    const char* context = "jarvis::app::AiCoreModel";
    QCOMPARE(QCoreApplication::translate(context, "OFFLINE"), QStringLiteral("НЕ В СЕТИ"));
    QCOMPARE(QCoreApplication::translate(context, "LISTENING"), QStringLiteral("СЛУШАЮ"));
    QCOMPARE(QCoreApplication::translate(context, "THINKING"), QStringLiteral("ОБРАБАТЫВАЮ"));
    QCOMPARE(QCoreApplication::translate(context, "EXECUTING"), QStringLiteral("ВЫПОЛНЯЮ"));
    QCOMPARE(QCoreApplication::translate(context, "SPEAKING"), QStringLiteral("ОТВЕЧАЮ"));

    QCoreApplication::removeTranslator(translator.get());
}

void TestI18n::switchingLanguageBackAndForthIsClean() {
    // Switching must not leave a stale catalogue installed underneath.
    auto russian = install(Language::Russian);
    QVERIFY(russian != nullptr);
    QCOMPARE(QCoreApplication::translate("Main", "SYSTEM"), QStringLiteral("СИСТЕМА"));

    QCoreApplication::removeTranslator(russian.get());
    auto english = install(Language::English);
    QVERIFY(english != nullptr);
    QCOMPARE(QCoreApplication::translate("Main", "SYSTEM"), QStringLiteral("SYSTEM"));

    QCoreApplication::removeTranslator(english.get());
    russian = install(Language::Russian);
    QVERIFY(russian != nullptr);
    QCOMPARE(QCoreApplication::translate("Main", "SYSTEM"), QStringLiteral("СИСТЕМА"));

    QCoreApplication::removeTranslator(russian.get());

    // With nothing installed, the source text comes back.
    QCOMPARE(QCoreApplication::translate("Main", "SYSTEM"), QStringLiteral("SYSTEM"));
}

void TestI18n::technicalNamesAreNeverTranslated() {
    auto translator = install(Language::Russian);
    QVERIFY(translator != nullptr);

    // Proper nouns and technical abbreviations pass through unchanged, so the
    // application launcher and the model manager keep matching on them.
    QCOMPARE(QCoreApplication::translate("Main", "JARVIS"), QStringLiteral("JARVIS"));
    QCOMPARE(QCoreApplication::translate("ModelsPage", "GPU"), QStringLiteral("GPU"));

    QCoreApplication::removeTranslator(translator.get());
}

// ---------------------------------------------------------------------------
// The model-facing language rules
// ---------------------------------------------------------------------------

void TestI18n::systemPromptNamesTheReplyLanguage() {
    const jarvis::i18n::LanguagePolicy russian{Language::Russian};
    QCOMPARE(QString::fromUtf8(std::string{russian.replyLanguageName()}.c_str()),
             QStringLiteral("Russian"));

    const std::string prompt = russian.systemPromptSection();
    QVERIFY2(prompt.find("Reply in Russian by default.") != std::string::npos,
             prompt.c_str());
}

void TestI18n::systemPromptProtectsProperNames() {
    const jarvis::i18n::LanguagePolicy policy{Language::Russian};
    const std::string prompt = policy.systemPromptSection();

    // Without this rule models translate "Visual Studio" and the launcher can
    // no longer match anything.
    QVERIFY(prompt.find("Never translate proper names") != std::string::npos);
    QVERIFY(prompt.find("Application names") != std::string::npos);
    QVERIFY(prompt.find("CPU, GPU, RAM, VRAM") != std::string::npos);

    // Comprehension is stated independently of the reply language.
    QVERIFY(prompt.find("Understand Russian and English equally well") !=
            std::string::npos);
}

void TestI18n::systemPromptDiffersPerLanguage() {
    const jarvis::i18n::LanguagePolicy russian{Language::Russian};
    const jarvis::i18n::LanguagePolicy english{Language::English};
    QVERIFY(russian.systemPromptSection() != english.systemPromptSection());
    QVERIFY(english.systemPromptSection().find("Reply in English by default.") !=
            std::string::npos);
}

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

void TestI18n::configRoundTripsCyrillicValues() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const std::filesystem::path file =
        std::filesystem::path{dir.path().toStdWString()} / "config.json";

    jarvis::config::ConfigStore store{file};

    jarvis::config::AppConfig written;
    written.general.language = Language::Russian;
    QVERIFY(store.save(written).has_value());

    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QVERIFY(loaded->config.general.language == Language::Russian);

    // The file itself must be UTF-8 and must not have mangled the code.
    QFile raw{QString::fromStdWString(file.wstring())};
    QVERIFY(raw.open(QIODevice::ReadOnly));
    const QByteArray bytes = raw.readAll();
    QVERIFY(bytes.contains("\"language\""));
    QVERIFY(bytes.contains("\"ru\""));
}

void TestI18n::configLivesUnderACyrillicPath() {
    // Users have Cyrillic account names; %LOCALAPPDATA% then contains Cyrillic
    // and every path in JARVIS has to survive it.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const std::filesystem::path root =
        std::filesystem::path{dir.path().toStdWString()} / L"Пользователь Ёлка" / L"ЯДРО";

    const auto paths = jarvis::config::AppPaths::resolveWithRoot(root);
    QVERIFY(paths.has_value());
    QVERIFY2(paths->ensureDirectories().has_value(),
             "directories with Cyrillic names must be creatable");
    QVERIFY(std::filesystem::is_directory(paths->logDirectory()));
    QVERIFY(std::filesystem::is_directory(paths->modelDirectory()));

    jarvis::config::ConfigStore store{paths->configFile()};
    jarvis::config::AppConfig config;
    QVERIFY2(store.save(config).has_value(), "saving under a Cyrillic path must work");

    const auto loaded = store.load();
    QVERIFY(loaded.has_value());
    QVERIFY(std::filesystem::exists(paths->configFile()));
}

void TestI18n::sourceLiteralsSurviveTheToolchain() {
    // /utf-8 is set project-wide. If it were ever dropped, MSVC would read
    // these literals as the ANSI code page and this comparison would fail.
    const QString endonym = QString::fromUtf8(
        std::string{jarvis::i18n::languageEndonym(Language::Russian)}.c_str());

    QCOMPARE(endonym.size(), 7);
    QCOMPARE(endonym.at(0), QChar{u'Р'});
    QCOMPARE(endonym, QStringLiteral("Русский"));

    // Ё must survive both the source encoding and the case conversion that the
    // interface applies to labels.
    const QString yo = QStringLiteral("ёлка");
    QCOMPARE(yo.toUpper(), QStringLiteral("ЁЛКА"));
}

QTEST_MAIN(TestI18n)

#include "tst_i18n.moc"
