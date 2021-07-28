#ifndef VNOTEX_GLOBAL_H
#define VNOTEX_GLOBAL_H

#include <QString>
#include <QPair>
#include <QDebug>
#include <QJsonObject>

namespace vnotex
{
    typedef quint64 ID;

    inline QPair<bool, ID> stringToID(const QString &p_str)
    {
        bool ok;
        ID id = p_str.toULongLong(&ok);
        return qMakePair(ok, id);
    }

    inline QString IDToString(ID p_id)
    {
        return QString::number(p_id);
    }

    typedef quint64 TimeStamp;

    struct Info
    {
        Info(const QString &p_name, const QString &p_displayName, const QString &p_description)
            : m_name(p_name), m_displayName(p_displayName), m_description(p_description)
        {
        }

        // Name for identification.
        QString m_name;

        // User-visible name.
        QString m_displayName;

        QString m_description;
    };

    enum { CONTENTS_MARGIN = 2 };

    inline QString QJsonObjectToString(const QJsonObject &p_obj)
    {
        QString str = "{";

        auto keys = p_obj.keys();
        for (auto &key : keys) {
            str += "\"" + key + "\": \"" + p_obj.value(key).toString() + "\";";
        }

        str += "}";
        return str;
    }

    inline QDebug operator<<(QDebug p_debug, const QJsonObject &p_obj)
    {
        QDebugStateSaver saver(p_debug);
        p_debug << QJsonObjectToString(p_obj);
        return p_debug;
    }

    enum FindOption
    {
        FindNone = 0,
        FindBackward = 0x1U,
        CaseSensitive = 0x2U,
        WholeWordOnly = 0x4U,
        RegularExpression = 0x8U,
        IncrementalSearch = 0x10U,
        // Used in full-text search.
        FuzzySearch = 0x20U
    };
    Q_DECLARE_FLAGS(FindOptions, FindOption);

    enum OverrideState
    {
        NoOverride = 0,
        ForceEnable = 1,
        ForceDisable = 2
    };

    enum class Alignment
    {
        None,
        Left,
        Center,
        Right
    };

    enum class ViewWindowMode
    {
        Read,
        Edit,
        FullPreview,
        FocusPreview,
        Invalid
    };

    enum { InvalidViewSplitId = 0 };

    enum class Direction
    {
        Left,
        Down,
        Up,
        Right
    };

    struct Segment
    {
        Segment() = default;

        Segment(int p_offset, int p_length)
            : m_offset(p_offset),
              m_length(p_length)
        {
        }

        bool operator<(const Segment &p_other) const
        {
            if (m_offset < p_other.m_offset) {
                return true;;
            } else {
                return m_length < p_other.m_length;
            }
        }

        int m_offset = 0;

        int m_length = -1;
    };

} // ns vnotex

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::FindOptions);

Q_DECLARE_METATYPE(vnotex::Segment);

#endif // GLOBAL_H
