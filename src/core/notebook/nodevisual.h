#ifndef NODEVISUAL_H
#define NODEVISUAL_H

#include <QString>

/*
 * 节点视觉效果类
 * 自定义节点名称，背景颜色，边框颜色，节点名称颜色
 * 支持清除所有颜色，包括背景颜色，边框颜色，节点名称颜色
 * 支持级联修改，包括背景颜色，边框颜色，节点名称颜色
 */
namespace vnotex {
class NodeVisual {
public:
  NodeVisual() = default;

  NodeVisual(const QString &p_backgroundColor, const QString &p_borderColor,
             const QString &p_nameColor);

  // 背景颜色
  const QString &getBackgroundColor() const { return m_backgroundColor; }
  void setBackgroundColor(const QString &p_color) { m_backgroundColor = p_color; }

  // 边框颜色
  const QString &getBorderColor() const { return m_borderColor; }
  void setBorderColor(const QString &p_color) { m_borderColor = p_color; }

  // 节点名称颜色
  const QString &getNameColor() const { return m_nameColor; }
  void setNameColor(const QString &p_color) { m_nameColor = p_color; }

  // 判断是否有任何视觉效果
  bool hasAnyVisualEffect() const;

  // 清除所有颜色
  void clearAllColors();

private:
  QString m_backgroundColor; // 背景颜色
  QString m_borderColor;     // 边框颜色
  QString m_nameColor;       // 节点名称颜色
};
} // namespace vnotex

#endif // NODEVISUAL_H
