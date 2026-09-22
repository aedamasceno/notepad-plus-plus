#include "macromanager.h"
#include "mainwindow.h"

#include "ScintillaEditBase.h"
#include "Scintilla.h"

#include <QApplication>
#include <QAction>
#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMainWindow>
#include <QMenu>
#include <QTemporaryDir>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>

#include <cstdlib>
#include <cstdio>
#include <functional>

namespace {
void expect(bool condition, const char *message)
{
    if (!condition) {
        qCritical() << message;
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

QByteArray text(ScintillaEditBase &editor)
{
    const auto length = editor.send(SCI_GETTEXTLENGTH);
    QByteArray result(static_cast<int>(length) + 1, '\0');
    editor.send(SCI_GETTEXT, result.size(), reinterpret_cast<Scintilla::sptr_t>(result.data()));
    result.chop(1);
    return result;
}

class PlaybackCallbackEditor : public ScintillaEditBase {
public:
    mutable std::function<void(unsigned int)> callback;
    mutable int beginUndoCount = 0;
    mutable int endUndoCount = 0;

    Scintilla::sptr_t send(unsigned int message, Scintilla::uptr_t wParam = 0,
                          Scintilla::sptr_t lParam = 0) const override
    {
        if (message == SCI_BEGINUNDOACTION)
            ++beginUndoCount;
        else if (message == SCI_ENDUNDOACTION)
            ++endUndoCount;
        const Scintilla::sptr_t result = ScintillaEditBase::send(message, wParam, lParam);
        if (callback)
            callback(message);
        return result;
    }
};

QJsonObject scalarStep()
{
    return {{"message", SCI_LINEDOWN}, {"wParam", "0"}, {"lParam", "0"},
            {"hasPayload", false}};
}

QJsonObject payloadStep(const QByteArray &payload)
{
    return {{"message", SCI_REPLACESEL}, {"wParam", "0"}, {"lParam", "0"},
            {"hasPayload", true}, {"payload", QString::fromLatin1(payload.toBase64())}};
}

QByteArray store(const QJsonArray &macros)
{
    return QJsonDocument(QJsonObject{{"version", 1}, {"macros", macros}})
        .toJson(QJsonDocument::Compact);
}

void setStore(const QByteArray &encoded)
{
    QSettings settings;
    settings.setValue("macros/data", encoded);
    settings.sync();
}

void testRecordTypingPlaybackAndState()
{
    MacroManager manager;
    ScintillaEditBase source;
    ScintillaEditBase target;

    expect(manager.startRecording(&source), "recording should start");
    expect(manager.isRecording(), "recording state should be active");
    const QByteArray utf8("h\xC3\xA9llo");
    source.send(SCI_REPLACESEL, 0, reinterpret_cast<Scintilla::sptr_t>(utf8.constData()));
    expect(!manager.play(&target), "playback must be forbidden while recording");
    expect(manager.stopRecording(), "recording should stop");
    expect(!manager.isRecording() && manager.hasCurrentMacro(), "stopped macro should be usable");
    expect(manager.play(&target), "recorded macro should play");
    expect(text(target) == utf8, "UTF-8 payload should round trip");
}

void testOwnershipRepeatAndSafeEdges()
{
    MacroManager manager;
    ScintillaEditBase source;
    ScintillaEditBase target;
    QByteArray transient("owned");

    expect(!manager.startRecording(nullptr), "null editor cannot record");
    expect(!manager.play(nullptr), "null editor cannot play");
    expect(manager.startRecording(&source), "recording should start");
    source.send(SCI_ADDTEXT, transient.size(),
                reinterpret_cast<Scintilla::sptr_t>(transient.data()));
    expect(manager.stopRecording(), "recording should stop");
    transient.fill('x');
    expect(!manager.play(&target, 0) && !manager.play(&target, 10001),
           "repeat bounds should be enforced");
    expect(manager.play(&target, 3), "valid repeat should play");
    expect(text(target) == "ownedownedowned", "recorded bytes must be deeply owned");
    expect(manager.currentStepCount() == 1,
           "playback must not recursively append recorded steps");

    MacroManager empty;
    expect(!empty.play(&target), "empty macro should be safe and unplayable");
}

void testEditorDestructionDuringTransitions()
{
    MacroManager recordingManager;
    auto *source = new ScintillaEditBase;
    expect(recordingManager.startRecording(source), "recording should start before source deletion");
    delete source;
    expect(!recordingManager.isRecording(), "destroyed recording source must end recording state");
    expect(!recordingManager.stopRecording(), "destroyed recording source is already torn down");

    MacroManager playingManager;
    ScintillaEditBase seed;
    expect(playingManager.startRecording(&seed), "playback deletion seed should record");
    seed.send(SCI_LINEDOWN);
    expect(playingManager.stopRecording(), "playback deletion seed should stop");
    auto *target = new ScintillaEditBase;
    QObject::connect(&playingManager, &MacroManager::stateChanged, target,
                     [target, &playingManager] {
                         if (playingManager.isPlaying())
                             delete target;
                     });
    expect(!playingManager.play(target), "target deletion from stateChanged must safely abort play");
    expect(!playingManager.isPlaying(), "aborted play must restore playing state");
}

void testManagerDestructionStopsLiveRecordingEditor()
{
    PlaybackCallbackEditor source;
    ScintillaEditBase reentrantSource;
    QPointer<MacroManager> manager(new MacroManager);
    int stopSendCount = 0;
    int macroRecordCount = 0;
    int reentrantMacroRecordCount = 0;
    bool nestedStopResult = true;
    bool nestedStartResult = true;
    source.callback = [&manager, &reentrantSource, &stopSendCount, &nestedStopResult,
                       &nestedStartResult](unsigned int message) {
        if (message != SCI_STOPRECORD)
            return;
        ++stopSendCount;
        if (manager) {
            nestedStopResult = manager->stopRecording();
            nestedStartResult = manager->startRecording(&reentrantSource);
        }
    };
    QObject::connect(&source, &ScintillaEditBase::macroRecord,
                     [&macroRecordCount](Scintilla::Message, Scintilla::uptr_t,
                                         Scintilla::sptr_t) { ++macroRecordCount; });
    QObject::connect(&reentrantSource, &ScintillaEditBase::macroRecord,
                     [&reentrantMacroRecordCount](Scintilla::Message, Scintilla::uptr_t,
                                                  Scintilla::sptr_t) {
                         ++reentrantMacroRecordCount;
                     });

    expect(manager->startRecording(&source), "destruction seed should start recording");
    delete manager.data();

    expect(stopSendCount == 1,
           "destroying a recording manager must send exactly one SCI_STOPRECORD");
    expect(!nestedStopResult && !nestedStartResult,
           "recording transitions must be rejected while manager destruction sends STOP");
    const int recordsAfterDestruction = macroRecordCount;
    source.send(SCI_LINEDOWN);
    expect(macroRecordCount == recordsAfterDestruction,
           "a live source must stop emitting macro notifications after manager destruction");
    reentrantSource.send(SCI_LINEDOWN);
    expect(reentrantMacroRecordCount == 0,
           "destructor reentrancy must not leave another editor recording");
}

void testManagerDestructionDuringRecordTransitions()
{
    PlaybackCallbackEditor startEditor;
    QPointer<MacroManager> startManager(new MacroManager);
    int startStopSendCount = 0;
    int startMacroRecordCount = 0;
    QObject::connect(&startEditor, &ScintillaEditBase::macroRecord,
                     [&startMacroRecordCount](Scintilla::Message, Scintilla::uptr_t,
                                              Scintilla::sptr_t) { ++startMacroRecordCount; });
    startEditor.callback = [&startManager, &startStopSendCount](unsigned int message) {
        if (message == SCI_STOPRECORD)
            ++startStopSendCount;
        if (message == SCI_STARTRECORD && startManager)
            delete startManager.data();
    };
    expect(!startManager->startRecording(&startEditor) && !startManager,
           "manager deletion during SCI_STARTRECORD must safely abort start");
    expect(startStopSendCount == 1,
           "manager deletion during SCI_STARTRECORD must send exactly one SCI_STOPRECORD");
    const int startRecordsAfterDestruction = startMacroRecordCount;
    startEditor.send(SCI_LINEDOWN);
    expect(startMacroRecordCount == startRecordsAfterDestruction,
           "reentrant manager deletion must leave the source recording disabled");

    PlaybackCallbackEditor stopEditor;
    QPointer<MacroManager> stopManager(new MacroManager);
    expect(stopManager->startRecording(&stopEditor),
           "manager-deletion stop seed should record");
    bool reentered = false;
    bool nestedStopResult = true;
    int stopSendCount = 0;
    stopEditor.callback = [&stopManager, &reentered, &nestedStopResult, &stopSendCount](unsigned int message) {
        if (message != SCI_STOPRECORD)
            return;
        ++stopSendCount;
        if (!reentered) {
            reentered = true;
            nestedStopResult = stopManager->stopRecording();
        }
    };
    expect(stopManager->stopRecording() && !nestedStopResult && stopSendCount == 1,
           "recursive stop must be rejected without a second SCI_STOPRECORD");

    delete stopManager.data();
    stopManager = new MacroManager;
    stopSendCount = 0;
    int stopMacroRecordCount = 0;
    QObject::connect(&stopEditor, &ScintillaEditBase::macroRecord,
                     [&stopMacroRecordCount](Scintilla::Message, Scintilla::uptr_t,
                                             Scintilla::sptr_t) { ++stopMacroRecordCount; });
    expect(stopManager->startRecording(&stopEditor),
           "manager-deletion stop seed should restart");
    stopEditor.callback = [&stopManager, &stopSendCount](unsigned int message) {
        if (message != SCI_STOPRECORD)
            return;
        ++stopSendCount;
        if (stopManager)
            delete stopManager.data();
    };
    expect(!stopManager->stopRecording() && !stopManager && stopSendCount == 1,
           "manager deletion during SCI_STOPRECORD must safely abort stop");
    const int stopRecordsAfterDeletion = stopMacroRecordCount;
    stopEditor.send(SCI_LINEDOWN);
    expect(stopMacroRecordCount == stopRecordsAfterDeletion,
           "SCI_STOPRECORD deletion must leave the surviving editor recording disabled");
}

void testManagerDestructionDuringPlaybackReentrancy()
{
    ScintillaEditBase seed;
    auto makeManager = [&seed] {
        auto *manager = new MacroManager;
        expect(manager->startRecording(&seed), "manager-deletion seed should record");
        seed.send(SCI_LINEDOWN);
        expect(manager->stopRecording(), "manager-deletion seed should stop");
        return manager;
    };

    PlaybackCallbackEditor initialTarget;
    QPointer<MacroManager> initialManager(makeManager());
    QObject::connect(initialManager, &MacroManager::stateChanged, &initialTarget,
                     [&initialManager] {
                         if (initialManager && initialManager->isPlaying())
                             delete initialManager.data();
                     });
    expect(!initialManager->play(&initialTarget) && !initialManager,
           "manager deletion from initial stateChanged must safely abort playback");
    expect(initialTarget.beginUndoCount == 0 && initialTarget.endUndoCount == 0,
           "initial state deletion must not open an undo action");

    PlaybackCallbackEditor sendTarget;
    QPointer<MacroManager> sendManager(makeManager());
    sendTarget.callback = [&sendManager](unsigned int message) {
        if (message == SCI_LINEDOWN && sendManager)
            delete sendManager.data();
    };
    expect(!sendManager->play(&sendTarget) && !sendManager,
           "manager deletion from a playback send callback must safely abort playback");
    expect(sendTarget.beginUndoCount == 1 && sendTarget.endUndoCount == 1,
           "send-callback deletion should leave the surviving target undo-balanced");

    PlaybackCallbackEditor eventTarget;
    QPointer<MacroManager> eventManager(makeManager());
    QTimer::singleShot(0, [&eventManager] { delete eventManager.data(); });
    expect(!eventManager->play(&eventTarget, 2) && !eventManager,
           "manager deletion from processEvents must safely abort playback");
    expect(eventTarget.beginUndoCount == 1 && eventTarget.endUndoCount == 1,
           "event-callback deletion should leave the surviving target undo-balanced");
}

void testPersistenceSyncFailuresAreAtomic(const QString &workingSettingsPath)
{
    QSettings().clear();
    ScintillaEditBase source;
    MacroManager manager;
    expect(manager.startRecording(&source), "sync-failure seed should record");
    source.send(SCI_LINEDOWN);
    expect(manager.stopRecording() && manager.saveCurrent("existing"),
           "sync-failure seed should save");

    const QString unusablePath = workingSettingsPath + "/not-a-directory";
    QFile blocker(unusablePath);
    expect(blocker.open(QIODevice::WriteOnly), "settings path blocker should open");
    blocker.write("block");
    blocker.close();
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, unusablePath);

    expect(!manager.saveCurrent("new"),
           "save must fail when QSettings cannot sync");
    expect(manager.savedMacroNames() == QStringList{"existing"},
           "failed save must not publish the candidate map");
    expect(!manager.removeSaved("existing"),
           "removal must fail when QSettings cannot sync");
    expect(manager.savedMacroNames() == QStringList{"existing"},
           "failed removal must not mutate the saved map");

    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, workingSettingsPath);
}

void testPlaybackOperationBudget()
{
    MacroManager manager;
    ScintillaEditBase source;
    ScintillaEditBase target;
    expect(manager.startRecording(&source), "operation budget recording should start");
    for (int i = 0; i < 101; ++i)
        source.send(SCI_LINEDOWN);
    expect(manager.stopRecording(), "operation budget recording should stop");
    expect(!manager.play(&target, 10000), "repeat times steps above send budget must be rejected");
}

void testLiveCaptureBudgetsPreserveRecordedPrefix()
{
    MacroManager stepsManager;
    ScintillaEditBase stepSource;
    ScintillaEditBase stepTarget;
    expect(stepsManager.startRecording(&stepSource), "step-budget recording should start");
    for (int i = 0; i < 100001; ++i)
        stepSource.send(SCI_LINEDOWN);
    expect(stepsManager.currentStepCount() == 100000,
           "live recording must stop appending at the step budget");
    expect(stepsManager.stopRecording() && stepsManager.play(&stepTarget),
           "the captured prefix must remain playable after reaching the step budget");

    MacroManager payloadManager;
    ScintillaEditBase payloadSource;
    ScintillaEditBase payloadTarget;
    const QByteArray payload(16 * 1024 * 1024, 'p');
    expect(payloadManager.startRecording(&payloadSource), "payload-budget recording should start");
    payloadSource.send(SCI_ADDTEXT, payload.size(),
                       reinterpret_cast<Scintilla::sptr_t>(payload.constData()));
    payloadSource.send(SCI_ADDTEXT, 1, reinterpret_cast<Scintilla::sptr_t>("x"));
    expect(payloadManager.currentStepCount() == 1,
           "live recording must reject payload beyond the aggregate payload budget");
    expect(payloadManager.stopRecording() && payloadManager.play(&payloadTarget) &&
               text(payloadTarget) == payload,
           "the payload-budget prefix must remain playable");
}

void testSaveBudgetsAreAtomic()
{
    QJsonArray macros;
    for (int i = 0; i < 999; ++i)
        macros.append(QJsonObject{{"name", QString::number(i)},
                                  {"steps", QJsonArray{scalarStep()}}});
    setStore(store(macros));
    MacroManager countManager;
    ScintillaEditBase countSource;
    expect(countManager.startRecording(&countSource), "macro-count recording should start");
    countSource.send(SCI_LINEDOWN);
    expect(countManager.stopRecording() && countManager.saveCurrent("macro 1000"),
           "the macro-count boundary should save");
    const QByteArray countStore = QSettings().value("macros/data").toByteArray();
    expect(!countManager.saveCurrent("macro 1001") &&
               countManager.savedMacroNames().size() == 1000 &&
               QSettings().value("macros/data").toByteArray() == countStore,
           "a macro-count rejection must not mutate memory or settings");

    QJsonArray almostFullSteps;
    for (int i = 0; i < 99998; ++i)
        almostFullSteps.append(scalarStep());
    setStore(store(QJsonArray{
        QJsonObject{{"name", "replace"}, {"steps", QJsonArray{scalarStep()}}},
        QJsonObject{{"name", "bulk"}, {"steps", almostFullSteps}},
    }));
    MacroManager stepManager;
    ScintillaEditBase stepSource;
    expect(stepManager.startRecording(&stepSource), "aggregate-step recording should start");
    stepSource.send(SCI_LINEDOWN);
    stepSource.send(SCI_LINEDOWN);
    expect(stepManager.stopRecording() && stepManager.saveCurrent("replace", true),
           "replacement at the aggregate-step boundary should save");
    expect(stepManager.startRecording(&stepSource), "over-budget replacement should record");
    stepSource.send(SCI_LINEDOWN);
    stepSource.send(SCI_LINEDOWN);
    stepSource.send(SCI_LINEDOWN);
    expect(stepManager.stopRecording(), "over-budget replacement should stop");
    const QByteArray stepStore = QSettings().value("macros/data").toByteArray();
    expect(!stepManager.saveCurrent("replace", true) &&
               QSettings().value("macros/data").toByteArray() == stepStore,
           "an aggregate-step rejection must leave the replacement and settings unchanged");

    const QByteArray fullPayload(16 * 1024 * 1024, 'q');
    setStore(store(QJsonArray{
        QJsonObject{{"name", "payload"}, {"steps", QJsonArray{payloadStep(fullPayload)}}},
    }));
    MacroManager payloadManager;
    ScintillaEditBase payloadSource;
    expect(payloadManager.startRecording(&payloadSource), "aggregate-payload recording should start");
    payloadSource.send(SCI_REPLACESEL, 0, reinterpret_cast<Scintilla::sptr_t>("x"));
    expect(payloadManager.stopRecording(), "aggregate-payload recording should stop");
    const QByteArray payloadStore = QSettings().value("macros/data").toByteArray();
    expect(!payloadManager.saveCurrent("extra payload") &&
               payloadManager.savedMacroNames() == QStringList{"payload"} &&
               QSettings().value("macros/data").toByteArray() == payloadStore,
           "an aggregate-payload rejection must not mutate memory or settings");
}

void testSaveEncodedOutputBudgetIsAtomic()
{
    QJsonObject largeScalar{{"message", SCI_LINEDOWN},
                            {"wParam", QString::number(std::numeric_limits<qulonglong>::max())},
                            {"lParam", QString::number(std::numeric_limits<qlonglong>::min())},
                            {"hasPayload", false}};
    QJsonArray steps;
    for (int i = 0; i < 98902; ++i)
        steps.append(largeScalar);
    QJsonArray macros;
    const QChar namePadding(1);
    for (int i = 0; i < 998; ++i) {
        const QString suffix = QString::number(i);
        const QString name(200 - suffix.size(), namePadding);
        macros.append(QJsonObject{{"name", name + suffix},
                                  {"steps", QJsonArray{largeScalar}}});
    }
    macros.append(QJsonObject{{"name", "replace for encoded limit"},
                              {"steps", QJsonArray{largeScalar}}});
    QByteArray padding(16 * 1024 * 1024, 'z');
    QByteArray encoded;
    do {
        steps.append(payloadStep(padding));
        macros.append(QJsonObject{{"name", QString(200, namePadding)}, {"steps", steps}});
        encoded = store(macros);
        macros.removeLast();
        steps.removeLast();
        if (encoded.size() > 32 * 1024 * 1024)
            padding.chop(qMin<qsizetype>(4096, padding.size()));
    } while (encoded.size() > 32 * 1024 * 1024);
    steps.append(payloadStep(padding));
    macros.append(QJsonObject{{"name", QString(200, namePadding)}, {"steps", steps}});
    setStore(store(macros));

    MacroManager manager;
    expect(manager.savedMacroNames().size() == 1000,
           "near-limit encoded seed should load");
    ScintillaEditBase source;
    expect(manager.startRecording(&source), "encoded-output recording should start");
    for (int i = 0; i < 99; ++i)
        source.send(SCI_LINEDOWN, std::numeric_limits<Scintilla::uptr_t>::max(),
                    std::numeric_limits<Scintilla::sptr_t>::min());
    expect(manager.stopRecording(), "encoded-output recording should stop");
    const QByteArray before = QSettings().value("macros/data").toByteArray();
    expect(!manager.saveCurrent("replace for encoded limit", true) &&
               manager.savedMacroNames().size() == 1000 &&
               QSettings().value("macros/data").toByteArray() == before,
           "encoded output above 32 MiB must be rejected before mutation or writing");
    QSettings().clear();
}

void testStrictPersistenceSchemaIsAtomic()
{
    const QJsonObject validMacro{{"name", "valid"}, {"steps", QJsonArray{scalarStep()}}};
    const QList<QJsonObject> malformedSteps{
        {{"message", 2300.5}, {"wParam", "0"}, {"lParam", "0"}, {"hasPayload", false}},
        {{"message", "2300"}, {"wParam", "0"}, {"lParam", "0"}, {"hasPayload", false}},
        {{"message", SCI_LINEDOWN}, {"wParam", 0}, {"lParam", "0"}, {"hasPayload", false}},
        {{"message", SCI_LINEDOWN}, {"wParam", "0"}, {"lParam", 0}, {"hasPayload", false}},
        {{"message", SCI_LINEDOWN}, {"wParam", "0"}, {"lParam", "0"}},
        {{"message", SCI_LINEDOWN}, {"wParam", "0"}, {"lParam", "0"}, {"hasPayload", 0}},
        {{"message", SCI_REPLACESEL}, {"wParam", "0"}, {"lParam", "0"}, {"hasPayload", true}},
        {{"message", SCI_REPLACESEL}, {"wParam", "0"}, {"lParam", "0"},
         {"hasPayload", true}, {"payload", 12}},
        {{"message", SCI_REPLACESEL}, {"wParam", "0"}, {"lParam", "0"},
         {"hasPayload", true}, {"payload", "%%%not-base64%%%"}},
        {{"message", SCI_LINEDOWN}, {"wParam", "0"}, {"lParam", "0"},
         {"hasPayload", false}, {"payload", "dW5leHBlY3RlZA=="}},
    };
    for (int i = 0; i < malformedSteps.size(); ++i) {
        const QJsonObject invalidMacro{{"name", QString("invalid-%1").arg(i)},
                                       {"steps", QJsonArray{malformedSteps.at(i)}}};
        setStore(store(QJsonArray{validMacro, invalidMacro}));
        MacroManager manager;
        expect(manager.savedMacroNames().isEmpty(),
               "any invalid persisted step must reject the entire store");
    }
}

void testPersistenceAggregateBudgets()
{
    QJsonArray manyMacros;
    for (int i = 0; i < 1001; ++i)
        manyMacros.append(QJsonObject{{"name", QString::number(i)},
                                      {"steps", QJsonArray{scalarStep()}}});
    setStore(store(manyMacros));
    expect(MacroManager().savedMacroNames().isEmpty(), "macro count budget must be enforced");

    QJsonArray steps;
    for (int i = 0; i < 50001; ++i)
        steps.append(scalarStep());
    setStore(store(QJsonArray{
        QJsonObject{{"name", "first"}, {"steps", steps}},
        QJsonObject{{"name", "second"}, {"steps", steps}},
    }));
    expect(MacroManager().savedMacroNames().isEmpty(), "aggregate step budget must be enforced");

    const QByteArray nineMiB(9 * 1024 * 1024, 'x');
    setStore(store(QJsonArray{
        QJsonObject{{"name", "first"}, {"steps", QJsonArray{payloadStep(nineMiB)}}},
        QJsonObject{{"name", "second"}, {"steps", QJsonArray{payloadStep(nineMiB)}}},
    }));
    expect(MacroManager().savedMacroNames().isEmpty(),
           "aggregate decoded payload budget must be enforced");

    QByteArray oversized = store(QJsonArray{
        QJsonObject{{"name", "valid"}, {"steps", QJsonArray{scalarStep()}}},
    });
    oversized.prepend(QByteArray(32 * 1024 * 1024, ' '));
    setStore(oversized);
    expect(MacroManager().savedMacroNames().isEmpty(), "raw persisted JSON budget must be enforced");
}

void testPersistenceRecreationOverwriteAndMalformed()
{
    ScintillaEditBase source;
    MacroManager first;
    const QByteArray utf8("snowman \xE2\x98\x83");
    expect(first.startRecording(&source), "persistence recording should start");
    source.send(SCI_REPLACESEL, 0,
                reinterpret_cast<Scintilla::sptr_t>(utf8.constData()));
    expect(first.stopRecording(), "persistence recording should stop");
    expect(first.saveCurrent("UTF-8 macro"), "named macro should save");
    expect(!first.saveCurrent("UTF-8 macro"), "overwrite must be explicit");
    expect(first.saveCurrent("UTF-8 macro", true), "explicit overwrite should succeed");

    MacroManager recreated;
    expect(recreated.savedMacroNames() == QStringList{"UTF-8 macro"},
           "saved names should survive recreation");
    expect(recreated.selectSaved("UTF-8 macro"), "saved macro should be selectable");
    ScintillaEditBase target;
    expect(recreated.play(&target) && text(target) == utf8,
           "recreated saved macro should remain playable");

    QSettings settings;
    settings.setValue("macros/data", QByteArray("{ definitely malformed"));
    settings.sync();
    MacroManager malformed;
    expect(malformed.savedMacroNames().isEmpty() && !malformed.hasCurrentMacro(),
           "malformed persistence must be rejected safely");
}

QAction *action(QMainWindow *window, const char *name)
{
    QAction *found = window->findChild<QAction *>(name);
    expect(found != nullptr, name);
    return found;
}

void testMainWindowMenuWiringStateAndCrossTabTargeting()
{
    QSettings().clear();
    QMainWindow *window = createMainWindow();
    auto *tabs = window->findChild<QTabWidget *>();
    auto *start = action(window, "macroStartAction");
    auto *stop = action(window, "macroStopAction");
    auto *play = action(window, "macroPlayAction");
    auto *repeat = action(window, "macroRepeatAction");
    auto *save = action(window, "macroSaveAction");
    auto *macroMenu = window->findChild<QMenu *>("macroMenu");
    expect(macroMenu && macroMenu->actions().contains(start) &&
               macroMenu->actions().contains(stop) && macroMenu->actions().contains(play) &&
               macroMenu->actions().contains(repeat) && macroMenu->actions().contains(save),
           "Macro menu should expose all wired actions");
    expect(start->isEnabled() && !stop->isEnabled() && !play->isEnabled(),
           "initial macro actions should be correct");

    ScintillaEditBase *source = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    start->trigger();
    expect(!start->isEnabled() && stop->isEnabled() && !play->isEnabled(),
           "recording action transition should be correct");
    source->send(SCI_REPLACESEL, 0, reinterpret_cast<Scintilla::sptr_t>("tab-bound"));
    action(window, "newAction")->trigger();
    ScintillaEditBase *target = tabs->currentWidget()->findChild<ScintillaEditBase *>();
    expect(target != source, "new tab should become active");
    stop->trigger();
    expect(start->isEnabled() && !stop->isEnabled() && play->isEnabled() &&
               repeat->isEnabled() && save->isEnabled(),
           "stopped action transition should be correct");
    play->trigger();
    expect(text(*source) == "tab-bound" && text(*target) == "tab-bound",
           "recording stays on source while playback targets active editor");
    delete window;
}

void testSavedMacroActionIsPlayable()
{
    QSettings().clear();
    ScintillaEditBase source;
    MacroManager seed;
    expect(seed.startRecording(&source), "seed recording should start");
    source.send(SCI_REPLACESEL, 0, reinterpret_cast<Scintilla::sptr_t>("saved"));
    expect(seed.stopRecording() && seed.saveCurrent("Saved item"), "seed macro should save");

    QMainWindow *window = createMainWindow();
    QAction *saved = action(window, "savedMacro:Saved item");
    auto *editor = window->findChild<QTabWidget *>()->currentWidget()
                       ->findChild<ScintillaEditBase *>();
    editor->send(SCI_SETTEXT, 0, reinterpret_cast<Scintilla::sptr_t>(""));
    saved->trigger();
    expect(text(*editor) == "saved", "saved menu action should select and play macro");
    delete window;
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settingsDirectory;
    expect(settingsDirectory.isValid(), "temporary settings directory required");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    qputenv("NPP_SESSION_DIR", (settingsDirectory.path() + "/sessions").toUtf8());
    QCoreApplication::setOrganizationName("NotepadPlusPlusMacroTests");
    QCoreApplication::setApplicationName("Backend");
    QSettings().clear();

    testRecordTypingPlaybackAndState();
    testOwnershipRepeatAndSafeEdges();
    testEditorDestructionDuringTransitions();
    testManagerDestructionStopsLiveRecordingEditor();
    testManagerDestructionDuringRecordTransitions();
    testManagerDestructionDuringPlaybackReentrancy();
    testPlaybackOperationBudget();
    testLiveCaptureBudgetsPreserveRecordedPrefix();
    testSaveBudgetsAreAtomic();
    testSaveEncodedOutputBudgetIsAtomic();
    testPersistenceRecreationOverwriteAndMalformed();
    testStrictPersistenceSchemaIsAtomic();
    testPersistenceAggregateBudgets();
    testPersistenceSyncFailuresAreAtomic(settingsDirectory.path());
    testMainWindowMenuWiringStateAndCrossTabTargeting();
    testSavedMacroActionIsPlayable();
    return 0;
}
