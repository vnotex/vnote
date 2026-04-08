# src/gui/ — GUI-Aware Services and Utilities

`src/gui/` contains GUI-aware services and utilities that depend on Qt Widgets/Gui modules. These are separated from `src/core/services/` which are Qt-minimal.

## services/

| Class | Purpose |
|-------|---------|
| `ThemeService` | GUI-aware theme management — loading themes, applying stylesheets |
| `ViewWindowFactory` | Registry pattern mapping file types to `ViewWindow2` creators; plugins register new viewers here |
| `NavigationModeService` | Keyboard navigation mode service |

## utils/

| Class | Purpose |
|-------|---------|
| `WidgetUtils` | Widget utility helpers (focus, geometry, etc.) |
| `ThemeUtils` | Theme file loading and parsing utilities |
| `ImageUtils` | Image processing utilities |
| `GuiUtils` | General GUI utilities |
| `IconUtils` | Icon loading and management |
| `PrintUtils` | Print/export utilities |

## Core vs GUI Distinction

Core services (`src/core/services/`) wrap the vxcore C API and have minimal Qt dependencies. GUI services (`src/gui/services/`) require Qt Widgets and handle presentation concerns.

## Related Modules

- [`../core/AGENTS.md`](../core/AGENTS.md) — Core services that GUI services extend/wrap
- [`../widgets/AGENTS.md`](../widgets/AGENTS.md) — Widgets that consume GUI services
- [`../../AGENTS.md`](../../AGENTS.md) — Architecture overview, code style
