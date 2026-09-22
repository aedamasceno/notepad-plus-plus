#include "mainwindow.h"
#include "dualviewmanager.h"
#include "editorutils.h"
#include "sessionmanager.h"
#include "ScintillaEditBase.h"

#include <QAction>
#include <QApplication>
#include <QMainWindow>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <cstdio>

namespace {
int failures = 0;
class ReplaceableTabWidget : public QTabWidget {
public:
    void replaceTabBar(QTabBar *bar) { setTabBar(bar); }
};

void expect(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
QAction *action(QObject *root, const char *name) { return root->findChild<QAction *>(name); }
ScintillaEditBase *editor(QTabWidget *tabs) {
    return tabs && tabs->currentWidget()
        ? tabs->currentWidget()->findChild<ScintillaEditBase *>() : nullptr;
}
ScintillaEditBase *editorAt(QTabWidget *tabs, int index) {
    return tabs && tabs->widget(index)
        ? tabs->widget(index)->findChild<ScintillaEditBase *>() : nullptr;
}
void process() { QApplication::processEvents(); }

QByteArray numberedLines(int count, int width) {
    QByteArray text;
    for (int i = 0; i < count; ++i)
        text += QByteArray::number(i) + QByteArray(width, 'x') + '\n';
    return text;
}
void testPaneArchitectureAndSharedDocument() {
    QTemporaryDir storage;
    qputenv("NPP_SESSION_DIR", storage.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *splitter = window->findChild<QSplitter *>("editorViewSplitter");
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    auto *move = action(window.get(), "moveToOtherViewAction");
    auto *clone = action(window.get(), "cloneToOtherViewAction");
    auto *vertical = action(window.get(), "synchronizeVerticalScrollingAction");
    auto *horizontal = action(window.get(), "synchronizeHorizontalScrollingAction");
    expect(splitter && primary && secondary, "stable splitter and pane object names");
    expect(window->findChild<QTabWidget *>() == primary, "primary remains first QTabWidget");
    expect(secondary && secondary->isHidden() && secondary->count() == 0,
           "secondary starts hidden and empty");
    expect(move && clone && vertical && horizontal, "dual-view actions exist");
    expect(vertical && vertical->isCheckable() && horizontal && horizontal->isCheckable(),
           "scroll synchronization actions are checkable");
    auto *first = editor(primary);
    const QByteArray utf8 = QStringLiteral("one λ\ntwo\nthree\n").toUtf8();
    first->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(utf8.constData()));
    clone->trigger();
    QApplication::processEvents();
    expect(!secondary->isHidden() && secondary->count() == 1, "clone reveals other pane");
    auto *second = editor(secondary);
    expect(second && first->send(SCI_GETDOCPOINTER) == second->send(SCI_GETDOCPOINTER),
           "clones share exactly one Scintilla document pointer");
    const QByteArray changed = QStringLiteral("shared 雪").toUtf8();
    second->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(changed.constData()));
    expect(EditorUtils::text(first) == changed, "UTF-8 edits propagate bidirectionally");
    first->send(SCI_SETSEL, 0, 2); second->send(SCI_SETSEL, 3, 5);
    expect(first->send(SCI_GETANCHOR) != second->send(SCI_GETANCHOR),
           "clone selections remain per-view");
    move->trigger();
    QApplication::processEvents();
    expect(primary->count() == 1 && secondary->count() == 0 && secondary->isHidden(),
           "moving a clone removes only the source view and activates the existing target");

    clone->trigger();
    QApplication::processEvents();
    primary->setCurrentIndex(0);
    QMetaObject::invokeMethod(primary, "tabCloseRequested", Q_ARG(int, 0));
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    expect(primary->count() == 0 && secondary->count() == 1 &&
               EditorUtils::text(editor(secondary)) == changed,
           "destroying one clone preserves shared document lifetime without a prompt");
    expect(vertical->isEnabled() == false && horizontal->isEnabled() == false &&
               !vertical->isChecked() && !horizontal->isChecked(),
           "one-pane state disables and clears synchronization");
    auto *session = window->findChild<SessionManager *>();
    session->flush();
    expect(session->primaryDocumentIds().isEmpty() && session->secondaryDocumentIds().size() == 1,
           "closing one clone checkpoints pane membership without a stale source id");
}

void testMoveAndScrollSynchronization() {
    QTemporaryDir storage;
    qputenv("NPP_SESSION_DIR", storage.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    auto *move = action(window.get(), "moveToOtherViewAction");
    auto *clone = action(window.get(), "cloneToOtherViewAction");
    auto *vertical = action(window.get(), "synchronizeVerticalScrollingAction");
    auto *horizontal = action(window.get(), "synchronizeHorizontalScrollingAction");
    auto *first = editor(primary);
    QByteArray lines;
    for (int i = 0; i < 200; ++i) lines += QByteArray::number(i) + QByteArray(100, 'x') + '\n';
    first->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(lines.constData()));
    const auto pointer = first->send(SCI_GETDOCPOINTER);
    move->trigger();
    expect(primary->count() == 0 && secondary->count() == 1 &&
               editor(secondary)->send(SCI_GETDOCPOINTER) == pointer,
           "move preserves document identity and activates destination");
    move->trigger();
    expect(primary->count() == 1 && secondary->count() == 0 && secondary->isHidden(),
           "moving back hides empty secondary");
    clone->trigger();
    first = editor(primary); auto *second = editor(secondary);
    first->send(SCI_LINESCROLL, 0, 5); second->send(SCI_LINESCROLL, 0, 11);
    const int offset = int(second->send(SCI_GETFIRSTVISIBLELINE) - first->send(SCI_GETFIRSTVISIBLELINE));
    vertical->trigger();
    first->send(SCI_LINESCROLL, 0, 7); QApplication::processEvents();
    expect(int(second->send(SCI_GETFIRSTVISIBLELINE) - first->send(SCI_GETFIRSTVISIBLELINE)) == offset,
           "vertical synchronization preserves captured relative offset");
    second->send(SCI_LINESCROLL, 0, 4); QApplication::processEvents();
    expect(int(second->send(SCI_GETFIRSTVISIBLELINE) - first->send(SCI_GETFIRSTVISIBLELINE)) == offset,
           "vertical synchronization is bidirectional without recursion");
    first->send(SCI_SETXOFFSET, 20); second->send(SCI_SETXOFFSET, 45);
    horizontal->trigger();
    first->send(SCI_SETXOFFSET, 30); QApplication::processEvents();
    expect(second->send(SCI_GETXOFFSET) == 55, "horizontal synchronization preserves offset");
    vertical->trigger(); horizontal->trigger();
    const int oldSecond = int(second->send(SCI_GETFIRSTVISIBLELINE));
    first->send(SCI_LINESCROLL, 0, 3); QApplication::processEvents();
    expect(second->send(SCI_GETFIRSTVISIBLELINE) == oldSecond,
           "disabled synchronization leaves views independent");
}

void testWrappedVerticalSynchronization() {
    QTemporaryDir storage;
    qputenv("NPP_SESSION_DIR", storage.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->resize(1000, 500); window->show(); process();
    auto *splitter = window->findChild<QSplitter *>("editorViewSplitter");
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    auto *move = action(window.get(), "moveToOtherViewAction");
    auto *newDocument = action(window.get(), "newAction");
    auto *vertical = action(window.get(), "synchronizeVerticalScrollingAction");

    const QByteArray longDocument = numberedLines(90, 180);
    editor(primary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(longDocument.constData()));
    move->trigger();                 // long document in secondary
    newDocument->trigger();          // new short document in secondary
    move->trigger();                 // short document in primary
    const QByteArray shortDocument = numberedLines(18, 120);
    editor(primary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(shortDocument.constData()));
    splitter->setSizes({250, 750}); process();
    auto *first = editor(primary); auto *second = editor(secondary);
    first->send(SCI_SETWRAPMODE, SC_WRAP_WORD); second->send(SCI_SETWRAPMODE, SC_WRAP_WORD);
    process();
    first->send(SCI_LINESCROLL, 0, 9); second->send(SCI_LINESCROLL, 0, 21); process();
    const int offset = int(second->send(SCI_GETFIRSTVISIBLELINE) - first->send(SCI_GETFIRSTVISIBLELINE));
    vertical->trigger();
    first->send(SCI_LINESCROLL, 0, 5); process();
    expect(int(second->send(SCI_GETFIRSTVISIBLELINE) - first->send(SCI_GETFIRSTVISIBLELINE)) == offset,
           "wrapped vertical sync preserves display-subline offset from primary");
    second->send(SCI_LINESCROLL, 0, 4); process();
    expect(int(second->send(SCI_GETFIRSTVISIBLELINE) - first->send(SCI_GETFIRSTVISIBLELINE)) == offset,
           "wrapped vertical sync uses the same relative offset in reverse without recursion");

    first->send(SCI_LINESCROLL, 0, -100000); process();
    expect(first->send(SCI_GETFIRSTVISIBLELINE) == 0 && second->send(SCI_GETFIRSTVISIBLELINE) >= 0,
           "wrapped sync clamps safely at the top display boundary");
    second->send(SCI_LINESCROLL, 0, 100000); process();
    const int lastLine = int(first->send(SCI_GETLINECOUNT)) - 1;
    const int lastDisplay = int(first->send(SCI_VISIBLEFROMDOCLINE, lastLine) +
                                first->send(SCI_WRAPCOUNT, lastLine) - 1);
    expect(first->send(SCI_GETFIRSTVISIBLELINE) <= lastDisplay,
           "wrapped sync clamps to the actual display-line range at the bottom");
    vertical->trigger();
    const auto oldSecond = second->send(SCI_GETFIRSTVISIBLELINE);
    first->send(SCI_LINESCROLL, 0, -3); process();
    expect(second->send(SCI_GETFIRSTVISIBLELINE) == oldSecond,
           "disabling wrapped vertical sync leaves panes independent");
}

void testActionStateAndPrimaryActivation() {
    QTemporaryDir storage;
    qputenv("NPP_SESSION_DIR", storage.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->show(); process();
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    auto *clone = action(window.get(), "cloneToOtherViewAction");
    auto *move = action(window.get(), "moveToOtherViewAction");
    auto *newDocument = action(window.get(), "newAction");
    const auto firstDocument = editor(primary)->send(SCI_GETDOCPOINTER);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(2, 2), QPointF(2, 2),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    clone->trigger();
    QApplication::sendEvent(editor(primary), &press); process();
    newDocument->trigger(); // create uncloned B in primary
    QApplication::sendEvent(editor(secondary), &press); process();
    expect(!clone->isEnabled(), "clone is disabled while cloned A is active");
    primary->setCurrentIndex(0); process();
    primary->setCurrentIndex(primary->count() - 1); process();
    expect(clone->isEnabled(), "switching from cloned A to uncloned B refreshes clone action state");

    QApplication::sendEvent(editor(primary), &press); process();
    move->trigger(); process();
    expect(editor(secondary) && editor(secondary)->send(SCI_GETDOCPOINTER) != firstDocument,
           "clicking primary reactivates it so commands target the primary document");
}

void testSelectedTabPressActivatesInactivePane() {
    QTemporaryDir storage;
    qputenv("NPP_SESSION_DIR", storage.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->show(); process();
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    auto *manager = window->findChild<DualViewManager *>();
    auto *clone = action(window.get(), "cloneToOtherViewAction");
    auto *move = action(window.get(), "moveToOtherViewAction");

    clone->trigger(); process();
    expect(manager->activePane() == secondary && primary->currentIndex() == 0,
           "selected-tab activation fixture starts with primary inactive");
    QTabBar *primaryBar = primary->tabBar();
    const QPoint tabCenter = primaryBar->tabRect(primary->currentIndex()).center();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(tabCenter), QPointF(tabCenter),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(primaryBar, &press); process();

    expect(manager->activePane() == primary,
           "pressing the selected tab in the inactive pane activates that pane");
    move->trigger(); process();
    expect(primary->count() == 0 && secondary->count() == 1,
           "active-view action routes through the pane activated by its selected tab");
}

void testReplacementTabBarKeepsPaneActivationTracking() {
    QSplitter splitter;
    auto *primary = new ReplaceableTabWidget;
    auto *secondary = new ReplaceableTabWidget;
    splitter.addWidget(primary);
    splitter.addWidget(secondary);
    DualViewManager manager(&splitter, primary, secondary);
    primary->replaceTabBar(new QTabBar);
    primary->addTab(new QWidget, QStringLiteral("Primary"));
    secondary->addTab(new QWidget, QStringLiteral("Secondary"));
    manager.updatePaneVisibility();
    splitter.show(); process();
    manager.activate(secondary);

    QTabBar *primaryBar = primary->tabBar();
    const QPoint tabCenter = primaryBar->tabRect(0).center();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(tabCenter), QPointF(tabCenter),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(primaryBar, &press); process();

    expect(manager.activePane() == primary,
           "replacement tab bars retain inactive-pane activation tracking");
}

void testCloseNonCurrentCloneCheckpointsLayout() {
    QTemporaryDir storage;
    qputenv("NPP_SESSION_DIR", storage.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->show(); process();
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    const QByteArray firstText("first document");
    editor(primary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(firstText.constData()));
    action(window.get(), "cloneToOtherViewAction")->trigger();
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(2, 2), QPointF(2, 2),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor(primary), &press); process();
    action(window.get(), "newAction")->trigger();
    const QByteArray secondText("second document");
    editor(primary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(secondText.constData()));
    process();
    expect(primary->count() == 2 && primary->currentIndex() == 1 && secondary->count() == 1,
           "non-current clone close fixture is established");
    {
        const QSignalBlocker suppressIncidentalCurrentChange(primary);
        QMetaObject::invokeMethod(window.get(), "tabCloseRequested", Q_ARG(int, 0));
    }
    process();
    auto *session = window->findChild<SessionManager *>();
    expect(session->primaryDocumentIds().size() == 1,
           "closing a non-current clone removes its primary pane assignment");
    expect(session->secondaryDocumentIds().size() == 1,
           "closing a non-current clone preserves its secondary pane assignment");
    expect(!session->primaryDocumentIds().isEmpty() && !session->secondaryDocumentIds().isEmpty() &&
               session->primaryDocumentIds().first() != session->secondaryDocumentIds().first(),
           "closing a non-current clone leaves distinct documents in each pane");
}

void testActivePaneAndUiRefreshWhenSecondaryEmpties() {
    QTemporaryDir storage;
    qputenv("NPP_SESSION_DIR", storage.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->show(); process();
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    auto *manager = window->findChild<DualViewManager *>();
    auto *clone = action(window.get(), "cloneToOtherViewAction");
    clone->trigger(); process();
    int activeChanges = 0;
    QObject::connect(manager, &DualViewManager::activePaneChanged,
                     [&activeChanges] { ++activeChanges; });
    QMetaObject::invokeMethod(secondary, "tabCloseRequested", Q_ARG(int, 0));
    process();
    expect(manager->activePane() == primary && activeChanges == 1,
           "emptying the active secondary pane emits one primary activation change");
    expect(clone->isEnabled(),
           "implicit primary activation refreshes dependent dual-view action state");
}

void testSyncOffsetsRecapturedOnEitherPaneTabChange() {
    QTemporaryDir storage;
    qputenv("NPP_SESSION_DIR", storage.path().toUtf8());
    std::unique_ptr<QMainWindow> window(createMainWindow());
    window->resize(1000, 500); window->show(); process();
    auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = window->findChild<QTabWidget *>("secondaryTabWidget");
    auto *move = action(window.get(), "moveToOtherViewAction");
    auto *newDocument = action(window.get(), "newAction");
    auto *vertical = action(window.get(), "synchronizeVerticalScrollingAction");
    auto *horizontal = action(window.get(), "synchronizeHorizontalScrollingAction");

    const QByteArray text = numberedLines(200, 120);
    editor(primary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.constData()));
    move->trigger();
    newDocument->trigger(); editor(secondary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.constData()));
    move->trigger();
    newDocument->trigger(); editor(primary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.constData()));
    move->trigger();
    newDocument->trigger(); editor(secondary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.constData()));
    move->trigger();
    expect(primary->count() == 2 && secondary->count() == 2,
           "sync tab-change fixture has two documents in each pane");

    auto position = [](ScintillaEditBase *view, int line, int x) {
        view->send(SCI_LINESCROLL, 0, line);
        view->send(SCI_SETXOFFSET, x);
    };
    position(editorAt(primary, 0), 5, 10);
    position(editorAt(primary, 1), 25, 40);
    position(editorAt(secondary, 0), 12, 20);
    position(editorAt(secondary, 1), 45, 70);
    primary->setCurrentIndex(0); secondary->setCurrentIndex(0); process();
    vertical->trigger(); horizontal->trigger();

    const int secondaryLineBefore = int(editorAt(secondary, 1)->send(SCI_GETFIRSTVISIBLELINE));
    const int secondaryXBefore = int(editorAt(secondary, 1)->send(SCI_GETXOFFSET));
    secondary->setCurrentIndex(1); process();
    expect(editorAt(secondary, 1)->send(SCI_GETFIRSTVISIBLELINE) == secondaryLineBefore &&
               editorAt(secondary, 1)->send(SCI_GETXOFFSET) == secondaryXBefore,
           "changing the secondary tab recaptures sync offsets without snapping");
    editorAt(primary, 0)->send(SCI_LINESCROLL, 0, 3);
    editorAt(primary, 0)->send(SCI_SETXOFFSET, 15); process();
    expect(editorAt(secondary, 1)->send(SCI_GETFIRSTVISIBLELINE) -
               editorAt(primary, 0)->send(SCI_GETFIRSTVISIBLELINE) == secondaryLineBefore - 5 &&
               editorAt(secondary, 1)->send(SCI_GETXOFFSET) -
               editorAt(primary, 0)->send(SCI_GETXOFFSET) == secondaryXBefore - 10,
           "secondary tab change uses the newly captured vertical and horizontal offsets");

    const int primaryLineBefore = int(editorAt(primary, 1)->send(SCI_GETFIRSTVISIBLELINE));
    const int primaryXBefore = int(editorAt(primary, 1)->send(SCI_GETXOFFSET));
    const int secondaryLineAtPrimaryChange = int(editorAt(secondary, 1)->send(SCI_GETFIRSTVISIBLELINE));
    const int secondaryXAtPrimaryChange = int(editorAt(secondary, 1)->send(SCI_GETXOFFSET));
    primary->setCurrentIndex(1); process();
    expect(editorAt(primary, 1)->send(SCI_GETFIRSTVISIBLELINE) == primaryLineBefore &&
               editorAt(primary, 1)->send(SCI_GETXOFFSET) == primaryXBefore,
           "changing the primary tab recaptures sync offsets without snapping");
    editorAt(secondary, 1)->send(SCI_LINESCROLL, 0, 2);
    editorAt(secondary, 1)->send(SCI_SETXOFFSET, secondaryXAtPrimaryChange + 8); process();
    expect(editorAt(secondary, 1)->send(SCI_GETFIRSTVISIBLELINE) -
               editorAt(primary, 1)->send(SCI_GETFIRSTVISIBLELINE) == secondaryLineAtPrimaryChange - primaryLineBefore &&
               editorAt(secondary, 1)->send(SCI_GETXOFFSET) -
               editorAt(primary, 1)->send(SCI_GETXOFFSET) == secondaryXAtPrimaryChange - primaryXBefore,
           "primary tab change uses the newly captured vertical and horizontal offsets");
}

void testDualSessionRoundTrip() {
    QTemporaryDir storage;
    qputenv("NPP_SESSION_DIR", storage.path().toUtf8());
    {
        std::unique_ptr<QMainWindow> window(createMainWindow());
        auto *primary = window->findChild<QTabWidget *>("primaryTabWidget");
        const QByteArray content("persistent clone\nsecond line");
        editor(primary)->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(content.constData()));
        action(window.get(), "cloneToOtherViewAction")->trigger();
        window->close();
    }
    std::unique_ptr<QMainWindow> restored(createMainWindow());
    auto *primary = restored->findChild<QTabWidget *>("primaryTabWidget");
    auto *secondary = restored->findChild<QTabWidget *>("secondaryTabWidget");
    expect(primary->count() == 1 && secondary->count() == 1,
           "dual session restores pane assignment and clone-both state");
    expect(editor(primary)->send(SCI_GETDOCPOINTER) == editor(secondary)->send(SCI_GETDOCPOINTER),
           "restored clones retain one shared document identity");
    editor(primary)->send(SCI_GOTOPOS, 0);
    editor(secondary)->send(SCI_GOTOPOS, 20);
    process();
    expect(restored->statusBar()->currentMessage().contains(QStringLiteral("Ln 2")),
           "restored clones retain active-view status update wiring");
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("npp-dualview-tests");
    QCoreApplication::setApplicationName("npp-dualview-tests");
    testPaneArchitectureAndSharedDocument();
    testMoveAndScrollSynchronization();
    testWrappedVerticalSynchronization();
    testActionStateAndPrimaryActivation();
    testSelectedTabPressActivatesInactivePane();
    testReplacementTabBarKeepsPaneActivationTracking();
    testCloseNonCurrentCloneCheckpointsLayout();
    testActivePaneAndUiRefreshWhenSecondaryEmpties();
    testSyncOffsetsRecapturedOnEitherPaneTabChange();
    testDualSessionRoundTrip();
    return failures == 0 ? 0 : 1;
}
