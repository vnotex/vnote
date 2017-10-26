#include "veditconfig.h"

#include "vconfigmanager.h"
#include "utils/vutils.h"

extern VConfigManager *g_config;

void VEditConfig::init(const QFontMetrics &p_metric,
                       bool p_enableHeadingSequence)
{
    update(p_metric);

    // Init configs that do not support update later.
    m_enableVimMode = g_config->getEnableVimMode();

    if (g_config->getLineDistanceHeight() <= 0) {
        m_lineDistanceHeight = 0;
    } else {
        m_lineDistanceHeight = g_config->getLineDistanceHeight() * VUtils::calculateScaleFactor();
    }

    m_highlightWholeBlock = m_enableVimMode;

    m_enableHeadingSequence = p_enableHeadingSequence;
}

void VEditConfig::update(const QFontMetrics &p_metric)
{
    if (g_config->getTabStopWidth() > 0) {
        m_tabStopWidth = g_config->getTabStopWidth() * p_metric.width(' ');
    } else {
        m_tabStopWidth = 0;
    }

    m_expandTab = g_config->getIsExpandTab();

    if (m_expandTab && (g_config->getTabStopWidth() > 0)) {
        m_tabSpaces = QString(g_config->getTabStopWidth(), ' ');
    } else {
        m_tabSpaces = "\t";
    }

    m_cursorLineBg = QColor(g_config->getEditorCurrentLineBg());
}
