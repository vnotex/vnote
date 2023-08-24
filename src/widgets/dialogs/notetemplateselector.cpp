#include "notetemplateselector.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>

#include <core/templatemgr.h>
#include <core/exception.h>
#include <utils/fileutils.h>
#include <utils/widgetutils.h>

#include <widgets/widgetsfactory.h>

using namespace vnotex;

NoteTemplateSelector::NoteTemplateSelector(QWidget *p_parent)
    : QWidget(p_parent)
{
    setupUI();
}

void NoteTemplateSelector::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto selectorLayout = new QHBoxLayout();
    mainLayout->addLayout(selectorLayout);

    setupTemplateComboBox(this);
    selectorLayout->addWidget(m_templateComboBox, 1);

    auto manageBtn = new QPushButton(tr("Manage"), this);
    selectorLayout->addWidget(manageBtn);
    connect(manageBtn, &QPushButton::clicked,
            this, []() {
                WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(TemplateMgr::getInst().getTemplateFolder()));
            });

    m_templateTextEdit = WidgetsFactory::createPlainTextConsole(this);
    mainLayout->addWidget(m_templateTextEdit);
    m_templateTextEdit->hide();
}

void NoteTemplateSelector::setupTemplateComboBox(QWidget *p_parent)
{
    m_templateComboBox = WidgetsFactory::createComboBox(p_parent);

    // None.
    m_templateComboBox->addItem(tr("None"), "");

    int idx = 1;
    auto templates = TemplateMgr::getInst().getTemplates();
    for (const auto &temp : templates) {
        m_templateComboBox->addItem(temp, temp);
        m_templateComboBox->setItemData(idx++, temp, Qt::ToolTipRole);
    }

    m_templateComboBox->setCurrentIndex(0);

    connect(m_templateComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NoteTemplateSelector::updateCurrentTemplate);
}

void NoteTemplateSelector::updateCurrentTemplate()
{
    m_templateContent.clear();
    m_templateTextEdit->clear();

    auto templateName = m_templateComboBox->currentData().toString();
    if (templateName.isEmpty()) {
        m_templateTextEdit->hide();
        emit templateChanged();
        return;
    }

    const auto filePath = TemplateMgr::getInst().getTemplateFilePath(templateName);
    try {
        m_templateContent = FileUtils::readTextFile(filePath);
        m_templateTextEdit->setPlainText(m_templateContent);
    } catch (Exception &p_e) {
        QString msg = tr("Failed to load template (%1) (%2).")
            .arg(filePath, p_e.what());
        qCritical() << msg;
        m_templateTextEdit->setPlainText(msg);
    }
    m_templateTextEdit->show();
    emit templateChanged();
}

QString NoteTemplateSelector::getCurrentTemplate() const
{
    return m_templateComboBox->currentData().toString();
}

bool NoteTemplateSelector::setCurrentTemplate(const QString &p_template)
{
    int idx = m_templateComboBox->findData(p_template);
    if (idx != -1) {
        m_templateComboBox->setCurrentIndex(idx);
        return true;
    } else {
        return false;
    }
}

const QString& NoteTemplateSelector::getTemplateContent() const
{
    return m_templateContent;
}
