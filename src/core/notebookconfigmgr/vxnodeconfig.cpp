#include "vxnodeconfig.h"

#include <QJsonArray>
#include <utils/utils.h>

using namespace vnotex;

using namespace vnotex::vx_node_config;

const QString NodeConfig::c_version = "version";

const QString NodeConfig::c_id = "id";

const QString NodeConfig::c_signature = "signature";

const QString NodeConfig::c_createdTimeUtc = "created_time";

const QString NodeConfig::c_files = "files";

const QString NodeConfig::c_folders = "folders";

const QString NodeConfig::c_name = "name";

const QString NodeConfig::c_modifiedTimeUtc = "modified_time";

const QString NodeConfig::c_attachmentFolder = "attachment_folder";

const QString NodeConfig::c_tags = "tags";

const QString NodeConfig::c_backgroundColor = "background_color";

const QString NodeConfig::c_borderColor = "border_color";

const QString NodeConfig::c_nameColor = "name_color";

static ID stringToNodeId(const QString &p_idStr) {
  auto ret = stringToID(p_idStr);
  if (!ret.first) {
    return Node::InvalidId;
  }
  return ret.second;
}

QJsonObject NodeFileConfig::toJson() const {
  QJsonObject jobj;

  jobj[NodeConfig::c_name] = m_name;
  jobj[NodeConfig::c_id] = IDToString(m_id);
  jobj[NodeConfig::c_signature] = IDToString(m_signature);
  jobj[NodeConfig::c_createdTimeUtc] = Utils::dateTimeStringUniform(m_createdTimeUtc);
  jobj[NodeConfig::c_modifiedTimeUtc] = Utils::dateTimeStringUniform(m_modifiedTimeUtc);
  jobj[NodeConfig::c_attachmentFolder] = m_attachmentFolder;
  jobj[NodeConfig::c_tags] = QJsonArray::fromStringList(m_tags);

  // Visual settings
  if (!m_backgroundColor.isEmpty()) {
    jobj[NodeConfig::c_backgroundColor] = m_backgroundColor;
  }
  if (!m_borderColor.isEmpty()) {
    jobj[NodeConfig::c_borderColor] = m_borderColor;
  }
  if (!m_nameColor.isEmpty()) {
    jobj[NodeConfig::c_nameColor] = m_nameColor;
  }

  return jobj;
}

void NodeFileConfig::fromJson(const QJsonObject &p_jobj) {
  m_name = p_jobj[NodeConfig::c_name].toString();

  m_id = stringToNodeId(p_jobj[NodeConfig::c_id].toString());
  m_signature = stringToNodeId(p_jobj[NodeConfig::c_signature].toString());

  m_createdTimeUtc =
      Utils::dateTimeFromStringUniform(p_jobj[NodeConfig::c_createdTimeUtc].toString());
  m_modifiedTimeUtc =
      Utils::dateTimeFromStringUniform(p_jobj[NodeConfig::c_modifiedTimeUtc].toString());

  m_attachmentFolder = p_jobj[NodeConfig::c_attachmentFolder].toString();

  {
    auto arr = p_jobj[NodeConfig::c_tags].toArray();
    for (int i = 0; i < arr.size(); ++i) {
      m_tags << arr[i].toString();
    }
  }

  // Visual settings (check if fields exist for backward compatibility)
  if (p_jobj.contains(NodeConfig::c_backgroundColor)) {
    m_backgroundColor = p_jobj[NodeConfig::c_backgroundColor].toString();
  }
  if (p_jobj.contains(NodeConfig::c_borderColor)) {
    m_borderColor = p_jobj[NodeConfig::c_borderColor].toString();
  }
  if (p_jobj.contains(NodeConfig::c_nameColor)) {
    m_nameColor = p_jobj[NodeConfig::c_nameColor].toString();
  }
}

NodeParameters NodeFileConfig::toNodeParameters() const {
  NodeParameters paras;
  paras.m_id = m_id;
  paras.m_signature = m_signature;
  paras.m_createdTimeUtc = m_createdTimeUtc;
  paras.m_modifiedTimeUtc = m_modifiedTimeUtc;
  paras.m_tags = m_tags;
  paras.m_attachmentFolder = m_attachmentFolder;

  // Visual settings
  paras.m_visual.setBackgroundColor(m_backgroundColor);
  paras.m_visual.setBorderColor(m_borderColor);
  paras.m_visual.setNameColor(m_nameColor);

  return paras;
}

QJsonObject NodeFolderConfig::toJson() const {
  QJsonObject jobj;

  jobj[NodeConfig::c_name] = m_name;

  // Visual settings
  if (!m_backgroundColor.isEmpty()) {
    jobj[NodeConfig::c_backgroundColor] = m_backgroundColor;
  }
  if (!m_borderColor.isEmpty()) {
    jobj[NodeConfig::c_borderColor] = m_borderColor;
  }
  if (!m_nameColor.isEmpty()) {
    jobj[NodeConfig::c_nameColor] = m_nameColor;
  }

  return jobj;
}

void NodeFolderConfig::fromJson(const QJsonObject &p_jobj) {
  m_name = p_jobj[NodeConfig::c_name].toString();

  // Visual settings (check if fields exist for backward compatibility)
  if (p_jobj.contains(NodeConfig::c_backgroundColor)) {
    m_backgroundColor = p_jobj[NodeConfig::c_backgroundColor].toString();
  }
  if (p_jobj.contains(NodeConfig::c_borderColor)) {
    m_borderColor = p_jobj[NodeConfig::c_borderColor].toString();
  }
  if (p_jobj.contains(NodeConfig::c_nameColor)) {
    m_nameColor = p_jobj[NodeConfig::c_nameColor].toString();
  }
}

NodeParameters NodeFolderConfig::toNodeParameters() const {
  NodeParameters paras;

  // Visual settings
  paras.m_visual.setBackgroundColor(m_backgroundColor);
  paras.m_visual.setBorderColor(m_borderColor);
  paras.m_visual.setNameColor(m_nameColor);

  return paras;
}

NodeConfig::NodeConfig() {}

NodeConfig::NodeConfig(int p_version, ID p_id, ID p_signature, const QDateTime &p_createdTimeUtc,
                       const QDateTime &p_modifiedTimeUtc)
    : m_version(p_version), m_id(p_id), m_signature(p_signature),
      m_createdTimeUtc(p_createdTimeUtc), m_modifiedTimeUtc(p_modifiedTimeUtc) {}

QJsonObject NodeConfig::toJson() const {
  QJsonObject jobj;

  jobj[NodeConfig::c_version] = m_version;
  jobj[NodeConfig::c_id] = IDToString(m_id);
  jobj[NodeConfig::c_signature] = IDToString(m_signature);
  jobj[NodeConfig::c_createdTimeUtc] = Utils::dateTimeStringUniform(m_createdTimeUtc);
  jobj[NodeConfig::c_modifiedTimeUtc] = Utils::dateTimeStringUniform(m_modifiedTimeUtc);

  QJsonArray files;
  for (const auto &file : m_files) {
    files.append(file.toJson());
  }
  jobj[NodeConfig::c_files] = files;

  QJsonArray folders;
  for (const auto &folder : m_folders) {
    folders.append(folder.toJson());
  }
  jobj[NodeConfig::c_folders] = folders;

  // Visual settings for the container node itself
  if (!m_backgroundColor.isEmpty()) {
    jobj[NodeConfig::c_backgroundColor] = m_backgroundColor;
  }
  if (!m_borderColor.isEmpty()) {
    jobj[NodeConfig::c_borderColor] = m_borderColor;
  }
  if (!m_nameColor.isEmpty()) {
    jobj[NodeConfig::c_nameColor] = m_nameColor;
  }

  return jobj;
}

void NodeConfig::fromJson(const QJsonObject &p_jobj) {
  m_version = p_jobj[NodeConfig::c_version].toInt();

  m_id = stringToNodeId(p_jobj[NodeConfig::c_id].toString());
  m_signature = stringToNodeId(p_jobj[NodeConfig::c_signature].toString());

  m_createdTimeUtc =
      Utils::dateTimeFromStringUniform(p_jobj[NodeConfig::c_createdTimeUtc].toString());
  m_modifiedTimeUtc =
      Utils::dateTimeFromStringUniform(p_jobj[NodeConfig::c_modifiedTimeUtc].toString());

  auto filesJson = p_jobj[NodeConfig::c_files].toArray();
  m_files.resize(filesJson.size());
  for (int i = 0; i < filesJson.size(); ++i) {
    m_files[i].fromJson(filesJson[i].toObject());
  }

  auto foldersJson = p_jobj[NodeConfig::c_folders].toArray();
  m_folders.resize(foldersJson.size());
  for (int i = 0; i < foldersJson.size(); ++i) {
    m_folders[i].fromJson(foldersJson[i].toObject());
  }

  // Visual settings for the container node itself (check if fields exist for backward
  // compatibility)
  if (p_jobj.contains(NodeConfig::c_backgroundColor)) {
    m_backgroundColor = p_jobj[NodeConfig::c_backgroundColor].toString();
  }
  if (p_jobj.contains(NodeConfig::c_borderColor)) {
    m_borderColor = p_jobj[NodeConfig::c_borderColor].toString();
  }
  if (p_jobj.contains(NodeConfig::c_nameColor)) {
    m_nameColor = p_jobj[NodeConfig::c_nameColor].toString();
  }
}

NodeParameters NodeConfig::toNodeParameters() const {
  NodeParameters paras;
  paras.m_id = m_id;
  paras.m_signature = m_signature;
  paras.m_createdTimeUtc = m_createdTimeUtc;
  paras.m_modifiedTimeUtc = m_modifiedTimeUtc;

  // Visual settings for the container node itself
  paras.m_visual.setBackgroundColor(m_backgroundColor);
  paras.m_visual.setBorderColor(m_borderColor);
  paras.m_visual.setNameColor(m_nameColor);

  return paras;
}
