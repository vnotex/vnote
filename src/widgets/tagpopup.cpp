#include "tagpopup.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include <utils/widgetutils.h>
#include <buffer/buffer.h>

#include "tagviewer.h"

using namespace vnotex;

TagPopup::TagPopup(QToolButton *p_btn, QWidget *p_parent)
    : ButtonPopup(p_btn, p_parent)
{
    setupUI();

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
    m_tagViewer = new TagViewer(true, this);
    setCentralWidget(m_tagViewer);

    setMinimumSize(256, 320);
}

void TagPopup::setBuffer(Buffer *p_buffer)
{
    if (m_buffer == p_buffer) {
        return;
    }

    m_buffer = p_buffer;
}
