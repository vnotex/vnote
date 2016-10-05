#include "vconfigmanager.h"
#include <QDir>
#include <QFile>
#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QtDebug>

const QString VConfigManager::dirConfigFileName = QString(".vnote.json");

VConfigManager::VConfigManager()
{

}

QJsonObject VConfigManager::readDirectoryConfig(const QString &path)
{
    QString configFile = QDir(path).filePath(dirConfigFileName);

    qDebug() << "read config file:" << configFile;
    QFile config(configFile);
    if (!config.open(QIODevice::ReadOnly)) {
        qWarning() << "error: fail to read directory configuration file:"
                   << configFile;
        return QJsonObject();
    }

    QByteArray configData = config.readAll();
    return QJsonDocument::fromJson(configData).object();
}

bool VConfigManager::writeDirectoryConfig(const QString &path, const QJsonObject &configJson)
{
    QString configFile = QDir(path).filePath(dirConfigFileName);

    qDebug() << "write config file:" << configFile;
    QFile config(configFile);
    if (!config.open(QIODevice::WriteOnly)) {
        qWarning() << "error: fail to open directory configuration file for write:"
                   << configFile;
        return false;
    }

    QJsonDocument configDoc(configJson);
    config.write(configDoc.toJson());
    return true;
}

bool VConfigManager::deleteDirectoryConfig(const QString &path)
{
    QString configFile = QDir(path).filePath(dirConfigFileName);

    QFile config(configFile);
    if (!config.remove()) {
        qWarning() << "error: fail to delete directory configuration file:"
                   << configFile;
        return false;
    }
    qDebug() << "delete config file:" << configFile;
    return true;
}
