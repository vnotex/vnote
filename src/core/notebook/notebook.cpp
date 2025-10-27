#include "notebook.h"

#include <QFileInfo>

#include "nodeparameters.h"
#include <core/exception.h>
#include <notebookbackend/inotebookbackend.h>
#include <notebookconfigmgr/inotebookconfigmgr.h>
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <versioncontroller/iversioncontroller.h>

using namespace vnotex;

const QString Notebook::c_defaultAttachmentFolder = QStringLiteral("vx_attachments");

const QString Notebook::c_defaultImageFolder = QStringLiteral("vx_images");

const QString Notebook::c_defaultRecycleBinFolder = QStringLiteral("vx_recycle_bin");

static vnotex::ID generateNotebookID() {
  static vnotex::ID id = Notebook::InvalidId;
  return ++id;
}

Notebook::Notebook(const NotebookParameters &p_paras, QObject *p_parent)
    : QObject(p_parent), m_id(generateNotebookID()), m_type(p_paras.m_type), m_name(p_paras.m_name),
      m_description(p_paras.m_description), m_rootFolderPath(p_paras.m_rootFolderPath),
      m_icon(p_paras.m_icon), m_imageFolder(p_paras.m_imageFolder),
      m_attachmentFolder(p_paras.m_attachmentFolder), m_createdTimeUtc(p_paras.m_createdTimeUtc),
      m_backend(p_paras.m_notebookBackend), m_versionController(p_paras.m_versionController),
      m_configMgr(p_paras.m_notebookConfigMgr) {
  if (m_imageFolder.isEmpty()) {
    m_imageFolder = c_defaultImageFolder;
  }
  if (m_attachmentFolder.isEmpty()) {
    m_attachmentFolder = c_defaultAttachmentFolder;
  }
  if (m_recycleBinFolder.isEmpty()) {
    m_recycleBinFolder = c_defaultRecycleBinFolder;
  }

  m_configMgr->setNotebook(this);
}

Notebook::Notebook(const QString &p_name, QObject *p_parent) : QObject(p_parent), m_name(p_name) {}

Notebook::~Notebook() {}

void Notebook::initialize() {
  if (m_initialized) {
    return;
  }

  m_initialized = true;
  initializeInternal();
}

vnotex::ID Notebook::getId() const { return m_id; }

const QString &Notebook::getType() const { return m_type; }

const QString &Notebook::getName() const { return m_name; }

void Notebook::setName(const QString &p_name) { m_name = p_name; }

void Notebook::updateName(const QString &p_name) {
  Q_ASSERT(!p_name.isEmpty());
  if (p_name == m_name) {
    return;
  }

  m_name = p_name;
  updateNotebookConfig();
  emit updated();
}

const QString &Notebook::getDescription() const { return m_description; }

void Notebook::setDescription(const QString &p_description) { m_description = p_description; }

void Notebook::updateDescription(const QString &p_description) {
  if (p_description == m_description) {
    return;
  }

  m_description = p_description;
  updateNotebookConfig();
  emit updated();
}

const QString &Notebook::getRootFolderPath() const { return m_rootFolderPath; }

QString Notebook::getRootFolderAbsolutePath() const {
  return PathUtils::absolutePath(m_rootFolderPath);
}

QString Notebook::getConfigFolderAbsolutePath() const {
  const auto &folderPath = m_configMgr->getConfigFolderPath();
  if (folderPath.isEmpty()) {
    return QString();
  }

  return getBackend()->getFullPath(folderPath);
}

const QIcon &Notebook::getIcon() const { return m_icon; }

void Notebook::setIcon(const QIcon &p_icon) { m_icon = p_icon; }

const QString &Notebook::getImageFolder() const { return m_imageFolder; }

const QString &Notebook::getAttachmentFolder() const { return m_attachmentFolder; }

const QString &Notebook::getRecycleBinFolder() const { return m_recycleBinFolder; }

QString Notebook::getRecycleBinFolderAbsolutePath() const {
  if (QDir::isAbsolutePath(m_recycleBinFolder)) {
    if (!QFileInfo::exists(m_recycleBinFolder)) {
      QDir dir(m_recycleBinFolder);
      dir.mkpath(m_recycleBinFolder);
    }
    return m_recycleBinFolder;
  } else {
    auto folderPath = getBackend()->getFullPath(m_recycleBinFolder);
    if (!getBackend()->exists(m_recycleBinFolder)) {
      getBackend()->makePath(m_recycleBinFolder);
    }
    return folderPath;
  }
}

const QSharedPointer<INotebookBackend> &Notebook::getBackend() const { return m_backend; }

const QSharedPointer<IVersionController> &Notebook::getVersionController() const {
  return m_versionController;
}

const QSharedPointer<INotebookConfigMgr> &Notebook::getConfigMgr() const { return m_configMgr; }

const QSharedPointer<Node> &Notebook::getRootNode() const {
  if (!m_root) {
    const_cast<Notebook *>(this)->m_root = m_configMgr->loadRootNode();
    Q_ASSERT(m_root->isRoot());
  }

  return m_root;
}

QSharedPointer<Node> Notebook::newNode(Node *p_parent, Node::Flags p_flags, const QString &p_name,
                                       const QString &p_content) {
  return m_configMgr->newNode(p_parent, p_flags, p_name, p_content);
}

const QDateTime &Notebook::getCreatedTimeUtc() const { return m_createdTimeUtc; }

QSharedPointer<Node> Notebook::loadNodeByPath(const QString &p_path) {
  if (!PathUtils::pathContains(m_rootFolderPath, p_path)) {
    return nullptr;
  }

  QString relativePath;
  QFileInfo fi(p_path);
  if (fi.isAbsolute()) {
    if (!fi.exists()) {
      return nullptr;
    }

    relativePath = PathUtils::relativePath(m_rootFolderPath, p_path);
  } else {
    relativePath = p_path;
  }

  return m_configMgr->loadNodeByPath(getRootNode(), relativePath);
}

QSharedPointer<Node> Notebook::copyNodeAsChildOf(const QSharedPointer<Node> &p_src, Node *p_dest,
                                                 bool p_move) {
  Q_ASSERT(p_src != p_dest);
  Q_ASSERT(p_dest->getNotebook() == this);

  if (Node::isAncestor(p_src.data(), p_dest)) {
    Exception::throwOne(Exception::Type::InvalidArgument,
                        QStringLiteral("source (%1) is the ancestor of destination (%2)")
                            .arg(p_src->fetchPath(), p_dest->fetchPath()));
    return nullptr;
  }

  if (p_src->getParent() == p_dest && p_move) {
    return p_src;
  }

  return m_configMgr->copyNodeAsChildOf(p_src, p_dest, p_move);
}

void Notebook::removeNode(const QSharedPointer<Node> &p_node, bool p_force, bool p_configOnly) {
  Q_ASSERT(p_node && !p_node->isRoot());
  Q_ASSERT(p_node->getNotebook() == this);
  m_configMgr->removeNode(p_node, p_force, p_configOnly);
}

void Notebook::removeNode(Node *p_node, bool p_force, bool p_configOnly) {
  Q_ASSERT(p_node);
  removeNode(p_node->sharedFromThis(), p_force, p_configOnly);
}

void Notebook::moveNodeToRecycleBin(Node *p_node) {
  moveNodeToRecycleBin(p_node->sharedFromThis());
}

void Notebook::moveNodeToRecycleBin(const QSharedPointer<Node> &p_node) {
  Q_ASSERT(p_node && !p_node->isRoot());
  m_configMgr->removeNodeToFolder(p_node, getOrCreateRecycleBinDateFolder());
}

QString Notebook::getOrCreateRecycleBinDateFolder() {
  // Name after date.
  auto dateFolderName = QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));
  auto folderPath = PathUtils::concatenateFilePath(getRecycleBinFolder(), dateFolderName);
  if (QDir::isAbsolutePath(folderPath)) {
    qDebug() << "using absolute recycle bin folder" << folderPath;
    QDir dir(folderPath);
    if (dir.exists()) {
      dir.mkpath(folderPath);
    }
  } else {
    if (!getBackend()->exists(folderPath)) {
      getBackend()->makePath(folderPath);
    }
  }

  return folderPath;
}

void Notebook::moveFileToRecycleBin(const QString &p_filePath) {
  auto destFilePath = PathUtils::concatenateFilePath(getOrCreateRecycleBinDateFolder(),
                                                     PathUtils::fileName(p_filePath));
  destFilePath = getBackend()->renameIfExistsCaseInsensitive(destFilePath);
  m_backend->copyFile(p_filePath, destFilePath, true);
}

void Notebook::moveDirToRecycleBin(const QString &p_dirPath) {
  auto destDirPath = PathUtils::concatenateFilePath(getOrCreateRecycleBinDateFolder(),
                                                    PathUtils::fileName(p_dirPath));
  destDirPath = getBackend()->renameIfExistsCaseInsensitive(destDirPath);
  m_backend->copyDir(p_dirPath, destDirPath, true);
}

QSharedPointer<Node> Notebook::addAsNode(Node *p_parent, Node::Flags p_flags, const QString &p_name,
                                         const NodeParameters &p_paras) {
  return m_configMgr->addAsNode(p_parent, p_flags, p_name, p_paras);
}

bool Notebook::isBuiltInFile(const Node *p_node, const QString &p_name) const {
  return m_configMgr->isBuiltInFile(p_node, p_name);
}

bool Notebook::isBuiltInFolder(const Node *p_node, const QString &p_name) const {
  return m_configMgr->isBuiltInFolder(p_node, p_name);
}

QSharedPointer<Node> Notebook::copyAsNode(Node *p_parent, Node::Flags p_flags,
                                          const QString &p_path) {
  return m_configMgr->copyAsNode(p_parent, p_flags, p_path);
}

void Notebook::reloadNodes() {
  m_root.clear();
  getRootNode();
}

QJsonObject Notebook::getExtraConfig(const QString &p_key) const {
  const auto &configs = getExtraConfigs();
  return configs.value(p_key).toObject();
}

QList<QSharedPointer<File>> Notebook::collectFiles() {
  QList<QSharedPointer<File>> files;

  auto rootNode = getRootNode();

  const auto &children = rootNode->getChildrenRef();
  for (const auto &child : children) {
    if (child->getUse() != Node::Use::Normal) {
      continue;
    }
    files.append(child->collectFiles());
  }

  return files;
}

QStringList Notebook::scanAndImportExternalFiles() {
  return m_configMgr->scanAndImportExternalFiles(getRootNode().data());
}

bool Notebook::rebuildDatabase() { return false; }

HistoryI *Notebook::history() { return nullptr; }

TagI *Notebook::tag() { return nullptr; }

void Notebook::emptyRecycleBin() {
  QDir dir(getRecycleBinFolderAbsolutePath());
  auto children = dir.entryList(QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
  for (const auto &child : children) {
    FileUtils::removeDir(dir.filePath(child));
  }
}
