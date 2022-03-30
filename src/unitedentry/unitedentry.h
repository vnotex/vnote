#ifndef UNITEDENTRY_H
#define UNITEDENTRY_H

#include <QWidget>
#include <QSharedPointer>

class QAction;
class QTimer;
class QTreeWidget;
class QLabel;

namespace vnotex
{
    class LineEditWithSnippet;
    class EntryPopup;
    class IUnitedEntry;

    class UnitedEntry : public QWidget
    {
        Q_OBJECT
    public:
        explicit UnitedEntry(QWidget *p_parent = nullptr);

        ~UnitedEntry();

        bool eventFilter(QObject *p_watched, QEvent *p_event) Q_DECL_OVERRIDE;

    protected:
        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

        void resizeEvent(QResizeEvent *p_event) Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void setupIcons();

        void activateUnitedEntry();

        void deactivateUnitedEntry();

        void handleFocusChanged(QWidget *p_old, QWidget *p_now);

        void clear();

        void processInput();

        const QSharedPointer<QTreeWidget> &getEntryListWidget();

        QSharedPointer<QLabel> getInfoWidget(const QString &p_info);

        void updatePopupGeometry();

        void popupWidget(const QSharedPointer<QWidget> &p_widget);

        // Return true if there is any entry visible.
        bool filterEntryListWidgetEntries(const QString &p_name);

        void handleEntryFinished(IUnitedEntry *p_entry);

        void setBusy(bool p_busy);

        void exitUnitedEntry();

        LineEditWithSnippet *m_lineEdit = nullptr;

        EntryPopup *m_popup = nullptr;

        QAction *m_iconAction = nullptr;

        bool m_activated = false;

        QWidget *m_previousFocusWidget = nullptr;

        QTimer *m_processTimer = nullptr;

        QSharedPointer<QTreeWidget> m_entryListWidget = nullptr;

        QSharedPointer<IUnitedEntry> m_lastEntry;

        bool m_hasPending = false;
    };
}

#endif // UNITEDENTRY_H
