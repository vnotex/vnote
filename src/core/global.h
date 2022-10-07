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

    enum class LineEndingPolicy
    {
        Platform,
        File,
        LF,
        CRLF,
        CR
    };

    inline QString lineEndingPolicyToString(LineEndingPolicy p_ending)
    {
        switch (p_ending) {
        case LineEndingPolicy::Platform:
            return QStringLiteral("platform");

        case LineEndingPolicy::File:
            return QStringLiteral("file");

        case LineEndingPolicy::LF:
            return QStringLiteral("lf");

        case LineEndingPolicy::CRLF:
            return QStringLiteral("crlf");

        case LineEndingPolicy::CR:
            return QStringLiteral("cr");
        }

        return QStringLiteral("platform");
    }

    inline LineEndingPolicy stringToLineEndingPolicy(const QString &p_str)
    {
        auto ending = p_str.toLower();
        if (ending == QStringLiteral("file")) {
            return LineEndingPolicy::File;
        } else if (ending == QStringLiteral("lf")) {
            return LineEndingPolicy::LF;
        } else if (ending == QStringLiteral("crlf")) {
            return LineEndingPolicy::CRLF;
        } else if (ending == QStringLiteral("cr")) {
            return LineEndingPolicy::CR;
        } else {
            return LineEndingPolicy::Platform;
        }
    }

    enum Role
    {
        // Qt::UserRole = 0x0100
        UserRole2 = 0x0101,
        HighlightsRole = 0x0102,
        // Used for comparison.
        ComparisonRole = 0x0103
    };

    enum ViewOrder
    {
        OrderedByConfiguration = 0,
        OrderedByName,
        OrderedByNameReversed,
        OrderedByCreatedTime,
        OrderedByCreatedTimeReversed,
        OrderedByModifiedTime,
        OrderedByModifiedTimeReversed,
        ViewOrderMax
    };
} // ns vnotex

Q_DECLARE_OPERATORS_FOR_FLAGS(vnotex::FindOptions);

Q_DECLARE_METATYPE(vnotex::Segment);

#endif // GLOBAL_H
