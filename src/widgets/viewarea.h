#ifndef VIEWAREA_H
#define VIEWAREA_H

#include <QWidget>
#include <QSharedPointer>

#include <functional>

#include <buffer/buffer.h>
#include "global.h"
#include "navigationmode.h"
#include "viewsplit.h"

class QLayout;
class QSplitter;

namespace vnotex
{
    class Buffer;
    class ViewWindow;
    class Event;
    class Notebook;
    struct FileOpenParameters;

    // Hold a list of ViewWindow. A ViewSplit could display any ViewWorkspace.
    // A ViewWorkspace could only be displayed by one ViewSplit at a time.
    // If a ViewWorkspace is visible, it is managed by its ViewSplit and
    // its state may not reflect the real fact.
    struct ViewWorkspace
    {
        explicit ViewWorkspace(ID p_id)
            : c_id(p_id)
        {
        }

        ~ViewWorkspace()
        {
            Q_ASSERT(m_viewWindows.isEmpty());
        }

        void clear()
        {
            m_visible = false;
            m_viewWindows.clear();
            m_currentViewWindowIndex = 0;
        }

        const ID c_id = 0;

        // Whether it is displayed by a ViewSplit now.
        bool m_visible = false;

        QVector<ViewWindow *> m_viewWindows;

        int m_currentViewWindowIndex = 0;
    };

    // ViewArea -- ViewSplit -- ViewWindow.
    // ViewSplit may be put within a splitter.
    // There may be multiple ViewWindow for the same buffer.
    class ViewArea : public QWidget, public NavigationMode
    {
        Q_OBJECT
    public:
        explicit ViewArea(QWidget *p_parent = nullptr);

        ~ViewArea();

        QSize sizeHint() const Q_DECL_OVERRIDE;

    public slots:
        void openBuffer(Buffer *p_buffer, const QSharedPointer<FileOpenParameters> &p_paras);

        bool close(const Notebook *p_notebook, bool p_force);

        ViewWindow *getCurrentViewWindow() const;

        void focus();

    // NavigationMode.
    protected:
        QVector<void *> getVisibleNavigationItems() Q_DECL_OVERRIDE;

        void placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) Q_DECL_OVERRIDE;

        void handleTargetHit(void *p_item) Q_DECL_OVERRIDE;

        void clearNavigation() Q_DECL_OVERRIDE;

    protected:
        // To accept specific drop.
        void dragEnterEvent(QDragEnterEvent *p_event) Q_DECL_OVERRIDE;

        // Drop the data.
        void dropEvent(QDropEvent *p_event) Q_DECL_OVERRIDE;

    signals:
        // Status widget of ViewArea is changed.
        // MainWindow should set the corresponding status widget accordingly.
        void statusWidgetChanged(QWidget *p_widget);

        // Count of ViewSplit is chagned.
        // Used internally.
        void viewSplitsCountChanged();

        void currentViewWindowChanged();

        // State of current view window has update.
        void currentViewWindowUpdated();

    private slots:
        // Return true if @p_win is closed.
        // @p_removeSplitIfEmpty: whether remove the workspace and split if @p_win is that only ViewWindow left.
        bool closeViewWindow(ViewWindow *p_win, bool p_force, bool p_removeSplitIfEmpty);

        void maximizeViewSplit(ViewSplit *p_split);

        void distributeViewSplits();

        void newWorkspaceInViewSplit(ViewSplit *p_split);

        // @p_insertNew: whether to find an available workspace for @p_split after removal.
        // Return true if removed.
        bool removeWorkspaceInViewSplit(ViewSplit *p_split, bool p_insertNew);

        void handleViewSplitsCountChange();

        bool close(bool p_force);

    private:
        enum class SplitType
        {
            Vertical,
            Horizontal
        };

        void setupUI();

        // Find given @p_buffer among all view splits.
        // Does not search invisible work spaces.
        QVector<ViewWindow *> findBufferInViewSplits(const Buffer *p_buffer) const;

        ViewSplit *createViewSplit(QWidget *p_parent);

        // A Scene widget will be used when there is no split.
        // Usually it is used to show some help message.
        void showSceneWidget();

        void hideSceneWidget();

        void addFirstViewSplit();

        // Close all the ViewWindows in @p_split if @p_removeWorkspace is true.
        // Otherwise, detach the Workpace from it and remove the split.
        void removeViewSplit(ViewSplit *p_split, bool p_removeWorkspace);

        void setCurrentViewWindow(ViewWindow *p_win);

        ViewSplit *getCurrentViewSplit() const;
        void setCurrentViewSplit(ViewSplit *p_split, bool p_focus);

        QSharedPointer<ViewWorkspace> createWorkspace();

        void splitViewSplit(ViewSplit *p_split, SplitType p_type);

        QSplitter *tryGetParentSplitter(const QWidget *p_widget) const;

        // Distribute all splits within @p_splitter recursively.
        void distributeViewSplitsOfSplitter(QSplitter *p_splitter);

        void unwrapSplitter(QSplitter *p_splitter);

        void maximizeWidgetOfSplitter(QWidget *p_widget);

        QSplitter *createSplitter(Qt::Orientation p_orientation, QWidget *p_parent) const;

        // Do not use the reference since it will remove the value in @m_workspaces.
        // Should have done the clean up of ViewWindow before calling this.
        void removeWorkspace(QSharedPointer<ViewWorkspace> p_workspace);

        bool shouldUseGlobalStatusWidget() const;

        // Iterate through all ViewWindows including both ViewSplits and Workspaces.
        // Should NOT use this function to close ViewWindow.
        void forEachViewWindow(const ViewSplit::ViewWindowSelector &p_func);

        void setupShortcuts();

        // Close all ViewWindows related to @p_node.
        bool close(Node *p_node, bool p_force);

        // Go through all ViewWindows and judge whether to close it by @p_func.
        bool closeIf(bool p_force,
                     const ViewSplit::ViewWindowSelector &p_func,
                     bool p_closeEmptySplit);

        void checkCurrentViewWindowChange();

        QVector<ViewWindow *> getAllViewWindows(ViewSplit *p_split, const ViewSplit::ViewWindowSelector &p_func);

        QLayout *m_mainLayout = nullptr;

        QWidget *m_sceneWidget = nullptr;

        QVector<ViewSplit *> m_splits;

        QVector<QSharedPointer<ViewWorkspace>> m_workspaces;

        ViewSplit *m_currentSplit = nullptr;

        ViewWindow *m_currentWindow = nullptr;

        // Current global status widget.
        // When there is no split window, we will make the status widget global.
        // Otherwise, we will display the status widget at the bottom of each view window.
        QSharedPointer<QWidget> m_currentStatusWidget;

        QVector<ViewSplit::ViewWindowNavigationModeInfo> m_navigationItems;

        // Timer to check file change outside periodically.
        QTimer *m_fileCheckTimer = nullptr;
    };
} // ns vnotex

#endif // VIEWAREA_H
