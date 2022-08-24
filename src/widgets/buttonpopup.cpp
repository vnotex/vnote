#include "buttonpopup.h"

#include <QVBoxLayout>

#include <utils/widgetutils.h>

using namespace vnotex;

ButtonPopup::ButtonPopup(QToolButton *p_btn, QWidget *p_parent)
    : QMenu(p_parent),
      m_button(p_btn)
{
    setupUI();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // Qt::Popup on macOS does not work well with input method.
    setWindowFlags(Qt::Tool | Qt::NoDropShadowWindowHint);
    setWindowModality(Qt::ApplicationModal);
#endif
}

void ButtonPopup::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    WidgetUtils::setContentsMargins(mainLayout);
}

void ButtonPopup::setCentralWidget(QWidget *p_widget)
{
    Q_ASSERT(p_widget);
    auto mainLayout = layout();
    Q_ASSERT(mainLayout->count() == 0);
    mainLayout->addWidget(p_widget);
}
