#include "mindmapeditorconfig.h"

#include "mainconfig.h"

#define READSTR(key) readString(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))
#define READINT(key) readInt(appObj, userObj, (key))

using namespace vnotex;

MindMapEditorConfig::MindMapEditorConfig(ConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig)
{
    m_sessionName = QStringLiteral("mindmap_editor");
}

void MindMapEditorConfig::init(const QJsonObject &p_app,
                               const QJsonObject &p_user)
{
    const auto appObj = p_app.value(m_sessionName).toObject();
    const auto userObj = p_user.value(m_sessionName).toObject();

    loadEditorResource(appObj, userObj);
}

QJsonObject MindMapEditorConfig::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("editor_resource")] = saveEditorResource();
    return obj;
}

void MindMapEditorConfig::loadEditorResource(const QJsonObject &p_app, const QJsonObject &p_user)
{
    const QString name(QStringLiteral("editor_resource"));

    if (MainConfig::isVersionChanged()) {
        bool needOverride = p_app[QStringLiteral("override_editor_resource")].toBool();
        if (needOverride) {
            qInfo() << "override \"editor_resource\" in user configuration due to version change";
            m_editorResource.init(p_app[name].toObject());
            return;
        }
    }

    if (p_user.contains(name)) {
        m_editorResource.init(p_user[name].toObject());
    } else {
        m_editorResource.init(p_app[name].toObject());
    }
}

QJsonObject MindMapEditorConfig::saveEditorResource() const
{
    return m_editorResource.toJson();
}

const WebResource &MindMapEditorConfig::getEditorResource() const
{
    return m_editorResource;
}
