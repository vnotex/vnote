# VNote 自定义思维导图（Mind Map）功能文档

本功能基于 `MindElixir.js` 实现了相关思维导图与 VNote 笔记的增强。

## 1. 工程架构

新的思维导图功能遵循清晰的模块化目录结构，以便于维护和扩展。所有相关文件都位于 `src/data/extra/web/js/mindmap/` 目录下。

```
mindmap/
├── core/
│   └── mindmap-core.js       # 核心逻辑，封装第三方库
├── features/
│   ├── link-handler/
│   │   └── link-handler.js   # 功能模块：链接增强
│   └── outline/
│       └── outline.js        # 功能模块：大纲视图
├── lib/
│   └── mind-elixir/
│       └── MindElixir.js     # 第三方依赖库
└── mindmap-readme.md         # 本文档
```

- **`lib/`**: 存放第三方依赖库，目前为 `MindElixir.js`。这使得主代码与外部库解耦。
- **`core/`**: 存放核心封装和逻辑。`mindmap-core.js` 作为 `MindElixir.js` 的直接封装层，为上层应用提供统一、稳定的接口，并管理各个功能模块的生命周期。
- **`features/`**: 存放所有可插拔的功能模块。每个子目录代表一个独立的功能（如链接处理、大纲视图）

此外，在 `mindmapeditor.js` 的同级目录下，还有一个 `vxcore.js` 文件，它提供了与Qt后端通信的基础能力。

## 2. 架构设计与开发指南

为了实现高度的灵活性和可扩展性，我们采用了分层和面向对象的插件式架构。

### 2.1. 核心关系：`mindmapeditor.js` 与 `mindmap-core.js`

两者的关系是 **组合（Composition）而非继承**，这是一种“has-a”关系，遵循了组合优于继承的设计原则。

-   **`MindMapEditor` (`mindmapeditor.js`)**:
    -   **角色**: **集成与通信层 (The Integrator)**。
    -   **职责**:
        1.  **继承 `VXCore`**: 获取与Qt后端通信的基础能力。
        2.  **对接Qt**: 作为JavaScript世界与Qt世界的桥梁，处理来自 `vxAdapter` 对象的信号（如 `saveDataRequested`, `dataUpdated`）和调用其方法（如 `setSavedData`）。
        3.  **创建核心实例**: `MindMapEditor` 在其构造函数中创建 `MindMapCore` 的实例。它“拥有”一个 `MindMapCore`。
        4.  **注册功能模块**: 它决定加载哪些功能，并调用 `mindMapCore.registerFeature()` 方法将 `OutlineFeature` 和 `LinkHandlerFeature` 等模块“注入”到核心中。

-   **`MindMapCore` (`mindmap-core.js`)**:
    -   **角色**: **封装与管理层 (The Engine)**。
    -   **职责**:
        1.  **封装 `MindElixir`**: 直接初始化和操作 `MindElixir.js` 实例。所有对思维导图的底层操作（如设置数据、获取数据、布局）都由它代理。这隐藏了第三方库的实现细节。
        2.  **管理功能模块**: 内部维护一个功能模块列表（`features` Map）。提供了 `registerFeature()`、`getFeature()` 等方法，并负责在适当的时机（如 `init`, `onDataChange`）调用每个模块的生命周期方法。
        3.  **事件中心**: 拥有自己的事件系统（`on`, `emit`），发布如 `ready`, `contentChanged`, `saveCompleted` 等关键事件，让上层和同级模块能响应核心状态的变化。

这种设计带来了几个好处：
-   **解耦**: `MindMapEditor` 不关心用的是哪个思维导图库，它只与 `MindMapCore` 的稳定API交互。未来如果更换 `MindElixir.js`，只需重写 `MindMapCore`，而 `MindMapEditor` 和所有功能模块几乎不受影响。
-   **清晰职责**: `MindMapEditor` 负责“对外”（与Qt通信），`MindMapCore` 负责“对内”（管理思维导图和功能）。
-   **可扩展性**: 新功能可以作为独立的`Feature`类开发，然后在 `MindMapEditor` 中注册即可，无需修改核心代码。

### 2.2. 功能模块（Feature）的实现规范

所有功能模块（如 `LinkHandlerFeature`, `OutlineFeature`）都遵循一个统一的接口约定：

-   是一个独立的 `class`。
-   **`setCore(core)`**: 一个方法，由 `MindMapCore` 在注册时调用，用于将核心实例注入到模块中，使模块能访问核心功能（如 `this.core.mindElixir`）。
-   **`init()`**: 初始化方法。在 `MindMapCore` 初始化完成后被调用，用于设置事件监听、创建UI元素等。
-   **`onDataChange(data)`**: 当思维导图加载新数据时被调用，用于同步模块状态。

### 2.3. 未来如何开发新功能

如果你想基于当前架构添加一个新的自定义功能（例如“节点计数器”），应遵循以下步骤：

1.  **创建功能文件**: 在 `features/` 目录下创建一个新的子目录，例如 `node-counter/`，并在其中创建 `node-counter.js` 文件。
2.  **实现功能类**: 在 `node-counter.js` 中，创建一个 `NodeCounterFeature` 类，并实现 `setCore`, `init` 等必要方法。
    ```javascript
    class NodeCounterFeature {
        setCore(core) {
            this.core = core;
        }

        init() {
            // 创建一个显示计数的UI元素
            // ...
            this.updateCount();
        }

        onDataChange(data) {
            // 数据变化时更新计数
            this.updateCount();
        }

        updateCount() {
            const nodeCount = this.core.mindElixir.getAllData().nodeData.children.length;
            // 更新UI...
        }
    }
    ```
3.  **注册新功能**: 在 `mindmapeditor.js` 的 `setupFeatures` 方法中，实例化并注册你的新功能。
    ```javascript
    // in mindmapeditor.js
    setupFeatures() {
        // ... aiting other features
        this.mindMapCore.registerFeature('nodeCounter', new NodeCounterFeature());
    }
    ```
4.  **更新HTML模板**: 根据项目的设计模式 [[memory:4144812]]，不要在 `mindmap-editor-template.html` 中硬编码JS路径。应在 `VNote` 的资源管理系统中注册新JS文件，使其在后端被自动注入。

## 3. 已实现功能介绍

### 3.1. 链接增强 (`LinkHandlerFeature`)

此功能彻底重做了 `MindElixir` 的默认超链接行为，提供了更强大、更符合 `VNote` 使用场景的交互。

-   **可视化标签**: 它会检测节点数据中的 `hyperLink` 字段，并自动在节点文本旁生成一个可视化的标签（如 `[md]`, `[pdf]`, `[http]`）。标签的样式会根据链接类型（文件扩展名）变化，一目了然。
-   **定向打开**: 这是此功能的核心。用户可以通过 **拖拽** 这个链接标签来决定在 `VNote` 的哪个区域打开链接：
    -   向上拖拽: 在上方打开
    -   向下拖拽: 在下方打开
    -   向左拖拽: 在左侧打开
    -   向右拖拽或直接点击: 在右侧打开（默认）
-   **动态更新**: 利用 `MutationObserver`，无论是添加新节点、编辑现有节点还是撤销/重做操作，链接标签都能被实时、正确地渲染，并保持布局不乱。

### 3.2. 大纲 (`OutlineFeature`)

此功能为复杂的思维导图提供了一个悬浮的、可交互的大纲窗口，极大地提升了导航和概览效率。

-   **悬浮窗口**: 大纲是一个独立、可拖拽、可调整大小的悬浮窗口。
-   **实时同步**: 大纲内容与思维导图实时双向同步。在思维导图中做的任何修改都会立刻反映到大纲树状图中。
-   **快速导航**: 在大纲窗口中点击任意节点，主思维导图视图会自动平移并将该节点居中高亮显示。
-   **搜索过滤**: 内置的搜索框可以快速过滤大纲，只显示匹配关键词的节点及其父节点，方便在大型脑图中快速定位信息。
-   **界面调整**:
    -   **折叠/展开**: 用户可以在大纲中自由折叠和展开节点，以关注不同层级的内容。
    -   **自适应布局**: 当主窗口尺寸缩小时，大纲窗口会自动折叠并移动到角落，避免遮挡内容；当主窗口恢复尺寸时，大纲窗口也会自动展开并恢复到原来的位置和大小。
