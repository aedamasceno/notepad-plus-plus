#include "dualviewmanager.h"
#include "ScintillaEditBase.h"

#include <QAction>
#include <QChildEvent>
#include <QEvent>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>

DualViewManager::DualViewManager(QSplitter *splitter, QTabWidget *primary,
                                 QTabWidget *secondary, QObject *parent)
    : QObject(parent), m_splitter(splitter), m_primary(primary), m_secondary(secondary),
      m_active(primary)
{
    installPaneEventFilters(m_primary);
    installPaneEventFilters(m_secondary);
    updatePaneVisibility();
}

void DualViewManager::installPaneEventFilters(QTabWidget *pane)
{
    pane->installEventFilter(this);
    pane->tabBar()->installEventFilter(this);
}

QTabWidget *DualViewManager::otherPane(QTabWidget *pane) const
{
    pane = pane ? pane : m_active;
    return pane == m_primary ? m_secondary : m_primary;
}

void DualViewManager::activate(QTabWidget *pane, int index)
{
    if (!pane) return;
    if (index >= 0) pane->setCurrentIndex(index);
    if (m_active != pane) {
        m_active = pane;
        emit activePaneChanged();
    }
    if (auto *editor = currentEditor(pane)) editor->setFocus();
}

bool DualViewManager::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ChildAdded &&
        (watched == m_primary || watched == m_secondary)) {
        auto *child = static_cast<QChildEvent *>(event)->child();
        if (auto *tabBar = qobject_cast<QTabBar *>(child))
            tabBar->installEventFilter(this);
    }
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) {
        auto *widget = qobject_cast<QWidget *>(watched);
        QTabWidget *pane = nullptr;
        if (watched == m_primary || (widget && m_primary->isAncestorOf(widget))) pane = m_primary;
        else if (watched == m_secondary || (widget && m_secondary->isAncestorOf(widget))) pane = m_secondary;
        if (pane && m_active != pane) { m_active = pane; emit activePaneChanged(); }
    }
    return QObject::eventFilter(watched, event);
}

void DualViewManager::updatePaneVisibility()
{
    const bool available = m_primary->count() > 0 && m_secondary->count() > 0;
    m_secondary->setVisible(m_secondary->count() > 0);
    const bool activePaneChanged = m_secondary->count() == 0 && m_active != m_primary;
    if (activePaneChanged) m_active = m_primary;
    if (!available) {
        m_verticalSync = m_horizontalSync = false;
        if (m_verticalAction) { m_verticalAction->setChecked(false); m_verticalAction->setEnabled(false); }
        if (m_horizontalAction) { m_horizontalAction->setChecked(false); m_horizontalAction->setEnabled(false); }
    } else {
        if (m_verticalAction) m_verticalAction->setEnabled(true);
        if (m_horizontalAction) m_horizontalAction->setEnabled(true);
    }
    if (activePaneChanged) emit this->activePaneChanged();
    emit paneAvailabilityChanged(available);
}

void DualViewManager::bindSyncActions(QAction *vertical, QAction *horizontal)
{
    m_verticalAction = vertical; m_horizontalAction = horizontal;
    updatePaneVisibility();
}

ScintillaEditBase *DualViewManager::currentEditor(QTabWidget *pane) const
{
    return pane && pane->currentWidget()
        ? pane->currentWidget()->findChild<ScintillaEditBase *>() : nullptr;
}

void DualViewManager::setVerticalSync(bool enabled)
{
    if (enabled && m_primary->count() && m_secondary->count()) {
        auto *a = currentEditor(m_primary); auto *b = currentEditor(m_secondary);
        if (a && b) m_verticalOffset = int(b->send(SCI_GETFIRSTVISIBLELINE) - a->send(SCI_GETFIRSTVISIBLELINE));
        m_verticalSync = true;
    } else m_verticalSync = false;
    if (m_verticalAction) m_verticalAction->setChecked(m_verticalSync);
}

void DualViewManager::setHorizontalSync(bool enabled)
{
    if (enabled && m_primary->count() && m_secondary->count()) {
        auto *a = currentEditor(m_primary); auto *b = currentEditor(m_secondary);
        if (a && b) m_horizontalOffset = int(b->send(SCI_GETXOFFSET) - a->send(SCI_GETXOFFSET));
        m_horizontalSync = true;
    } else m_horizontalSync = false;
    if (m_horizontalAction) m_horizontalAction->setChecked(m_horizontalSync);
}

void DualViewManager::recaptureSyncOffsets()
{
    auto *primaryEditor = currentEditor(m_primary);
    auto *secondaryEditor = currentEditor(m_secondary);
    if (!primaryEditor || !secondaryEditor)
        return;
    if (m_verticalSync)
        m_verticalOffset = int(secondaryEditor->send(SCI_GETFIRSTVISIBLELINE) -
                               primaryEditor->send(SCI_GETFIRSTVISIBLELINE));
    if (m_horizontalSync)
        m_horizontalOffset = int(secondaryEditor->send(SCI_GETXOFFSET) -
                                 primaryEditor->send(SCI_GETXOFFSET));
}

void DualViewManager::connectEditor(ScintillaEditBase *editor)
{
    editor->installEventFilter(this);
    connect(editor, &ScintillaEditBase::verticalScrolled, this,
            [this, editor](int) { syncVerticalFrom(editor); });
    connect(editor, &ScintillaEditBase::horizontalScrolled, this,
            [this, editor](int) { syncHorizontalFrom(editor); });
}

void DualViewManager::syncVerticalFrom(ScintillaEditBase *source)
{
    if (!m_verticalSync || m_syncing) return;
    auto *primaryEditor = currentEditor(m_primary); auto *secondaryEditor = currentEditor(m_secondary);
    if (!primaryEditor || !secondaryEditor || (source != primaryEditor && source != secondaryEditor)) return;
    auto *target = source == primaryEditor ? secondaryEditor : primaryEditor;
    const int sign = source == primaryEditor ? 1 : -1;
    const int sourceVisible = int(source->send(SCI_GETFIRSTVISIBLELINE));
    int desired = sourceVisible + sign * m_verticalOffset;
    const int lastDocumentLine = qMax(0, int(target->send(SCI_GETLINECOUNT)) - 1);
    const int lastDisplayLine = int(target->send(SCI_VISIBLEFROMDOCLINE, lastDocumentLine) +
                                    target->send(SCI_WRAPCOUNT, lastDocumentLine) - 1);
    desired = qBound(0, desired, qMax(0, lastDisplayLine));
    const int delta = desired - int(target->send(SCI_GETFIRSTVISIBLELINE));
    m_syncing = true;
    if (delta) target->send(SCI_LINESCROLL, 0, delta);
    m_syncing = false;
}

void DualViewManager::syncHorizontalFrom(ScintillaEditBase *source)
{
    if (!m_horizontalSync || m_syncing) return;
    auto *primaryEditor = currentEditor(m_primary); auto *secondaryEditor = currentEditor(m_secondary);
    if (!primaryEditor || !secondaryEditor || (source != primaryEditor && source != secondaryEditor)) return;
    auto *target = source == primaryEditor ? secondaryEditor : primaryEditor;
    const int sign = source == primaryEditor ? 1 : -1;
    const int desired = qMax(0, int(source->send(SCI_GETXOFFSET)) + sign * m_horizontalOffset);
    m_syncing = true; target->send(SCI_SETXOFFSET, desired); m_syncing = false;
}
