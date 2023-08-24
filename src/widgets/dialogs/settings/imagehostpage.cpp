#include "imagehostpage.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>

#include <widgets/widgetsfactory.h>
#include <core/editorconfig.h>
#include <core/configmgr.h>
#include <utils/widgetutils.h>
#include <imagehost/imagehostmgr.h>
#include <widgets/messageboxhelper.h>

#include "editorpage.h"
#include "newimagehostdialog.h"

using namespace vnotex;

ImageHostPage::ImageHostPage(QWidget *p_parent)
    : SettingsPage(p_parent)
{
    setupUI();
}

void ImageHostPage::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);

    // New Image Host.
    {
        auto layout = new QHBoxLayout();
        m_mainLayout->addLayout(layout);

        auto newBtn = new QPushButton(tr("New Image Host"), this);
        connect(newBtn, &QPushButton::clicked,
                this, &ImageHostPage::newImageHost);
        layout->addWidget(newBtn);
        layout->addStretch();
    }

    auto box = setupGeneralBox(this);
    m_mainLayout->addWidget(box);

    m_mainLayout->addStretch();
}

QGroupBox *ImageHostPage::setupGeneralBox(QWidget *p_parent)
{
    auto box = new QGroupBox(tr("General"), p_parent);
    auto layout = WidgetsFactory::createFormLayout(box);

    {
        // Add items in loadInternal().
        m_defaultImageHostComboBox = WidgetsFactory::createComboBox(box);

        const QString label(tr("Default image host:"));
        layout->addRow(label, m_defaultImageHostComboBox);
        addSearchItem(label, m_defaultImageHostComboBox);
        connect(m_defaultImageHostComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &ImageHostPage::pageIsChanged);
    }

    {
        const QString label(tr("Clear obsolete images"));
        m_clearObsoleteImageCheckBox = WidgetsFactory::createCheckBox(label, box);
        m_clearObsoleteImageCheckBox->setToolTip(tr("Clear unused images at image host (based on current file only)"));
        layout->addRow(m_clearObsoleteImageCheckBox);
        addSearchItem(label, m_clearObsoleteImageCheckBox->toolTip(), m_clearObsoleteImageCheckBox);
        connect(m_clearObsoleteImageCheckBox, &QCheckBox::stateChanged,
                this, &ImageHostPage::pageIsChanged);
    }

    return box;
}

void ImageHostPage::addWidgetToLayout(QWidget *p_widget)
{
    m_mainLayout->addWidget(p_widget);
}

void ImageHostPage::loadInternal()
{
    const auto &hosts = ImageHostMgr::getInst().getImageHosts();
    const auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    {
        m_defaultImageHostComboBox->clear();

        m_defaultImageHostComboBox->addItem(tr("Local"));
        for (const auto &host : hosts) {
            m_defaultImageHostComboBox->addItem(host->getName(), host->getName());
        }

        auto defaultHost = ImageHostMgr::getInst().getDefaultImageHost();
        if (defaultHost) {
            int idx = m_defaultImageHostComboBox->findData(defaultHost->getName());
            Q_ASSERT(idx > 0);
            m_defaultImageHostComboBox->setCurrentIndex(idx);
        } else {
            m_defaultImageHostComboBox->setCurrentIndex(0);
        }
    }

    m_clearObsoleteImageCheckBox->setChecked(editorConfig.isClearObsoleteImageAtImageHostEnabled());

    // Clear all the boxes before.
    {
        auto boxes = findChildren<QGroupBox *>(QString(), Qt::FindDirectChildrenOnly);
        for (auto box : boxes) {
            if (box->objectName().isEmpty()) {
                continue;
            }

            m_mainLayout->removeWidget(box);
            box->deleteLater();
        }
    }

    removeLastStretch();

    // Setup boxes.
    for (const auto &host : hosts) {
        auto box = setupGroupBoxForImageHost(host, this);
        addWidgetToLayout(box);
    }

    m_mainLayout->addStretch();
}

bool ImageHostPage::saveInternal()
{
    auto &hostMgr = ImageHostMgr::getInst();
    auto &editorConfig = ConfigMgr::getInst().getEditorConfig();

    Q_ASSERT(m_hostToFields.size() == hostMgr.getImageHosts().size());

    bool hasError = false;

    hostMgr.setDefaultImageHost(m_defaultImageHostComboBox->currentData().toString());

    editorConfig.setClearObsoleteImageAtImageHostEnabled(m_clearObsoleteImageCheckBox->isChecked());

    for (auto it = m_hostToFields.constBegin(); it != m_hostToFields.constEnd(); ++it) {
        auto host = it.key();
        const auto &fields = it.value();
        Q_ASSERT(!fields.isEmpty());

        // Name.
        {
            auto box = dynamic_cast<QGroupBox *>(fields[0]->parent());
            Q_ASSERT(box);
            auto nameLineEdit = box->findChild<QLineEdit *>(QStringLiteral("_name"), Qt::FindDirectChildrenOnly);
            Q_ASSERT(nameLineEdit);
            const auto &newName = nameLineEdit->text();
            if (newName != host->getName()) {
                if (!hostMgr.renameImageHost(host, newName)) {
                    setError(tr("Failed to rename image host (%1) to (%2).").arg(host->getName(), newName));
                    hasError = true;
                    break;
                }

                box->setObjectName(newName);
            }
        }

        // Configs.
        const auto configObj = fieldsToConfig(fields);
        host->setConfig(configObj);
    }

    hostMgr.saveImageHosts();

    // No need to notify editor since ImageHostMgr will signal out.
    // EditorPage::notifyEditorConfigChange();
    return !hasError;
}

QString ImageHostPage::title() const
{
    return tr("Image Host");
}

void ImageHostPage::newImageHost()
{
    NewImageHostDialog dialog(this);
    if (dialog.exec()) {
        removeLastStretch();

        auto box = setupGroupBoxForImageHost(dialog.getNewImageHost(), this);
        addWidgetToLayout(box);

        m_mainLayout->addStretch();
    }
}

QGroupBox *ImageHostPage::setupGroupBoxForImageHost(ImageHost *p_host, QWidget *p_parent)
{
    auto box = new QGroupBox(p_parent);
    box->setObjectName(p_host->getName());
    auto layout = WidgetsFactory::createFormLayout(box);

    // Add Test and Delete button.
    {
        auto btnLayout = new QHBoxLayout();
        btnLayout->addStretch();

        layout->addRow(btnLayout);

        auto testBtn = new QPushButton(tr("Test"), box);
        btnLayout->addWidget(testBtn);
        connect(testBtn, &QPushButton::clicked,
                this, [this, box]() {
                    const auto name = box->objectName();
                    testImageHost(name);
                });

        auto deleteBtn = new QPushButton(tr("Delete"), box);
        btnLayout->addWidget(deleteBtn);
        connect(deleteBtn, &QPushButton::clicked,
                this, [this, box]() {
                    const auto name = box->objectName();
                    removeImageHost(name);
                });
    }

    layout->addRow(tr("Type:"), new QLabel(ImageHost::typeString(p_host->getType()), box));

    auto nameLineEdit = WidgetsFactory::createLineEdit(p_host->getName(), box);
    nameLineEdit->setObjectName(QStringLiteral("_name"));
    layout->addRow(tr("Name:"), nameLineEdit);
    m_hostToFields[p_host].append(nameLineEdit);
    connect(nameLineEdit, &QLineEdit::textChanged,
            this, &ImageHostPage::pageIsChanged);

    const auto configObj = p_host->getConfig();
    const auto keys = configObj.keys();
    for (const auto &key : keys) {
        Q_ASSERT(key != "_name");
        auto configLineEdit = WidgetsFactory::createLineEdit(configObj[key].toString(), box);
        configLineEdit->setObjectName(key);
        layout->addRow(tr("%1:").arg(key), configLineEdit);
        m_hostToFields[p_host].append(configLineEdit);
        connect(configLineEdit, &QLineEdit::textChanged,
                this, &ImageHostPage::pageIsChanged);
    }

    return box;
}

void ImageHostPage::removeImageHost(const QString &p_hostName)
{
    int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Type::Question,
                                                 tr("Delete image host (%1)?").arg(p_hostName));
    if (ret != QMessageBox::Ok) {
        return;
    }

    auto &hostMgr = ImageHostMgr::getInst();
    auto host = hostMgr.find(p_hostName);
    Q_ASSERT(host);
    hostMgr.removeImageHost(host);

    // Remove the group box and related fields.
    m_hostToFields.remove(host);

    auto box = findChild<QGroupBox *>(p_hostName, Qt::FindDirectChildrenOnly);
    Q_ASSERT(box);
    m_mainLayout->removeWidget(box);
    box->deleteLater();
}

QJsonObject ImageHostPage::fieldsToConfig(const QVector<QLineEdit *> &p_fields) const
{
    QJsonObject configObj;
    for (auto field : p_fields) {
        configObj[field->objectName()] = field->text();
    }

    return configObj;
}

void ImageHostPage::testImageHost(const QString &p_hostName)
{
    auto &hostMgr = ImageHostMgr::getInst();
    auto host = hostMgr.find(p_hostName);
    Q_ASSERT(host);

    auto it = m_hostToFields.find(host);
    Q_ASSERT(it != m_hostToFields.end());

    const auto configObj = fieldsToConfig(it.value());
    QString msg;
    bool ret = host->testConfig(configObj, msg);
    MessageBoxHelper::notify(ret ? MessageBoxHelper::Information : MessageBoxHelper::Warning,
                             tr("Test %1.").arg(ret ? tr("succeeded") : tr("failed")),
                             QString(),
                             msg);
}

void ImageHostPage::removeLastStretch()
{
    auto item = m_mainLayout->itemAt(m_mainLayout->count() - 1);
    if (item) {
        m_mainLayout->removeItem(item);
        delete item;
    }
}
