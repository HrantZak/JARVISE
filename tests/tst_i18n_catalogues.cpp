// The translation catalogues, and the encoding of every source file.
//
// Two failures motivated this test, and both were silent.
//
// The first: a string was added to the interface and only one catalogue was
// updated, so the other language fell back to the English source. Nothing
// failed - the application ran, the tests passed, and the interface was simply
// wrong in one language.
//
// The second was worse. UTF-8 text was read as Latin-1 and written back out, so
// each byte of a Russian word became its own Latin-1 character and one word
// became two lines of noise. The file still parsed, the build stayed green, and
// the only symptom was a model answering questions nobody had asked. It reached
// a live test before anyone noticed, and the repair tool written to fix it was
// itself broken in a way that reported 461 damaged strings as clean.
//
// The damaged form is deliberately not written out here: this file is one of
// the files the scan below reads, and an example would be indistinguishable
// from the real thing. theDetectorRecognisesRealDamage() builds one at runtime
// instead.
//
// So this checks the files themselves, from the source tree, byte by byte:
// every message present in both catalogues, every translation filled in, and no
// file anywhere in the project carrying the signature of that corruption.

#include <QDirIterator>
#include <QFile>
#include <QSet>
#include <QTest>
#include <QXmlStreamReader>

#include <utility>

#ifndef JARVIS_SOURCE_DIR
#error "JARVIS_SOURCE_DIR must be defined - the test reads the source tree"
#endif

namespace {

/// A message's identity: the context it belongs to and the untranslated text.
/// Qt keys messages on exactly this pair, so two contexts may hold the same
/// source string and mean different things.
using Key = std::pair<QString, QString>;

struct Catalogue {
    QSet<Key> keys;
    QStringList untranslated;   ///< sources with an empty or unfinished translation
    int messages{0};
    bool hasCyrillic{false};
};

QString sourcePath(const char* relative) {
    return QStringLiteral(JARVIS_SOURCE_DIR) + QLatin1Char{'/'} + QLatin1String{relative};
}

Catalogue readCatalogue(const QString& path, QString* error) {
    Catalogue catalogue;

    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1").arg(path);
        return catalogue;
    }

    QXmlStreamReader xml{&file};
    QString context;

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }

        if (xml.name() == QLatin1String{"name"}) {
            context = xml.readElementText();
            continue;
        }
        if (xml.name() != QLatin1String{"message"}) {
            continue;
        }

        QString source;
        QString translation;
        QString type;

        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isEndElement() && xml.name() == QLatin1String{"message"}) {
                break;
            }
            if (!xml.isStartElement()) {
                continue;
            }
            if (xml.name() == QLatin1String{"source"}) {
                source = xml.readElementText();
            } else if (xml.name() == QLatin1String{"translation"}) {
                type = xml.attributes().value(QLatin1String{"type"}).toString();
                translation = xml.readElementText();
            }
        }

        if (source.isEmpty()) {
            continue;
        }

        ++catalogue.messages;
        catalogue.keys.insert(Key{context, source});

        // "unfinished" is what lupdate writes for a new string, and what
        // Linguist leaves behind when a translation is deleted. "vanished" and
        // "obsolete" mark strings the sources no longer use; -no-obsolete is
        // meant to keep them out, and their presence means the catalogue was
        // hand-edited rather than regenerated.
        if (translation.isEmpty() || !type.isEmpty()) {
            catalogue.untranslated
                << QStringLiteral("%1 / %2%3")
                       .arg(context, source,
                            type.isEmpty() ? QString{}
                                           : QStringLiteral(" [%1]").arg(type));
        }

        for (const QChar ch : translation) {
            if (ch.unicode() >= 0x0400 && ch.unicode() <= 0x04FF) {
                catalogue.hasCyrillic = true;
                break;
            }
        }
    }

    if (xml.hasError()) {
        *error = QStringLiteral("%1: %2").arg(path, xml.errorString());
    }
    return catalogue;
}

// --- mojibake detection ----------------------------------------------------
//
// The same algorithm scripts/repair_mojibake.py uses, so the test and the
// repair tool agree on what damage is. A run of characters that could have come
// from single bytes is mapped back to those bytes; if the bytes are valid
// multi-byte UTF-8, the run is text that was decoded with the wrong codec.

/// The byte a character came from, or -1 when it cannot have come from one.
int originatingByte(char16_t ch) {
    // CP1252 fills 0x80-0x9F with printable characters. A run can mix these
    // with raw C1 controls from the bytes CP1252 leaves undefined, which is why
    // the mapping is per character rather than per run.
    switch (ch) {
        case 0x20AC: return 0x80;
        case 0x201A: return 0x82;
        case 0x0192: return 0x83;
        case 0x201E: return 0x84;
        case 0x2026: return 0x85;
        case 0x2020: return 0x86;
        case 0x2021: return 0x87;
        case 0x02C6: return 0x88;
        case 0x2030: return 0x89;
        case 0x0160: return 0x8A;
        case 0x2039: return 0x8B;
        case 0x0152: return 0x8C;
        case 0x017D: return 0x8E;
        case 0x2018: return 0x91;
        case 0x2019: return 0x92;
        case 0x201C: return 0x93;
        case 0x201D: return 0x94;
        case 0x2022: return 0x95;
        case 0x2013: return 0x96;
        case 0x2014: return 0x97;
        case 0x02DC: return 0x98;
        case 0x2122: return 0x99;
        case 0x0161: return 0x9A;
        case 0x203A: return 0x9B;
        case 0x0153: return 0x9C;
        case 0x017E: return 0x9E;
        case 0x0178: return 0x9F;
        default: break;
    }
    return (ch >= 0x0080 && ch <= 0x00FF) ? static_cast<int>(ch) : -1;
}

/// True when \p bytes is valid UTF-8 containing at least one multi-byte
/// sequence. A run of single bytes is just accented text that belongs.
bool isMultiByteUtf8(const QByteArray& bytes) {
    bool sawMultiByte = false;

    for (int i = 0; i < bytes.size();) {
        const auto lead = static_cast<unsigned char>(bytes.at(i));
        int length = 0;

        if (lead < 0x80) {
            length = 1;
        } else if ((lead & 0xE0) == 0xC0 && lead >= 0xC2) {
            length = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3;
        } else if ((lead & 0xF8) == 0xF0 && lead <= 0xF4) {
            length = 4;
        } else {
            return false;
        }

        if (i + length > bytes.size()) {
            return false;
        }
        for (int k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(bytes.at(i + k)) & 0xC0) != 0x80) {
                return false;
            }
        }

        sawMultiByte = sawMultiByte || length > 1;
        i += length;
    }

    return sawMultiByte;
}

/// Every damaged run in \p text, as the text it should have been.
QStringList damagedRuns(const QString& text) {
    QStringList found;

    for (int i = 0; i < text.size();) {
        if (originatingByte(text.at(i).unicode()) < 0) {
            ++i;
            continue;
        }

        QByteArray bytes;
        const int start = i;
        while (i < text.size()) {
            const int byte = originatingByte(text.at(i).unicode());
            if (byte < 0) {
                break;
            }
            bytes.append(static_cast<char>(byte));
            ++i;
        }

        if (isMultiByteUtf8(bytes)) {
            found << QStringLiteral("'%1' should be '%2'")
                         .arg(text.mid(start, i - start),
                              QString::fromUtf8(bytes));
        }
    }

    return found;
}

} // namespace

class TestI18nCatalogues : public QObject {
    Q_OBJECT

private slots:
    void bothCataloguesCoverExactlyTheSameStrings();
    void everyStringIsTranslatedInBothLanguages();
    void theRussianCatalogueIsActuallyInRussian();
    void neitherCatalogueIsDamaged();
    void noSourceFileIsDamaged();
    void theDetectorRecognisesRealDamage();
};

void TestI18nCatalogues::bothCataloguesCoverExactlyTheSameStrings() {
    QString error;
    const Catalogue ru = readCatalogue(sourcePath("i18n/jarvis_ru.ts"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const Catalogue en = readCatalogue(sourcePath("i18n/jarvis_en.ts"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    QVERIFY2(ru.messages > 0, "the Russian catalogue is empty");

    // Set equality both ways, and each direction names what is missing. A key
    // in one catalogue and not the other means one language silently falls back
    // to the untranslated source.
    QStringList missingFromEnglish;
    for (const Key& key : ru.keys) {
        if (!en.keys.contains(key)) {
            missingFromEnglish << QStringLiteral("%1 / %2").arg(key.first, key.second);
        }
    }
    QStringList missingFromRussian;
    for (const Key& key : en.keys) {
        if (!ru.keys.contains(key)) {
            missingFromRussian << QStringLiteral("%1 / %2").arg(key.first, key.second);
        }
    }

    QVERIFY2(missingFromEnglish.isEmpty(),
             qPrintable(QStringLiteral("absent from jarvis_en.ts:\n  %1")
                            .arg(missingFromEnglish.join(QStringLiteral("\n  ")))));
    QVERIFY2(missingFromRussian.isEmpty(),
             qPrintable(QStringLiteral("absent from jarvis_ru.ts:\n  %1")
                            .arg(missingFromRussian.join(QStringLiteral("\n  ")))));

    QCOMPARE(ru.messages, en.messages);
}

void TestI18nCatalogues::everyStringIsTranslatedInBothLanguages() {
    QString error;
    const Catalogue ru = readCatalogue(sourcePath("i18n/jarvis_ru.ts"), &error);
    const Catalogue en = readCatalogue(sourcePath("i18n/jarvis_en.ts"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    // The fix when this fails is two commands, and they are worth naming here
    // rather than leaving to memory.
    const QString remedy = QStringLiteral(
        "\n  cmake --build build/msvc-debug --target update_translations"
        "\n  python scripts/fill_translations.py");

    QVERIFY2(ru.untranslated.isEmpty(),
             qPrintable(QStringLiteral("untranslated in jarvis_ru.ts:\n  %1%2")
                            .arg(ru.untranslated.join(QStringLiteral("\n  ")), remedy)));
    QVERIFY2(en.untranslated.isEmpty(),
             qPrintable(QStringLiteral("untranslated in jarvis_en.ts:\n  %1%2")
                            .arg(en.untranslated.join(QStringLiteral("\n  ")), remedy)));
}

void TestI18nCatalogues::theRussianCatalogueIsActuallyInRussian() {
    // A catalogue whose translations were all replaced by their English sources
    // would pass every check above. JARVIS is a Russian-first application; if
    // the Russian catalogue holds no Cyrillic at all, something replaced it.
    QString error;
    const Catalogue ru = readCatalogue(sourcePath("i18n/jarvis_ru.ts"), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(ru.hasCyrillic, "the Russian catalogue contains no Cyrillic at all");
}

void TestI18nCatalogues::neitherCatalogueIsDamaged() {
    for (const char* relative : {"i18n/jarvis_ru.ts", "i18n/jarvis_en.ts"}) {
        QFile file{sourcePath(relative)};
        QVERIFY2(file.open(QIODevice::ReadOnly), relative);

        const QStringList damage = damagedRuns(QString::fromUtf8(file.readAll()));
        QVERIFY2(damage.isEmpty(),
                 qPrintable(QStringLiteral("%1 carries %2 damaged run(s):\n  %3")
                                .arg(QLatin1String{relative})
                                .arg(damage.size())
                                .arg(damage.mid(0, 10).join(QStringLiteral("\n  ")))));
    }
}

void TestI18nCatalogues::noSourceFileIsDamaged() {
    // Every file a person edits and a compiler reads. The damage was introduced
    // by tooling that rewrote source files, so restricting this to the
    // catalogues would miss where it actually happened: it was in the tests and
    // in SystemTools.cpp too.
    const QStringList roots{QStringLiteral("src"), QStringLiteral("tests"),
                            QStringLiteral("ui"), QStringLiteral("scripts"),
                            QStringLiteral("docs")};
    const QStringList patterns{QStringLiteral("*.cpp"), QStringLiteral("*.h"),
                               QStringLiteral("*.qml"), QStringLiteral("*.py"),
                               QStringLiteral("*.md")};

    QStringList failures;
    int scanned = 0;

    for (const QString& root : roots) {
        QDirIterator files{sourcePath(qPrintable(root)), patterns, QDir::Files,
                           QDirIterator::Subdirectories};
        while (files.hasNext()) {
            const QString path = files.next();

            // The repair tool documents the damage it repairs, so its docstring
            // contains a genuine damaged run on purpose. Excluding the file by
            // name is honest; teaching the detector to ignore "examples" would
            // not be.
            if (path.endsWith(QLatin1String{"repair_mojibake.py"})) {
                continue;
            }

            QFile file{path};
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            ++scanned;

            const QStringList damage = damagedRuns(QString::fromUtf8(file.readAll()));
            if (!damage.isEmpty()) {
                failures << QStringLiteral("%1: %2 run(s), first: %3")
                                .arg(path)
                                .arg(damage.size())
                                .arg(damage.first());
            }
        }
    }

    QVERIFY2(scanned > 50,
             qPrintable(QStringLiteral("only %1 files were scanned; the source "
                                       "tree was not found")
                            .arg(scanned)));
    QVERIFY2(failures.isEmpty(),
             qPrintable(QStringLiteral("%1 damaged file(s):\n  %2\nRepair with:"
                                       "\n  python scripts/repair_mojibake.py <file>")
                            .arg(failures.size())
                            .arg(failures.join(QStringLiteral("\n  ")))));
}

void TestI18nCatalogues::theDetectorRecognisesRealDamage() {
    // A detector that finds nothing is indistinguishable from a clean tree, and
    // that is exactly how the first version of the repair script passed 461
    // damaged strings. This pins it to the real corruption of a real string.
    // One word, no spaces: a run ends at the first character that cannot have
    // come from a single byte, and an ASCII space is one of those.
    const QString clean = QStringLiteral("Сколько");

    const QByteArray utf8 = clean.toUtf8();
    QString damaged;
    for (const char byte : utf8) {
        damaged.append(QChar{static_cast<char16_t>(static_cast<unsigned char>(byte))});
    }
    QCOMPARE(damaged.size(), utf8.size());  // 7 characters became 14
    QVERIFY(damaged != clean);

    const QStringList found = damagedRuns(damaged);
    QCOMPARE(found.size(), 1);
    QVERIFY2(found.first().contains(clean),
             qPrintable(QStringLiteral("the detector did not recover the original: %1")
                            .arg(found.first())));

    // And it leaves alone text that merely contains accented characters, or the
    // C1 range would make every European string a false positive.
    QVERIFY(damagedRuns(clean).isEmpty());
    QVERIFY(damagedRuns(QStringLiteral("café - naïve - Ünal")).isEmpty());
    QVERIFY(damagedRuns(QStringLiteral("plain ASCII only")).isEmpty());
}

QTEST_MAIN(TestI18nCatalogues)

#include "tst_i18n_catalogues.moc"
