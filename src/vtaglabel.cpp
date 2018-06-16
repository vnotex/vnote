#include "vtaglabel.h"

#include <QtWidgets>

#include "utils/viconutils.h"

#define MAX_DISPLAY_SIZE_PER_LABEL 5

VTagLabel::VTagLabel(const QString &p_text,
                     bool p_useFullText,
                     QWidget *p_parent)
    : QWidget(p_parent),
      m_useFullText(p_useFullText),
      m_text(p_text)
{
    setupUI();
}

VTagLabel::VTagLabel(QWidget *p_parent)
    : QWidget(p_parent),
      m_useFullText(false)
{
    setupUI();
}

void VTagLabel::setupUI()
{
    m_label = new QLabel(this);
    m_label->setProperty("TagLabel", true);

    updateLabel();

    m_closeBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/close.svg"), "", this);
    m_closeBtn->setProperty("StatusBtn", true);
    m_closeBtn->setToolTip(tr("Remove"));
    m_closeBtn->setFocusPolicy(Qt::NoFocus);
    m_closeBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_closeBtn->hide();
    connect(m_closeBtn, &QPushButton::clicked,
            this, [this]() {
                emit removalRequested(m_text);
            });

    QHBoxLayout *layout = new QHBoxLayout();
    layout->addWidget(m_label);
    layout->addWidget(m_closeBtn);

    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    setLayout(layout);
}

void VTagLabel::updateLabel()
{
    QString tag(m_text);

    if (!m_useFullText) {
        if (tag.size() > MAX_DISPLAY_SIZE_PER_LABEL) {
            tag.resize(MAX_DISPLAY_SIZE_PER_LABEL);
            tag += QStringLiteral("...");
        }
    }

    m_label->setText(tag);
    m_label->setToolTip(m_text);
}

void VTagLabel::clear()
{
    m_label->clear();
}

void VTagLabel::setText(const QString &p_text)
{
    m_text = p_text;
    updateLabel();
}

bool VTagLabel::event(QEvent *p_event)
{
    switch (p_event->type()) {
    case QEvent::Enter:
        m_closeBtn->show();
        break;

    case QEvent::Leave:
        m_closeBtn->hide();
        break;

    default:
        break;
    }

    return QWidget::event(p_event);
}
