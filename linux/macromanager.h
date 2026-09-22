#pragma once

#include <cstdint>

#include "ScintillaMessages.h"
#include "ScintillaTypes.h"

#include <QByteArray>
#include <QMetaObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

class ScintillaEditBase;

struct MacroStep {
    Scintilla::Message message{};
    Scintilla::uptr_t wParam = 0;
    Scintilla::sptr_t scalarLParam = 0;
    QByteArray payload;
    bool hasPayload = false;
};

class MacroManager : public QObject {
    Q_OBJECT

public:
    explicit MacroManager(QObject *parent = nullptr);
    ~MacroManager() noexcept override;

    bool startRecording(ScintillaEditBase *editor);
    bool stopRecording();
    bool play(ScintillaEditBase *editor, int repeatCount = 1);

    bool isRecording() const { return m_recording; }
    bool isPlaying() const { return m_playing; }
    bool hasCurrentMacro() const { return !m_current.isEmpty(); }
    qsizetype currentStepCount() const { return m_current.size(); }

    QStringList savedMacroNames() const;
    bool saveCurrent(const QString &name, bool overwrite = false);
    bool selectSaved(const QString &name);
    bool removeSaved(const QString &name);
    void reloadSavedMacros();

signals:
    void stateChanged();
    void savedMacrosChanged();

private:
    void capture(Scintilla::Message message, Scintilla::uptr_t wParam,
                 Scintilla::sptr_t lParam);
    static bool isPayloadMessage(Scintilla::Message message);
    static bool isRecordableMessage(Scintilla::Message message);
    static QByteArray encodeSavedMacros(const QMap<QString, QVector<MacroStep>> &saved);
    static bool persistSavedMacros(const QMap<QString, QVector<MacroStep>> &saved);

    QVector<MacroStep> m_current;
    qsizetype m_currentPayloadSize = 0;
    QPointer<ScintillaEditBase> m_recordingEditor;
    QMetaObject::Connection m_recordingConnection;
    QMetaObject::Connection m_recordingDestroyedConnection;
    bool m_destroying = false;
    bool m_recording = false;
    bool m_playing = false;
    QMap<QString, QVector<MacroStep>> m_saved;
};
