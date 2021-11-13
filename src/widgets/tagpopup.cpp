#include "tagpopup.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include <utils/widgetutils.h>
#include <buffer/buffer.h>

#include "tagviewer.h"

using namespace vnotex;

TagPopup::TagPopup(QToolButton *p_btn, QWidget *p_parent)
    : QMenu(p_parent),
      m_button(p_btn)
{
    setupUI();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // Qt::Popup on macOS does not work well with input method.
    setWindowFlags(Qt::Tool | Qt::NoDropShadowWindowHint);
    setWindowModality(Qt::ApplicationModal);
#endif

    connect(this, &QMenu::aboutToShow,
            this, [this]() {
                m_tagViewer->setNode(m_buffer ? m_buffer->getNode() : nullptr);
                // Enable input method.
                m_tagViewer->activateWindow();
                m_tagViewer->setFocus();
            });

    connect(this, &QMenu::aboutToHide,
            m_tagViewer, &TagViewer::save);
}

void TagPopup::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    WidgetUtils::setContentsMargins(mainLayout);

    m_tagViewer = new TagViewer(true, this);
    mainLayout->addWidget(m_tagViewer);

    setMinimumSize(256, 320);
}

void TagPopup::setBuffer(Buffer *p_buffer)
{
    if (m_buffer == p_buffer) {
        return;
    }

    m_buffer = p_buffer;
}
