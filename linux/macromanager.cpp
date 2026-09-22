#include "macromanager.h"

#include "Scintilla.h"
#include "ScintillaEditBase.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <cstring>
#include <limits>
#include <utility>

namespace {
constexpr qsizetype MaximumRawStoreSize = 32 * 1024 * 1024;
constexpr qsizetype MaximumMacroCount = 1000;
constexpr qsizetype MaximumAggregateSteps = 100000;
constexpr qsizetype MaximumAggregatePayload = 16 * 1024 * 1024;
constexpr quint64 MaximumPlaybackOperations = 1000000;
}

MacroManager::MacroManager(QObject *parent) : QObject(parent) { reloadSavedMacros(); }

MacroManager::~MacroManager() noexcept
{
    m_destroying = true;
    if (!m_recording)
        return;

    QPointer<ScintillaEditBase> target(m_recordingEditor);
    m_recording = false;
    disconnect(m_recordingConnection);
    disconnect(m_recordingDestroyedConnection);
    m_recordingEditor.clear();
    if (target)
        target->send(SCI_STOPRECORD);
}

bool MacroManager::startRecording(ScintillaEditBase *editor)
{
    if (m_destroying || !editor || m_recording || m_playing)
        return false;

    m_current.clear();
    m_currentPayloadSize = 0;
    m_recordingEditor = editor;
    m_recordingConnection = connect(editor, &ScintillaEditBase::macroRecord, this,
        [this](Scintilla::Message message, Scintilla::uptr_t wParam,
               Scintilla::sptr_t lParam) { capture(message, wParam, lParam); });
    m_recordingDestroyedConnection = connect(editor, &QObject::destroyed, this, [this] {
        disconnect(m_recordingConnection);
        m_recordingEditor.clear();
        m_recording = false;
        emit stateChanged();
    });
    m_recording = true;
    QPointer<MacroManager> self(this);
    QPointer<ScintillaEditBase> target(editor);
    target->send(SCI_STARTRECORD);
    if (!self)
        return false;
    emit stateChanged();
    return true;
}

bool MacroManager::stopRecording()
{
    if (m_destroying || !m_recording)
        return false;
    QPointer<MacroManager> self(this);
    QPointer<ScintillaEditBase> target(m_recordingEditor);
    m_recording = false;
    disconnect(m_recordingConnection);
    disconnect(m_recordingDestroyedConnection);
    m_recordingEditor.clear();
    if (target)
        target->send(SCI_STOPRECORD);
    if (!self)
        return false;
    emit stateChanged();
    return true;
}

bool MacroManager::play(ScintillaEditBase *editor, int repeatCount)
{
    if (m_destroying || !editor || m_recording || m_playing || m_current.isEmpty() ||
        repeatCount < 1 || repeatCount > 10000)
        return false;
    const quint64 stepCount = static_cast<quint64>(m_current.size());
    if (stepCount > MaximumPlaybackOperations / static_cast<quint64>(repeatCount))
        return false;

    const QVector<MacroStep> steps = m_current;
    QPointer<MacroManager> self(this);
    QPointer<ScintillaEditBase> target(editor);
    bool undoOpen = false;
    const auto finish = [&self, &target, &undoOpen](bool completed) {
        if (undoOpen && target) {
            undoOpen = false;
            target->send(SCI_ENDUNDOACTION);
        }
        if (!self)
            return false;
        self->m_playing = false;
        emit self->stateChanged();
        return completed;
    };

    m_playing = true;
    emit stateChanged();
    if (!self)
        return false;
    if (!target)
        return finish(false);

    undoOpen = true;
    target->send(SCI_BEGINUNDOACTION);
    if (!self || !target)
        return finish(false);
    for (int repeat = 0; repeat < repeatCount; ++repeat) {
        for (const MacroStep &step : steps) {
            if (!target)
                return finish(false);
            const Scintilla::sptr_t lParam = step.hasPayload
                ? reinterpret_cast<Scintilla::sptr_t>(step.payload.constData())
                : step.scalarLParam;
            target->send(static_cast<unsigned int>(step.message), step.wParam, lParam);
            if (!self || !target)
                return finish(false);
        }
        if (repeat + 1 < repeatCount) {
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            if (!self || !target)
                return finish(false);
        }
    }
    return finish(true);
}

void MacroManager::capture(Scintilla::Message message, Scintilla::uptr_t wParam,
                           Scintilla::sptr_t lParam)
{
    if (!m_recording || m_playing || !isRecordableMessage(message))
        return;
    if (m_current.size() >= MaximumAggregateSteps)
        return;
    MacroStep step;
    step.message = message;
    step.wParam = wParam;
    if (isPayloadMessage(message)) {
        if (!lParam)
            return;
        const char *bytes = reinterpret_cast<const char *>(lParam);
        const bool lengthBearing = message == Scintilla::Message::AddText ||
                                   message == Scintilla::Message::AppendText;
        const qsizetype remainingPayload = MaximumAggregatePayload - m_currentPayloadSize;
        if (remainingPayload == 0)
            return;
        size_t size = 0;
        if (lengthBearing) {
            if (wParam > static_cast<Scintilla::uptr_t>(remainingPayload))
                return;
            size = static_cast<size_t>(wParam);
        } else {
            size = strnlen(bytes, static_cast<size_t>(remainingPayload) + 1);
            if (size > static_cast<size_t>(remainingPayload))
                return;
        }
        step.payload = QByteArray(bytes, static_cast<qsizetype>(size));
        step.hasPayload = true;
        m_currentPayloadSize += static_cast<qsizetype>(size);
    } else {
        step.scalarLParam = lParam;
    }
    m_current.push_back(std::move(step));
}

bool MacroManager::isPayloadMessage(Scintilla::Message message)
{
    switch (message) {
    case Scintilla::Message::ReplaceSel:
    case Scintilla::Message::AddText:
    case Scintilla::Message::InsertText:
    case Scintilla::Message::AppendText:
    case Scintilla::Message::SearchNext:
    case Scintilla::Message::SearchPrev:
        return true;
    default:
        return false;
    }
}

bool MacroManager::isRecordableMessage(Scintilla::Message message)
{
    if (isPayloadMessage(message))
        return true;
    switch (message) {
    case Scintilla::Message::Cut: case Scintilla::Message::Copy:
    case Scintilla::Message::Paste: case Scintilla::Message::Clear:
    case Scintilla::Message::ClearAll: case Scintilla::Message::SelectAll:
    case Scintilla::Message::GotoLine: case Scintilla::Message::GotoPos:
    case Scintilla::Message::SearchAnchor:
    case Scintilla::Message::LineDown: case Scintilla::Message::LineDownExtend:
    case Scintilla::Message::ParaDown: case Scintilla::Message::ParaDownExtend:
    case Scintilla::Message::LineUp: case Scintilla::Message::LineUpExtend:
    case Scintilla::Message::ParaUp: case Scintilla::Message::ParaUpExtend:
    case Scintilla::Message::CharLeft: case Scintilla::Message::CharLeftExtend:
    case Scintilla::Message::CharRight: case Scintilla::Message::CharRightExtend:
    case Scintilla::Message::WordLeft: case Scintilla::Message::WordLeftExtend:
    case Scintilla::Message::WordRight: case Scintilla::Message::WordRightExtend:
    case Scintilla::Message::WordPartLeft: case Scintilla::Message::WordPartLeftExtend:
    case Scintilla::Message::WordPartRight: case Scintilla::Message::WordPartRightExtend:
    case Scintilla::Message::WordLeftEnd: case Scintilla::Message::WordLeftEndExtend:
    case Scintilla::Message::WordRightEnd: case Scintilla::Message::WordRightEndExtend:
    case Scintilla::Message::Home: case Scintilla::Message::HomeExtend:
    case Scintilla::Message::LineEnd: case Scintilla::Message::LineEndExtend:
    case Scintilla::Message::HomeWrap: case Scintilla::Message::HomeWrapExtend:
    case Scintilla::Message::LineEndWrap: case Scintilla::Message::LineEndWrapExtend:
    case Scintilla::Message::DocumentStart: case Scintilla::Message::DocumentStartExtend:
    case Scintilla::Message::DocumentEnd: case Scintilla::Message::DocumentEndExtend:
    case Scintilla::Message::StutteredPageUp: case Scintilla::Message::StutteredPageUpExtend:
    case Scintilla::Message::StutteredPageDown: case Scintilla::Message::StutteredPageDownExtend:
    case Scintilla::Message::PageUp: case Scintilla::Message::PageUpExtend:
    case Scintilla::Message::PageDown: case Scintilla::Message::PageDownExtend:
    case Scintilla::Message::EditToggleOvertype: case Scintilla::Message::Cancel:
    case Scintilla::Message::DeleteBack: case Scintilla::Message::Tab:
    case Scintilla::Message::LineIndent: case Scintilla::Message::BackTab:
    case Scintilla::Message::LineDedent: case Scintilla::Message::FormFeed:
    case Scintilla::Message::VCHome: case Scintilla::Message::VCHomeExtend:
    case Scintilla::Message::VCHomeWrap: case Scintilla::Message::VCHomeWrapExtend:
    case Scintilla::Message::VCHomeDisplay: case Scintilla::Message::VCHomeDisplayExtend:
    case Scintilla::Message::DelWordLeft: case Scintilla::Message::DelWordRight:
    case Scintilla::Message::DelWordRightEnd: case Scintilla::Message::DelLineLeft:
    case Scintilla::Message::DelLineRight: case Scintilla::Message::LineCopy:
    case Scintilla::Message::LineCut: case Scintilla::Message::LineDelete:
    case Scintilla::Message::LineTranspose: case Scintilla::Message::LineReverse:
    case Scintilla::Message::LineDuplicate: case Scintilla::Message::LowerCase:
    case Scintilla::Message::UpperCase: case Scintilla::Message::LineScrollDown:
    case Scintilla::Message::LineScrollUp: case Scintilla::Message::DeleteBackNotLine:
    case Scintilla::Message::HomeDisplay: case Scintilla::Message::HomeDisplayExtend:
    case Scintilla::Message::LineEndDisplay: case Scintilla::Message::LineEndDisplayExtend:
    case Scintilla::Message::SetSelectionMode:
    case Scintilla::Message::LineDownRectExtend: case Scintilla::Message::LineUpRectExtend:
    case Scintilla::Message::CharLeftRectExtend: case Scintilla::Message::CharRightRectExtend:
    case Scintilla::Message::HomeRectExtend: case Scintilla::Message::VCHomeRectExtend:
    case Scintilla::Message::LineEndRectExtend: case Scintilla::Message::PageUpRectExtend:
    case Scintilla::Message::PageDownRectExtend: case Scintilla::Message::SelectionDuplicate:
    case Scintilla::Message::CopyAllowLine: case Scintilla::Message::CutAllowLine:
    case Scintilla::Message::VerticalCentreCaret:
    case Scintilla::Message::MoveSelectedLinesUp: case Scintilla::Message::MoveSelectedLinesDown:
    case Scintilla::Message::ScrollToStart: case Scintilla::Message::ScrollToEnd:
        return true;
    default:
        return false;
    }
}

QStringList MacroManager::savedMacroNames() const { return m_saved.keys(); }

bool MacroManager::saveCurrent(const QString &requestedName, bool overwrite)
{
    const QString name = requestedName.trimmed();
    if (m_destroying || m_recording || m_playing || m_current.isEmpty() || name.isEmpty() ||
        name.size() > 200 ||
        (m_saved.contains(name) && !overwrite))
        return false;
    if (!m_saved.contains(name) && m_saved.size() >= MaximumMacroCount)
        return false;

    qsizetype aggregateSteps = m_current.size();
    qsizetype aggregatePayload = m_currentPayloadSize;
    for (auto it = m_saved.cbegin(); it != m_saved.cend(); ++it) {
        if (it.key() == name)
            continue;
        if (it.value().size() > MaximumAggregateSteps - aggregateSteps)
            return false;
        aggregateSteps += it.value().size();
        for (const MacroStep &step : it.value()) {
            if (!step.hasPayload)
                continue;
            if (step.payload.size() > MaximumAggregatePayload - aggregatePayload)
                return false;
            aggregatePayload += step.payload.size();
        }
    }

    QMap<QString, QVector<MacroStep>> candidate = m_saved;
    candidate.insert(name, m_current);
    const QByteArray encoded = encodeSavedMacros(candidate);
    if (encoded.size() > MaximumRawStoreSize)
        return false;

    if (!persistSavedMacros(candidate))
        return false;
    m_saved = std::move(candidate);
    emit savedMacrosChanged();
    return true;
}

bool MacroManager::selectSaved(const QString &name)
{
    if (m_destroying || m_recording || m_playing || !m_saved.contains(name))
        return false;
    m_current = m_saved.value(name);
    m_currentPayloadSize = 0;
    for (const MacroStep &step : std::as_const(m_current)) {
        if (step.hasPayload)
            m_currentPayloadSize += step.payload.size();
    }
    emit stateChanged();
    return true;
}

bool MacroManager::removeSaved(const QString &name)
{
    if (m_destroying || m_recording || m_playing || !m_saved.contains(name))
        return false;
    QMap<QString, QVector<MacroStep>> candidate = m_saved;
    candidate.remove(name);
    if (!persistSavedMacros(candidate))
        return false;
    m_saved = std::move(candidate);
    emit savedMacrosChanged();
    return true;
}

QByteArray MacroManager::encodeSavedMacros(
    const QMap<QString, QVector<MacroStep>> &saved)
{
    QJsonArray macros;
    for (auto macroIt = saved.cbegin(); macroIt != saved.cend(); ++macroIt) {
        QJsonArray steps;
        for (const MacroStep &step : macroIt.value()) {
            QJsonObject encoded{{"message", static_cast<int>(step.message)},
                                {"wParam", QString::number(step.wParam)},
                                {"lParam", QString::number(step.scalarLParam)},
                                {"hasPayload", step.hasPayload}};
            if (step.hasPayload)
                encoded.insert("payload", QString::fromLatin1(step.payload.toBase64()));
            steps.append(encoded);
        }
        macros.append(QJsonObject{{"name", macroIt.key()}, {"steps", steps}});
    }
    const QJsonDocument document(QJsonObject{{"version", 1}, {"macros", macros}});
    return document.toJson(QJsonDocument::Compact);
}

bool MacroManager::persistSavedMacros(const QMap<QString, QVector<MacroStep>> &saved)
{
    QSettings settings;
    settings.setValue(QStringLiteral("macros/data"), encodeSavedMacros(saved));
    settings.sync();
    return settings.status() == QSettings::NoError;
}

void MacroManager::reloadSavedMacros()
{
    if (m_destroying)
        return;
    m_saved.clear();
    const QByteArray encoded = QSettings().value(QStringLiteral("macros/data")).toByteArray();
    if (encoded.isEmpty())
        return;
    if (encoded.size() > MaximumRawStoreSize)
        return;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(encoded, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject root = document.object();
    if (root.value("version").toInt(-1) != 1 || !root.value("macros").isArray())
        return;
    const QJsonArray macros = root.value("macros").toArray();
    if (macros.size() > MaximumMacroCount)
        return;

    QMap<QString, QVector<MacroStep>> validated;
    qsizetype aggregateSteps = 0;
    qsizetype aggregatePayload = 0;
    for (const QJsonValue &macroValue : macros) {
        if (!macroValue.isObject())
            return;
        const QJsonObject macroObject = macroValue.toObject();
        const QString name = macroObject.value("name").toString().trimmed();
        if (name.isEmpty() || name.size() > 200 || validated.contains(name) ||
            !macroObject.value("steps").isArray())
            return;
        const QJsonArray encodedSteps = macroObject.value("steps").toArray();
        if (encodedSteps.isEmpty() || encodedSteps.size() > 100000)
            return;
        if (encodedSteps.size() > MaximumAggregateSteps - aggregateSteps)
            return;
        aggregateSteps += encodedSteps.size();
        QVector<MacroStep> steps;
        steps.reserve(encodedSteps.size());
        for (const QJsonValue &stepValue : encodedSteps) {
            if (!stepValue.isObject())
                return;
            const QJsonObject object = stepValue.toObject();
            const QJsonValue messageValue = object.value("message");
            const QJsonValue wParamValue = object.value("wParam");
            const QJsonValue lParamValue = object.value("lParam");
            const QJsonValue hasPayloadValue = object.value("hasPayload");
            if (!messageValue.isDouble() || messageValue.toDouble() < 0 ||
                messageValue.toDouble() > std::numeric_limits<int>::max() ||
                messageValue.toDouble() != static_cast<double>(messageValue.toInt()) ||
                !wParamValue.isString() || !lParamValue.isString() ||
                !hasPayloadValue.isBool())
                return;
            const auto message = static_cast<Scintilla::Message>(messageValue.toInt());
            bool wOk = false;
            bool lOk = false;
            const qulonglong wParam = wParamValue.toString().toULongLong(&wOk);
            const qlonglong lParam = lParamValue.toString().toLongLong(&lOk);
            const bool hasPayload = hasPayloadValue.toBool();
            if (!wOk || wParam > std::numeric_limits<Scintilla::uptr_t>::max() ||
                !lOk || lParam < std::numeric_limits<Scintilla::sptr_t>::min() ||
                lParam > std::numeric_limits<Scintilla::sptr_t>::max() ||
                !isRecordableMessage(message) ||
                hasPayload != isPayloadMessage(message))
                return;
            MacroStep step{message, static_cast<Scintilla::uptr_t>(wParam),
                           static_cast<Scintilla::sptr_t>(lParam), {}, hasPayload};
            if (hasPayload) {
                const QJsonValue payloadValue = object.value("payload");
                if (!payloadValue.isString())
                    return;
                const QByteArray base64 = payloadValue.toString().toLatin1();
                step.payload = QByteArray::fromBase64(base64, QByteArray::AbortOnBase64DecodingErrors);
                if (step.payload.isNull() || step.payload.toBase64() != base64 ||
                    step.payload.size() > MaximumAggregatePayload - aggregatePayload)
                    return;
                aggregatePayload += step.payload.size();
                if ((message == Scintilla::Message::AddText ||
                     message == Scintilla::Message::AppendText) &&
                    step.wParam != static_cast<Scintilla::uptr_t>(step.payload.size()))
                    return;
                step.scalarLParam = 0;
            } else if (object.contains("payload"))
                return;
            steps.push_back(std::move(step));
        }
        validated.insert(name, std::move(steps));
    }
    m_saved = std::move(validated);
    emit savedMacrosChanged();
}
