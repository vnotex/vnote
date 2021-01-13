#ifndef WEBRESOURCE_H
#define WEBRESOURCE_H

#include <QJsonObject>
#include <QJsonArray>
#include <QStringList>
#include <QVector>

namespace vnotex
{
    // Resource for Web.
    struct WebResource
    {
        struct Resource
        {
            void init(const QJsonObject &p_obj)
            {
                m_name = p_obj[QStringLiteral("name")].toString();
                m_enabled = p_obj[QStringLiteral("enabled")].toBool();

                m_styles.clear();
                auto stylesArray = p_obj[QStringLiteral("styles")].toArray();
                for (int i = 0; i < stylesArray.size(); ++i) {
                    m_styles << stylesArray[i].toString();
                }

                m_scripts.clear();
                auto scriptsArray = p_obj[QStringLiteral("scripts")].toArray();
                for (int i = 0; i < scriptsArray.size(); ++i) {
                    m_scripts << scriptsArray[i].toString();
                }
            }

            QJsonObject toJson() const
            {
                QJsonObject obj;
                obj[QStringLiteral("name")] = m_name;
                obj[QStringLiteral("enabled")] = m_enabled;

                QJsonArray stylesArray;
                for (const auto &ele : m_styles) {
                    stylesArray.append(ele);
                }
                obj[QStringLiteral("styles")] = stylesArray;

                QJsonArray scriptsArray;
                for (const auto &ele : m_scripts) {
                    scriptsArray.append(ele);
                }
                obj[QStringLiteral("scripts")] = scriptsArray;
                return obj;
            }

            bool isGlobal() const
            {
                return m_name == QStringLiteral("global_styles");
            }

            QString m_name;

            bool m_enabled = true;

            QStringList m_styles;

            QStringList m_scripts;
        };

        void init(const QJsonObject &p_obj)
        {
            m_template = p_obj[QStringLiteral("template")].toString();

            auto ary = p_obj[QStringLiteral("resources")].toArray();
            m_resources.resize(ary.size());
            for (int i = 0; i < ary.size(); ++i) {
                m_resources[i].init(ary[i].toObject());
            }
        }

        QJsonObject toJson() const
        {
            QJsonObject obj;
            obj[QStringLiteral("template")] = m_template;

            {
                QJsonArray ary;
                for (const auto &ele : m_resources) {
                    ary.append(ele.toJson());
                }
                obj[QStringLiteral("resources")] = ary;
            }

            return obj;
        }

        // HTML template file.
        QString m_template;

        // Resources to fill in the template.
        QVector<Resource> m_resources;
    };

}

#endif // WEBRESOURCE_H
