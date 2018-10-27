#include "vkeyboardlayoutmappingdialog.h"

#include <QtWidgets>

#include "vlineedit.h"
#include "utils/vkeyboardlayoutmanager.h"
#include "utils/vutils.h"
#include "utils/viconutils.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

VKeyboardLayoutMappingDialog::VKeyboardLayoutMappingDialog(QWidget *p_parent)
    : QDialog(p_parent),
      m_mappingModified(false),
      m_listenIndex(-1)
{
    setupUI();

    loadAvailableMappings();
}

void VKeyboardLayoutMappingDialog::setupUI()
{
    QString info = tr("Manage keybaord layout mappings to used in shortcuts.");
    info += "\n";
    info += tr("Double click an item to set mapping key.");
    QLabel *infoLabel = new QLabel(info, this);

    // Selector.
    m_selectorCombo = VUtils::getComboBox(this);
    connect(m_selectorCombo, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int p_idx) {
                loadMappingInfo(m_selectorCombo->itemData(p_idx).toString());
            });

    // Add.
    m_addBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/add.svg"), "", this);
    m_addBtn->setToolTip(tr("New Mapping"));
    m_addBtn->setProperty("FlatBtn", true);
    connect(m_addBtn, &QPushButton::clicked,
            this, &VKeyboardLayoutMappingDialog::newMapping);

    // Delete.
    m_deleteBtn = new QPushButton(VIconUtils::buttonDangerIcon(":/resources/icons/delete.svg"),
                                  "",
                                  this);
    m_deleteBtn->setToolTip(tr("Delete Mapping"));
    m_deleteBtn->setProperty("FlatBtn", true);
    connect(m_deleteBtn, &QPushButton::clicked,
            this, &VKeyboardLayoutMappingDialog::deleteCurrentMapping);

    QHBoxLayout *selectLayout = new QHBoxLayout();
    selectLayout->addWidget(new QLabel(tr("Keyboard layout mapping:"), this));
    selectLayout->addWidget(m_selectorCombo);
    selectLayout->addWidget(m_addBtn);
    selectLayout->addWidget(m_deleteBtn);
    selectLayout->addStretch();

    // Name.
    m_nameEdit = new VLineEdit(this);
    connect(m_nameEdit, &QLineEdit::textEdited,
            this, [this](const QString &p_text) {
                Q_UNUSED(p_text);
                setModified(true);
            });

    QHBoxLayout *editLayout = new QHBoxLayout();
    editLayout->addWidget(new QLabel(tr("Name:"), this));
    editLayout->addWidget(m_nameEdit);
    editLayout->addStretch();

    // Tree.
    m_contentTree = new QTreeWidget(this);
    m_contentTree->setProperty("ItemBorder", true);
    m_contentTree->setRootIsDecorated(false);
    m_contentTree->setColumnCount(2);
    m_contentTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    QStringList headers;
    headers << tr("Key") << tr("New Key");
    m_contentTree->setHeaderLabels(headers);

    m_contentTree->installEventFilter(this);

    connect(m_contentTree, &QTreeWidget::itemDoubleClicked,
            this, [this](QTreeWidgetItem *p_item, int p_column) {
                Q_UNUSED(p_column);
                int idx = m_contentTree->indexOfTopLevelItem(p_item);
                if (m_listenIndex == -1) {
                    // Listen key for this item.
                    setListeningKey(idx);
                } else if (idx == m_listenIndex) {
                    // Cancel listening key for this item.
                    cancelListeningKey();
                } else {
                    // Recover previous item.
                    cancelListeningKey();
                    setListeningKey(idx);
                }
            });

    connect(m_contentTree, &QTreeWidget::itemClicked,
            this, [this](QTreeWidgetItem *p_item, int p_column) {
                Q_UNUSED(p_column);
                int idx = m_contentTree->indexOfTopLevelItem(p_item);
                if (idx != m_listenIndex) {
                    cancelListeningKey();
                }
            });

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->addLayout(editLayout);
    infoLayout->addWidget(m_contentTree);

    QGroupBox *box = new QGroupBox(tr("Mapping Information"));
    box->setLayout(infoLayout);

    // Ok is the default button.
    QDialogButtonBox *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                                    | QDialogButtonBox::Apply
                                                    | QDialogButtonBox::Cancel);
    connect(btnBox, &QDialogButtonBox::accepted,
            this, [this]() {
                if (applyChanges()) {
                    QDialog::accept();
                }
            });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QPushButton *okBtn = btnBox->button(QDialogButtonBox::Ok);
    okBtn->setProperty("SpecialBtn", true);

    m_applyBtn = btnBox->button(QDialogButtonBox::Apply);
    connect(m_applyBtn, &QPushButton::clicked,
            this, &VKeyboardLayoutMappingDialog::applyChanges);

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addWidget(infoLabel);
    mainLayout->addLayout(selectLayout);
    mainLayout->addWidget(box);
    mainLayout->addWidget(btnBox);

    setLayout(mainLayout);

    setWindowTitle(tr("Keyboard Layout Mappings"));
}

void VKeyboardLayoutMappingDialog::newMapping()
{
    QString name = getNewMappingName();
    if (!VKeyboardLayoutManager::addLayout(name)) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Fail to add mapping <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle)
                              .arg(name),
                            tr("Please check the configuration file and try again."),
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
        return;
    }

    loadAvailableMappings();

    setCurrentMapping(name);
}

QString VKeyboardLayoutMappingDialog::getNewMappingName() const
{
    QString name;
    QString baseName("layout_mapping");
    int seq = 1;
    do {
        name = QString("%1_%2").arg(baseName).arg(QString::number(seq++), 3, '0');
    } while (m_selectorCombo->findData(name) != -1);

    return name;
}

void VKeyboardLayoutMappingDialog::deleteCurrentMapping()
{
    QString mapping = currentMapping();
    if (mapping.isEmpty()) {
        return;
    }

    int ret = VUtils::showMessage(QMessageBox::Warning,
                                  tr("Warning"),
                                  tr("Are you sure to delete mapping <span style=\"%1\">%2</span>?")
                                    .arg(g_config->c_dataTextStyle)
                                    .arg(mapping),
                                  "",
                                  QMessageBox::Ok | QMessageBox::Cancel,
                                  QMessageBox::Ok,
                                  this);
    if (ret != QMessageBox::Ok) {
        return;
    }

    if (!VKeyboardLayoutManager::removeLayout(mapping)) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Fail to delete mapping <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle)
                              .arg(mapping),
                            tr("Please check the configuration file and try again."),
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
    }

    loadAvailableMappings();
}

void VKeyboardLayoutMappingDialog::loadAvailableMappings()
{
    m_selectorCombo->setCurrentIndex(-1);
    m_selectorCombo->clear();

    QStringList layouts = VKeyboardLayoutManager::availableLayouts();
    for (auto const & layout : layouts) {
        m_selectorCombo->addItem(layout, layout);
    }

    if (m_selectorCombo->count() > 0) {
        m_selectorCombo->setCurrentIndex(0);
    }
}

static QList<int> keysNeededToMap()
{
    QList<int> keys;

    for (int i = Qt::Key_0; i <= Qt::Key_9; ++i) {
        keys.append(i);
    }

    for (int i = Qt::Key_A; i <= Qt::Key_Z; ++i) {
        keys.append(i);
    }

    QList<int> addi = g_config->getKeyboardLayoutMappingKeys();
    for (auto tmp : addi) {
        if (!keys.contains(tmp)) {
            keys.append(tmp);
        }
    }

    return keys;
}

static void recoverTreeItem(QTreeWidgetItem *p_item)
{
    int key = p_item->data(0, Qt::UserRole).toInt();
    QString text0 = QString("%1 (%2)").arg(VUtils::keyToChar(key, false))
                                      .arg(key);
    p_item->setText(0, text0);

    int newKey = p_item->data(1, Qt::UserRole).toInt();
    QString text1;
    if (newKey > 0) {
        text1 = QString("%1 (%2)").arg(VUtils::keyToChar(newKey, false))
                                  .arg(newKey);
    }

    p_item->setText(1, text1);
}

// @p_newKey, 0 if there is no mapping.
static void fillTreeItem(QTreeWidgetItem *p_item, int p_key, int p_newKey)
{
    p_item->setData(0, Qt::UserRole, p_key);
    p_item->setData(1, Qt::UserRole, p_newKey);
    recoverTreeItem(p_item);
}

static void setTreeItemMapping(QTreeWidgetItem *p_item, int p_newKey)
{
    p_item->setData(1, Qt::UserRole, p_newKey);
}

static void fillMappingTree(QTreeWidget *p_tree, const QHash<int, int> &p_mappings)
{
    QList<int> keys = keysNeededToMap();

    for (auto key : keys) {
        int val = 0;
        auto it = p_mappings.find(key);
        if (it != p_mappings.end()) {
            val = it.value();
        }

        QTreeWidgetItem *item = new QTreeWidgetItem(p_tree);
        fillTreeItem(item, key, val);
    }
}

static QHash<int, int> retrieveMappingFromTree(QTreeWidget *p_tree)
{
    QHash<int, int> mappings;
    int cnt = p_tree->topLevelItemCount();
    for (int i = 0; i < cnt; ++i) {
        QTreeWidgetItem *item = p_tree->topLevelItem(i);
        int key = item->data(0, Qt::UserRole).toInt();
        int newKey = item->data(1, Qt::UserRole).toInt();
        if (newKey > 0) {
            mappings.insert(key, newKey);
        }
    }

    return mappings;
}

void VKeyboardLayoutMappingDialog::loadMappingInfo(const QString &p_layout)
{
    setModified(false);

    if (p_layout.isEmpty()) {
        m_nameEdit->clear();
        m_contentTree->clear();
        m_nameEdit->setEnabled(false);
        m_contentTree->setEnabled(false);
        return;
    }

    m_nameEdit->setText(p_layout);
    m_nameEdit->setEnabled(true);

    m_contentTree->clear();
    if (!p_layout.isEmpty()) {
        auto mappings = VKeyboardLayoutManager::readLayoutMapping(p_layout);
        fillMappingTree(m_contentTree, mappings);
    }
    m_contentTree->setEnabled(true);
}

void VKeyboardLayoutMappingDialog::updateButtons()
{
    QString mapping = currentMapping();

    m_deleteBtn->setEnabled(!mapping.isEmpty());
    m_applyBtn->setEnabled(m_mappingModified);
}

QString VKeyboardLayoutMappingDialog::currentMapping() const
{
    return m_selectorCombo->currentData().toString();
}

void VKeyboardLayoutMappingDialog::setCurrentMapping(const QString &p_layout)
{
    return m_selectorCombo->setCurrentIndex(m_selectorCombo->findData(p_layout));
}

bool VKeyboardLayoutMappingDialog::applyChanges()
{
    if (!m_mappingModified) {
        return true;
    }

    QString mapping = currentMapping();
    if (mapping.isEmpty()) {
        setModified(false);
        return true;
    }

    // Check the name.
    QString newName = m_nameEdit->text();
    if (newName.isEmpty() || newName.toLower() == "global") {
        // Set back the original name.
        m_nameEdit->setText(mapping);
        m_nameEdit->selectAll();
        m_nameEdit->setFocus();
        return false;
    } else if (newName != mapping) {
        // Rename the mapping.
        if (!VKeyboardLayoutManager::renameLayout(mapping, newName)) {
            VUtils::showMessage(QMessageBox::Warning,
                                tr("Warning"),
                                tr("Fail to rename mapping <span style=\"%1\">%2</span>.")
                                  .arg(g_config->c_dataTextStyle)
                                  .arg(mapping),
                                tr("Please check the configuration file and try again."),
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);
            m_nameEdit->setText(mapping);
            m_nameEdit->selectAll();
            m_nameEdit->setFocus();
            return false;
        }

        // Update the combobox.
        int idx = m_selectorCombo->currentIndex();
        m_selectorCombo->setItemText(idx, newName);
        m_selectorCombo->setItemData(idx, newName);

        mapping = newName;
    }

    // Check the mappings.
    QHash<int, int> mappings = retrieveMappingFromTree(m_contentTree);
    if (!VKeyboardLayoutManager::updateLayout(mapping, mappings)) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Fail to update mapping <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle)
                              .arg(mapping),
                            tr("Please check the configuration file and try again."),
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
        return false;
    }

    setModified(false);
    return true;
}

bool VKeyboardLayoutMappingDialog::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if (p_obj == m_contentTree) {
        switch (p_event->type()) {
        case QEvent::FocusOut:
            cancelListeningKey();
            break;

        case QEvent::KeyPress:
            if (listenKey(static_cast<QKeyEvent *>(p_event))) {
                return true;
            }

            break;

        default:
            break;
        }
    }

    return QDialog::eventFilter(p_obj, p_event);
}

bool VKeyboardLayoutMappingDialog::listenKey(QKeyEvent *p_event)
{
    if (m_listenIndex == -1) {
        return false;
    }

    int key = p_event->key();

    if (VUtils::isMetaKey(key)) {
        return false;
    }

    if (key == Qt::Key_Escape) {
        cancelListeningKey();
        return true;
    }

    // Set the mapping.
    QTreeWidgetItem *item = m_contentTree->topLevelItem(m_listenIndex);
    setTreeItemMapping(item, key);
    setModified(true);

    // Try next item automatically.
    int nextIdx = m_listenIndex + 1;
    cancelListeningKey();

    if (nextIdx < m_contentTree->topLevelItemCount()) {
        QTreeWidgetItem *item = m_contentTree->topLevelItem(nextIdx);
        m_contentTree->clearSelection();
        m_contentTree->setCurrentItem(item);

        setListeningKey(nextIdx);
    }

    return true;
}

void VKeyboardLayoutMappingDialog::cancelListeningKey()
{
    if (m_listenIndex > -1) {
        // Recover that item.
        recoverTreeItem(m_contentTree->topLevelItem(m_listenIndex));

        m_listenIndex = -1;
    }
}

void VKeyboardLayoutMappingDialog::setListeningKey(int p_idx)
{
    Q_ASSERT(m_listenIndex == -1 && p_idx > -1);
    m_listenIndex = p_idx;
    QTreeWidgetItem *item = m_contentTree->topLevelItem(m_listenIndex);
    item->setText(1, tr("Press key to set mapping"));
}
