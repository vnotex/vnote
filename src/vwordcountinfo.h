#ifndef VWORDCOUNTINFO_H
#define VWORDCOUNTINFO_H

#include <QString>
#include <QDebug>


struct VWordCountInfo
{
    enum Mode
    {
        Read = 0,
        Edit,
        Invalid
    };

    VWordCountInfo()
        : m_mode(Mode::Invalid),
          m_wordCount(-1),
          m_charWithoutSpacesCount(-1),
          m_charWithSpacesCount(-1)
    {
    }

    bool isNull() const
    {
        return m_mode == Mode::Invalid;
    }

    void clear()
    {
        m_mode = Mode::Invalid;
        m_wordCount = -1;
        m_charWithoutSpacesCount = -1;
        m_charWithSpacesCount = -1;
    }

    QString toString() const
    {
        return QString("VWordCountInfo mode %1 WC %2 CNSC %3 CSC %4")
                      .arg(m_mode)
                      .arg(m_wordCount)
                      .arg(m_charWithoutSpacesCount)
                      .arg(m_charWithSpacesCount);
    }

    Mode m_mode;
    int m_wordCount;
    int m_charWithoutSpacesCount;
    int m_charWithSpacesCount;
};

#endif // VWORDCOUNTINFO_H
