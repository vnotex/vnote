#include "outlinepopup.h"

#include <QVBoxLayout>
#include <QToolButton>

#include "outlineviewer.h"

using namespace vnotex;

OutlinePopup::OutlinePopup(QToolButton *p_btn, QWidget *p_parent)
    : QMenu(p_parent),
      m_button(p_btn)
{
    setupUI();

    connect(this, &QMenu::aboutToShow,
            this, [this]() {
                m_viewer->setFocus();
            });
}

void OutlinePopup::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);

    m_viewer = new OutlineViewer(tr("Outline"), this);
    mainLayout->addWidget(m_viewer);

    setMinimumSize(320, 384);
}

void OutlinePopup::setOutlineProvider(const QSharedPointer<OutlineProvider> &p_provider)
{
    m_viewer->setOutlineProvider(p_provider);
}

void OutlinePopup::showEvent(QShowEvent* p_event)
{
    QMenu::showEvent(p_event);

    const auto p = pos();
    const auto btnRect = m_button->geometry();
    // Move it to right-aligned.
    move(p.x() + btnRect.width() - geometry().width(), p.y());
}
