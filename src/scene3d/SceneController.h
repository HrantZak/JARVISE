#pragma once
#include <QObject>
#include <QVariantList>
#include <optional>

namespace jarvis::scene3d {
class SceneController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY changed)
    Q_PROPERTY(bool rotating READ rotating WRITE setRotating NOTIFY changed)
    Q_PROPERTY(bool pulsing READ pulsing WRITE setPulsing NOTIFY changed)
    Q_PROPERTY(double lighting READ lighting WRITE setLighting NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastExport READ lastExport NOTIFY changed)
    Q_PROPERTY(bool drawingMode READ drawingMode NOTIFY drawingChanged)
    Q_PROPERTY(QVariantList strokes READ strokes NOTIFY drawingChanged)
    Q_PROPERTY(QVariantList drawingLabels READ drawingLabels NOTIFY drawingChanged)
    Q_PROPERTY(QString drawingTitle READ drawingTitle NOTIFY drawingChanged)
public:
    explicit SceneController(QObject* parent=nullptr):QObject(parent) {}
    QVariantList items() const { return m_items; }
    int selected() const { return m_selected; }
    void setSelected(int value);
    bool rotating() const { return m_rotating; }
    void setRotating(bool value) { m_rotating=value; emit changed(); }
    bool pulsing() const { return m_pulsing; }
    void setPulsing(bool value) { m_pulsing=value; emit changed(); }
    double lighting() const { return m_lighting; }
    void setLighting(double value);
    QString status() const { return m_status; }
    QString lastExport() const { return m_lastExport; }
    std::optional<QString> handle(const QString& request);
    Q_INVOKABLE bool submit(const QString& request);
    Q_INVOKABLE bool setParameter(const QString& key,const QVariant& value);
    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void clearScene();
    Q_INVOKABLE void resetCamera() { emit cameraReset(); }
    Q_INVOKABLE QString exportScene(const QString& format);
    Q_INVOKABLE void requestExport(const QString& format);
    QString exportDirectory; // Tests can redirect exports to a temporary directory.
    bool drawingMode() const { return m_drawingMode; }
    QVariantList strokes() const { return m_strokes; }
    QVariantList drawingLabels() const { return m_labels; }
    QString drawingTitle() const { return m_drawingTitle; }
    static QString validateDrawing(const QString& json);
    bool applyDrawing(const QString& json);
    std::optional<QString> drawRequest(const QString& text);
    Q_INVOKABLE void requestDrawing(const QString& text) { emit drawingPrompt(text); }
signals:
    void changed();
    void showRequested();
    void cameraReset();
    void fullscreenRequested();
    void drawingChanged();
    void drawingPrompt(const QString& text);
private:
    bool budget(const QVariantList& items) const;
    QVariantList m_items;
    int m_selected=-1;
    bool m_rotating=true,m_pulsing=false;
    bool m_exporting=false;
    bool m_drawingMode=true;
    QVariantList m_strokes;
    QVariantList m_labels;
    QString m_drawingTitle=QStringLiteral("Что нарисовать?");
    double m_lighting=1.0;
    QString m_status=QStringLiteral("Опишите модель — она появится здесь."),m_lastExport;
};
}
