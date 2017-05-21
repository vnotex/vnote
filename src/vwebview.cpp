#include "vwebview.h"

#include <QMenu>
#include <QPoint>
#include <QContextMenuEvent>
#include <QWebEnginePage>
#include <QAction>
#include <QList>
#include "vfile.h"

VWebView::VWebView(VFile *p_file, QWidget *p_parent)
    : QWebEngineView(p_parent), m_file(p_file)
{
}

void VWebView::contextMenuEvent(QContextMenuEvent *p_event)
{
    QMenu *menu = page()->createStandardContextMenu();
    menu->setToolTipsVisible(true);

    const QList<QAction *> actions = menu->actions();

    if (!hasSelection() && m_file && m_file->isModifiable()) {
        QAction *editAct= new QAction(QIcon(":/resources/icons/edit_note.svg"),
                                          tr("&Edit"), this);
        editAct->setToolTip(tr("Edit current note"));
        connect(editAct, &QAction::triggered,
                this, &VWebView::handleEditAction);
        menu->insertAction(actions.isEmpty() ? NULL : actions[0], editAct);
        // actions does not contain editAction.
        if (!actions.isEmpty()) {
            menu->insertSeparator(actions[0]);
        }
    }

    menu->exec(p_event->globalPos());
    delete menu;
}

void VWebView::handleEditAction()
{
    emit editNote();
}
