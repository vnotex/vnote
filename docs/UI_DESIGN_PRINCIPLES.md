# VNote UI Design Principles

A comprehensive style guide for VNote's Qt-based user interface, inspired by modern IDE aesthetics (VS Code, JetBrains) while maintaining cross-platform consistency.

---

## Design Philosophy

### Core Principles

1. **Professional & Focused** - Clean UI that stays out of the way, letting content shine
2. **Consistent & Predictable** - Same visual language across all components
3. **Accessible & Readable** - Sufficient contrast, clear hierarchies, comfortable spacing
4. **Performant** - Lightweight styles that don't impact rendering

### Visual Identity

| Aspect | Approach |
|--------|----------|
| Style | Flat design with subtle depth cues |
| Edges | Consistently rounded corners |
| Chrome | Minimal borders, use background differentiation |
| States | Clear hover/focus/active feedback |
| Density | Comfortable spacing, not cramped |

---

## Spacing System

Use a consistent **4px base unit** for all spacing decisions.

| Token | Value | Use Case |
|-------|-------|----------|
| `xs` | 2px | Tight internal spacing, indicator gaps |
| `sm` | 4px | Component internal padding |
| `md` | 8px | Standard padding, gaps between elements |
| `lg` | 12px | Section spacing, larger padding |
| `xl` | 16px | Panel margins, major section gaps |
| `2xl` | 24px | Large section separation |

### Component Padding Guidelines

| Component | Padding |
|-----------|---------|
| Buttons (QPushButton) | 6px 12px (vertical, horizontal) |
| Tool buttons (QToolButton) | 4px |
| Input fields (QLineEdit, QComboBox) | 4px 8px |
| Menu items | 6px 16px 6px 8px (top, right, bottom, left) |
| Menu icons | 4px 4px 4px 8px |
| Menu indicators | 16px with 6px left margin |
| Tab items | 6px 16px (top/bottom), 8px 6px (left/right) |
| List/Tree items | 4px 8px |
| Dock widget title | 4px |
| Dock widget buttons | 2px |

---

## Border Radius System

Consistent corner rounding creates visual cohesion.

| Token | Value | Use Case |
|-------|-------|----------|
| `none` | 0px | Tab bars, toolbar edges, full-bleed elements |
| `sm` | 4px | Buttons, inputs, small interactive elements |
| `md` | 6px | Menus, dropdowns, dialogs, cards |
| `lg` | 8px | Tooltips, major containers, modals |
| `full` | 50% | Circular buttons, badges, avatars |

### Component Radius Guidelines

| Component | Radius |
|-----------|--------|
| QPushButton | 4px |
| QToolButton | 4px |
| QLineEdit, QComboBox | 4px |
| QMenu | 6px |
| QToolTip | 6px |
| QGroupBox | 6px |
| QDialog | 8px |
| Scrollbar handle | 4px |
| Checkbox/Radio | 4px (checkbox), 50% (radio) |

---

## Border System

Minimize heavy borders; prefer background differentiation.

| Style | Value | Use Case |
|-------|-------|----------|
| None | 0px | Most interactive elements |
| Subtle | 1px solid | Input fields (idle state), separators |
| Focus | 1px solid [accent] | Focused inputs, active states |
| Section | 1px solid [border-color] | Panel separators, dividers |

### Border Color Opacity

For subtle borders that adapt to both light and dark themes:
- Light theme borders: ~10-15% opacity black
- Dark theme borders: ~15-20% opacity white

---

## Color System

### Semantic Color Tokens

Colors should be defined in `palette.json` using semantic naming:

```
base
├── normal (default state)
│   ├── fg    # Primary text
│   ├── bg    # Primary background
│   └── border # Default border
├── master (accent/brand)
│   ├── fg    # Text on accent
│   ├── bg    # Accent color
│   └── alt   # Secondary accent
├── content (content areas)
│   ├── fg, bg, border
│   ├── selection, hover, focus, selected
├── states
│   ├── disabled, pressed, focus, hover, selected
└── feedback
    ├── error, warning, info, danger
```

### Contrast Requirements (WCAG AA)

| Text Type | Minimum Ratio |
|-----------|---------------|
| Body text | 4.5:1 |
| Large text (18px+) | 3:1 |
| UI components | 3:1 |
| Decorative | No minimum |

### State Colors (Opacity-based)

For consistent interactive states across themes:

| State | Light Theme | Dark Theme |
|-------|-------------|------------|
| Hover | rgba(0,0,0, 0.04) | rgba(255,255,255, 0.06) |
| Focus | rgba(0,0,0, 0.08) | rgba(255,255,255, 0.10) |
| Pressed | rgba(0,0,0, 0.12) | rgba(255,255,255, 0.14) |
| Selected | rgba(accent, 0.15) | rgba(accent, 0.20) |

---

## Typography

### Font Stack

```css
font-family: 
  -apple-system,           /* macOS */
  BlinkMacSystemFont,      /* macOS Chrome */
  "Segoe UI",              /* Windows */
  "Microsoft YaHei",       /* Windows CJK */
  "Noto Sans",             /* Linux */
  "Helvetica Neue",        /* Fallback */
  sans-serif;
```

### Font Sizes

| Token | Size | Use Case |
|-------|------|----------|
| `xs` | 11px | Labels, captions, metadata |
| `sm` | 12px | Secondary text, toolbar labels |
| `base` | 13px | Default UI text |
| `lg` | 14px | Important labels, headings |
| `xl` | 16px | Panel titles, section headers |
| `2xl` | 20px | Major headings, tips |

---

## Component Specifications

### Scrollbars

**Modern slim style** - Unobtrusive, appears on hover.

```css
/* Scrollbar track */
width: 10px;           /* Vertical */
height: 10px;          /* Horizontal */
background: transparent;
margin: 2px;

/* Scrollbar handle */
min-height: 40px;
min-width: 40px;
background: rgba(128,128,128, 0.3);
border-radius: 4px;

/* Handle states */
:hover   → rgba(128,128,128, 0.5)
:pressed → rgba(128,128,128, 0.7)

/* No arrow buttons */
::add-line, ::sub-line {
  width: 0; height: 0;
}
```

### Buttons (QPushButton)

```css
padding: 6px 12px;
min-width: 64px;
border-radius: 4px;
border: 1px solid [border-color];
background: transparent;

:hover   → background: [hover-bg]
:pressed → background: [pressed-bg]
:focus   → border-color: [accent]
:default → border-color: [accent]
```

### Tool Buttons (QToolButton)

```css
padding: 4px;
margin: 2px;
border: none;
border-radius: 4px;
background: transparent;

:hover   → background: [hover-bg]
:pressed → background: [pressed-bg]
:checked → background: [selected-bg]
```

### Input Fields (QLineEdit, QComboBox, QSpinBox)

```css
padding: 4px 8px;
min-height: 28px;
border: 1px solid [border-color];
border-radius: 4px;
background: [content-bg];

:hover → border-color: [hover-border]; background: [hover-bg]
:focus → border-color: [accent]; background: [focus-bg]
```

### Menus (QMenu)

```css
border: 1px solid [border-color];
border-radius: 6px;
padding: 4px 0;
background: [menu-bg];

/* Menu items */
::item {
  padding: 6px 16px 6px 8px;  /* Tighter left padding */
  margin: 0 4px;
  border-radius: 4px;
}
::item:selected {
  background: [selected-bg];
}

/* Icons */
::icon {
  margin: 4px 4px 4px 8px;  /* Balanced icon spacing */
}

/* Checkbox/Radio indicators */
::indicator {
  width: 16px;
  height: 16px;
  margin-left: 6px;  /* Tight spacing to text */
}

/* Separator */
::separator {
  height: 1px;
  margin: 4px 12px;
}
```

### Tabs (QTabBar)

```css
/* Tab item */
::tab {
  padding: 6px 16px;
  border: none;
  border-bottom: 2px solid transparent;  /* Indicator */
}
::tab:selected {
  border-bottom-color: [accent];
  background: [content-bg];
}
::tab:hover:!selected {
  background: [hover-bg];
}
```

### Tree/List Views (QTreeView, QListView)

```css
border: none;
background: [content-bg];
show-decoration-selected: 0;  /* Branch area not highlighted */

::item {
  padding: 4px 8px;
  /* No margin/border-radius - full-width selection avoids
     visual gaps with branch indicators */
}
::item:hover {
  background: [hover-bg];
}
::item:selected {
  background: [selected-bg];
}
```

### Tooltips (QToolTip)

```css
padding: 6px 10px;
border: none;
border-radius: 6px;
background: [tooltip-bg];
color: [tooltip-fg];
```

### Dock Widgets (QDockWidget)

```css
/* Title bar */
::title {
  background: [title-bg];
  border-radius: 6px;
  padding: 4px;
}

/* Title bar buttons (close, float) */
::close-button, ::float-button {
  border: none;
  border-radius: 4px;
  padding: 2px;
}
::close-button:hover, ::float-button:hover {
  background: [hover-bg];
}
::close-button:pressed, ::float-button:pressed {
  background: [pressed-bg];
}
```

### Dialogs (QDialog)

```css
border-radius: 8px;  /* If platform supports */
background: [dialog-bg];
```

### Progress Bar (QProgressBar)

```css
height: 6px;
border: none;
border-radius: 3px;
background: [groove-bg];

::chunk {
  border-radius: 3px;
  background: [accent];
}
```

### Slider (QSlider)

```css
/* Groove */
::groove:horizontal {
  height: 4px;
  border-radius: 2px;
  background: [groove-bg];
}

/* Handle */
::handle:horizontal {
  width: 16px;
  height: 16px;
  border-radius: 8px;  /* Circular */
  background: [accent];
  margin: -6px 0;
}
```

---

## Animation & Transitions

**Note**: QSS has limited animation support. These are aspirational guidelines.

| Property | Duration | Easing |
|----------|----------|--------|
| Background color | 150ms | ease-out |
| Border color | 150ms | ease-out |
| Opacity | 200ms | ease-in-out |

For animations, consider implementing in C++ code using `QPropertyAnimation`.

---

## Iconography

### Icon Sizes

| Context | Size |
|---------|------|
| Toolbar | 16x16 or 20x20 |
| Menu | 16x16 |
| Tree/List | 16x16 |
| Large action | 24x24 |
| Tips/Empty state | 48x48+ |

### Icon Style

- **Monochrome** - Single color, adapts to theme
- **Line style** - 1.5px stroke weight
- **Consistent metaphors** - Use standard conventions

---

## Theme-Specific Adjustments

### Light Themes
- Subtle shadows for elevation (if supported)
- Lighter hover states
- Slightly more contrast in borders

### Dark Themes
- Slightly lighter backgrounds for elevation
- Stronger hover state contrast
- Reduced border visibility (use background separation)

---

## Implementation Checklist

When updating a theme's `interface.qss`:

- [ ] Apply consistent border-radius (4px/6px/8px scale)
- [ ] Update scrollbars to slim modern style
- [ ] Increase button/input padding
- [ ] Add border-radius to tree/list item selections
- [ ] Soften menu borders and add radius
- [ ] Update tooltip styling
- [ ] Ensure consistent hover/focus states
- [ ] Remove arrow buttons from scrollbars
- [ ] Test all interactive states

---

## File Structure

```
themes/[theme-name]/
├── palette.json        # Color definitions
├── interface.qss       # Qt stylesheet
├── web.css             # Markdown preview styles
├── highlight.css       # Code syntax highlighting
├── text-editor.theme   # Editor colors
└── *.svg               # Theme-specific icons
```

---

## References

- [Qt Style Sheets Reference](https://doc.qt.io/qt-6/stylesheet-reference.html)
- [VS Code Design Guidelines](https://code.visualstudio.com/api/ux-guidelines/overview)
- [Material Design Color System](https://m3.material.io/styles/color/overview)
- [WCAG Contrast Guidelines](https://www.w3.org/WAI/WCAG21/Understanding/contrast-minimum.html)

---

*Last updated: February 2026*
