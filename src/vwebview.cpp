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

    QAction *editAction = new QAction(QIcon(":/resources/icons/edit_note.svg"),
                                     tr("&Edit"), this);
    editAction->setToolTip(tr("Edit current note"));
    connect(editAction, &QAction::triggered,
            this, &VWebView::handleEditAction);
    if (!m_file->isModifiable()) {
        editAction->setEnabled(false);
    }
    menu->insertAction(actions.isEmpty() ? NULL : actions[0], editAction);

    connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    menu->popup(p_event->globalPos());
}

void VWebView::handleEditAction()
{
    emit editNote();
}
