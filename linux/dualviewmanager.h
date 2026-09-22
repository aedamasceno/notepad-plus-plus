#pragma once

#include <QObject>

class QAction;
class QSplitter;
class QTabWidget;
class QWidget;
class ScintillaEditBase;

class DualViewManager : public QObject
{
    Q_OBJECT
public:
    explicit DualViewManager(QSplitter *splitter, QTabWidget *primary,
                             QTabWidget *secondary, QObject *parent = nullptr);
    QTabWidget *primary() const { return m_primary; }
    QTabWidget *secondary() const { return m_secondary; }
    QTabWidget *activePane() const { return m_active; }
    QTabWidget *otherPane(QTabWidget *pane = nullptr) const;
    void activate(QTabWidget *pane, int index = -1);
    void updatePaneVisibility();
    void bindSyncActions(QAction *vertical, QAction *horizontal);
    void setVerticalSync(bool enabled);
    void setHorizontalSync(bool enabled);
    void recaptureSyncOffsets();
    bool verticalSync() const { return m_verticalSync; }
    bool horizontalSync() const { return m_horizontalSync; }
    void connectEditor(ScintillaEditBase *editor);

signals:
    void activePaneChanged();
    void paneAvailabilityChanged(bool available);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void installPaneEventFilters(QTabWidget *pane);
    ScintillaEditBase *currentEditor(QTabWidget *pane) const;
    void syncVerticalFrom(ScintillaEditBase *source);
    void syncHorizontalFrom(ScintillaEditBase *source);
    QSplitter *m_splitter;
    QTabWidget *m_primary;
    QTabWidget *m_secondary;
    QTabWidget *m_active;
    QAction *m_verticalAction = nullptr;
    QAction *m_horizontalAction = nullptr;
    bool m_verticalSync = false;
    bool m_horizontalSync = false;
    bool m_syncing = false;
    int m_verticalOffset = 0;
    int m_horizontalOffset = 0;
};
