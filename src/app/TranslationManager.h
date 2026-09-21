#pragma once

#include <QObject>
#include <QString>

#include <memory>

#include "jarvis/i18n/Language.h"

class QQmlApplicationEngine;
class QTranslator;

namespace jarvis::app {

/// Loads and swaps the interface language at run time.
///
/// Two catalogues are installed for each language: the JARVIS one, and Qt's own
/// (so standard dialogs and controls speak the same language). Switching
/// removes both, installs the replacement pair, and asks the QML engine to
/// re-evaluate every qsTr() binding - no restart, and the current page and
/// scroll position survive.
///
/// A language whose catalogue is missing is refused rather than silently
/// leaving the interface half-translated.
class TranslationManager : public QObject {
    Q_OBJECT

public:
    explicit TranslationManager(QObject* parent = nullptr);
    ~TranslationManager() override;

    /// The engine to retranslate. Set once, after the engine is constructed.
    void setEngine(QQmlApplicationEngine* engine);

    [[nodiscard]] i18n::Language language() const noexcept { return m_language; }

    /// Installs \p language. Returns false when its catalogue could not be
    /// loaded, in which case the previous language stays active.
    bool apply(i18n::Language language);

Q_SIGNALS:
    /// Emitted after the catalogues are installed and QML has been asked to
    /// retranslate. C++ objects holding tr()-derived strings rebuild them here.
    void languageChanged();

private:
    void removeCurrent();

    QQmlApplicationEngine* m_engine{nullptr};
    std::unique_ptr<QTranslator> m_appTranslator;
    std::unique_ptr<QTranslator> m_qtTranslator;
    i18n::Language m_language{i18n::kDefaultLanguage};
    bool m_installed{false};
};

} // namespace jarvis::app
