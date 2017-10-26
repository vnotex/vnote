#ifndef VEDITCONFIG_H
#define VEDITCONFIG_H

#include <QFontMetrics>
#include <QString>
#include <QColor>


class VEditConfig {
public:
    VEditConfig() : m_tabStopWidth(0),
                    m_tabSpaces("\t"),
                    m_enableVimMode(false),
                    m_highlightWholeBlock(false),
                    m_lineDistanceHeight(0),
                    m_enableHeadingSequence(false)
    {}

    void init(const QFontMetrics &p_metric,
              bool p_enableHeadingSequence);

    // Only update those configs which could be updated online.
    void update(const QFontMetrics &p_metric);

    // Width in pixels.
    int m_tabStopWidth;

    bool m_expandTab;

    // The literal string for Tab. It is spaces if Tab is expanded.
    QString m_tabSpaces;

    bool m_enableVimMode;

    // The background color of cursor line.
    QColor m_cursorLineBg;

    // Whether highlight a visual line or a whole block.
    bool m_highlightWholeBlock;

    // Line distance height in pixels.
    int m_lineDistanceHeight;

    // Whether enable auto heading sequence.
    bool m_enableHeadingSequence;
};

#endif // VEDITCONFIG_H
