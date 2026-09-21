#include "TranslationManager.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QTranslator>

#include "jarvis/logging/Logger.h"

namespace jarvis::app {
namespace {

constexpr std::string_view kCategory = "i18n";
constexpr QLatin1StringView kResourcePrefix{":/i18n"};

QString toQString(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

TranslationManager::TranslationManager(QObject* parent)
    : QObject{parent} {}

TranslationManager::~TranslationManager() {
    removeCurrent();
}

void TranslationManager::setEngine(QQmlApplicationEngine* engine) {
    m_engine = engine;
}

void TranslationManager::removeCurrent() {
    if (m_appTranslator) {
        QCoreApplication::removeTranslator(m_appTranslator.get());
        m_appTranslator.reset();
    }
    if (m_qtTranslator) {
        QCoreApplication::removeTranslator(m_qtTranslator.get());
        m_qtTranslator.reset();
    }
    m_installed = false;
}

bool TranslationManager::apply(i18n::Language language) {
    const QString catalogue = toQString(i18n::translationCatalogue(language));
    const QString locale = toQString(i18n::qtLocaleName(language));

    auto appTranslator = std::make_unique<QTranslator>();
    if (!appTranslator->load(catalogue, kResourcePrefix)) {
        JARVIS_LOG_ERROR(kCategory, "translation catalogue '{}' is missing from the binary",
                         catalogue.toStdString());
        return false;
    }

    // Only swap once the new catalogue is known to be loadable, so a failure
    // leaves the interface in the language it was already speaking.
    removeCurrent();

    m_appTranslator = std::move(appTranslator);
    QCoreApplication::installTranslator(m_appTranslator.get());

    // Qt's own catalogue is a nice-to-have: it localises the few standard
    // strings Qt Quick Controls carry. Its absence is not an error.
    auto qtTranslator = std::make_unique<QTranslator>();
    const QString qtPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qtTranslator->load(QStringLiteral("qtbase_%1").arg(locale.left(2)), qtPath)) {
        m_qtTranslator = std::move(qtTranslator);
        QCoreApplication::installTranslator(m_qtTranslator.get());
    }

    // The locale drives everything Qt formats for itself: month and weekday
    // names in the HUD clock, decimal separators, and sort order. Without this
    // a Russian interface would still print "MON 10 AUG".
    QLocale::setDefault(QLocale{locale});

    m_language = language;
    m_installed = true;

    // Re-evaluates every qsTr() binding in the loaded QML. Without this the
    // interface would only change on the next start.
    if (m_engine != nullptr) {
        m_engine->retranslate();
    }

    JARVIS_LOG_INFO(kCategory, "language -> {} ({})",
                    i18n::languageCode(language), catalogue.toStdString());

    Q_EMIT languageChanged();
    return true;
}

} // namespace jarvis::app
