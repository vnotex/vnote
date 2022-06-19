#ifndef UNITEDENTRY_H
#define UNITEDENTRY_H

#include <QFrame>
#include <QSharedPointer>

class QAction;
class QTimer;
class QTreeWidget;
class QLabel;
class QMainWindow;

namespace vnotex
{
    class LineEditWithSnippet;
    class EntryPopup;
    class IUnitedEntry;

    class UnitedEntry : public QFrame
    {
        Q_OBJECT
    public:
        explicit UnitedEntry(QMainWindow *p_mainWindow);

        ~UnitedEntry();

        bool eventFilter(QObject *p_watched, QEvent *p_event) Q_DECL_OVERRIDE;

        QAction *getTriggerAction();

    protected:
        void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupActions();

        void activateUnitedEntry();

        void deactivateUnitedEntry();

        void clear();

        void processInput();

        void handleFocusChanged(QWidget *p_old, QWidget *p_now);

        const QSharedPointer<QTreeWidget> &getEntryListWidget();

        QSharedPointer<QLabel> getInfoWidget(const QString &p_info);

        void popupWidget(const QSharedPointer<QWidget> &p_widget);

        // Return true if there is any entry visible.
        bool filterEntryListWidgetEntries(const QString &p_name);

        void handleEntryFinished(IUnitedEntry *p_entry);

        void handleEntryItemActivated(IUnitedEntry *p_entry, bool p_quit, bool p_restoreFocus);

        void setBusy(bool p_busy);

        void exitUnitedEntry();

        void updateGeometryToContents();

        QSize preferredSize() const;

        // Return true if want to stop the propogation.
        bool handleLineEditKeyPress(QKeyEvent *p_event);

        QMainWindow *m_mainWindow = nullptr;

        LineEditWithSnippet *m_lineEdit = nullptr;

        EntryPopup *m_popup = nullptr;

        QAction *m_menuIconAction = nullptr;

        QAction *m_busyIconAction = nullptr;

        bool m_activated = false;

        QWidget *m_previousFocusWidget = nullptr;

        QTimer *m_processTimer = nullptr;

        QSharedPointer<QTreeWidget> m_entryListWidget = nullptr;

        QSharedPointer<IUnitedEntry> m_lastEntry;

        bool m_hasPending = false;
    };
}

#endif // UNITEDENTRY_H
