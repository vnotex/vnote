/**
 * 思维导图大纲功能模块
 * 提供思维导图节点的大纲视图和导航功能
 */
class OutlineFeature {
    constructor() {
        this.core = null;
        this.outlineWindow = null;
        this.nodeDataMap = new Map();
        this.isCollapsed = false;
        this.isResizing = false;
        this.originalSize = { width: 280, height: 500 };
        this.minimumSize = { width: 200, height: 300 };
        this.defaultPosition = { top: 580, right: 20 };
        this.lastPosition = null; // 记录最后的位置
        this.lastSize = null; // 记录最后的大小
        this.COLLAPSE_THRESHOLD = 750; // 思维导图尺寸小于这个值时自动折叠
        this.titleBarHeight = 45; // 标题栏高度
    }

    /**
     * 设置核心实例引用
     * @param {MindMapCore} core - 核心实例
     */
    setCore(core) {
        this.core = core;
    }

    /**
     * 初始化大纲功能
     * 步骤：
     * 1. 创建大纲窗口
     * 2. 设置DOM观察器
     */
    init() {
        console.log('OutlineFeature: init called');
        // 先检查并删除已存在的大纲窗口
        const existingWindow = document.getElementById('vx-outline-window');
        if (existingWindow) {
            existingWindow.remove();
        }
        this.createOutlineWindow();
        this.setupDOMObserver();
        this.setupResizeObserver();
        console.log('OutlineFeature: initialization complete');
    }

    /**
     * 创建大纲窗口
     * 步骤：
     * 1. 创建窗口容器
     * 2. 添加标题栏、搜索框和内容区
     * 3. 设置拖拽功能
     * 4. 添加事件监听
     */
    createOutlineWindow() {
        // 创建大纲窗口容器
        this.outlineWindow = document.createElement('div');
        this.outlineWindow.id = 'vx-outline-window';
        this.outlineWindow.className = 'vx-outline-window';
        
        // 设置窗口样式
        this.outlineWindow.style.cssText = `
            position: fixed;
            top: ${this.defaultPosition.top}px;
            right: ${this.defaultPosition.right}px;
            width: ${this.originalSize.width}px;
            height: ${this.originalSize.height}px;
            background: #ffffff;
            border: 1px solid #e0e0e0;
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.15);
            z-index: 1000;
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            overflow: hidden;
            user-select: none;
            display: flex;
            flex-direction: column;
            transition: width 0.3s ease, height 0.3s ease;
        `;

        // 创建标题栏
        const titleBar = document.createElement('div');
        titleBar.className = 'vx-outline-title';
        titleBar.style.cssText = `
            background: #f8f9fa;
            padding: 12px 16px;
            border-bottom: 1px solid #e0e0e0;
            cursor: move;
            user-select: none;
            font-weight: 600;
            font-size: 14px;
            color: #333;
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-shrink: 0;
            height: ${this.titleBarHeight}px;
            box-sizing: border-box;
            position: relative;
            z-index: 2;
        `;

        // 创建标题文本
        const titleText = document.createElement('span');
        titleText.textContent = '脑图大纲';
        titleText.style.cssText = `
            flex: 1;
            text-align: center;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
        `;

        // 创建折叠按钮
        const collapseButton = document.createElement('button');
        collapseButton.className = 'vx-outline-collapse-btn';
        collapseButton.innerHTML = '◀';
        collapseButton.title = '折叠/展开';
        collapseButton.style.cssText = `
            width: 24px;
            height: 24px;
            border: 1px solid #ddd;
            border-radius: 4px;
            background: #fff;
            color: #666;
            cursor: pointer;
            font-size: 12px;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: all 0.2s ease;
            margin-left: 8px;
            outline: none;
            position: relative;
            z-index: 3;
        `;

        collapseButton.addEventListener('click', () => this.toggleCollapse());
        collapseButton.addEventListener('mouseenter', () => {
            collapseButton.style.backgroundColor = '#f0f0f0';
            collapseButton.style.borderColor = '#999';
        });
        collapseButton.addEventListener('mouseleave', () => {
            collapseButton.style.backgroundColor = '#fff';
            collapseButton.style.borderColor = '#ddd';
        });

        titleBar.appendChild(titleText);
        titleBar.appendChild(collapseButton);

        // 创建搜索框容器
        const searchContainer = document.createElement('div');
        searchContainer.className = 'vx-outline-search';
        searchContainer.style.cssText = `
            padding: 8px 12px;
            border-bottom: 1px solid #e0e0e0;
            background: #fafafa;
            flex-shrink: 0;
            display: flex;
            align-items: center;
            gap: 6px;
            position: relative;
            z-index: 1;
        `;

        // 创建搜索框
        const searchInput = document.createElement('input');
        searchInput.type = 'text';
        searchInput.placeholder = '搜索节点...';
        searchInput.className = 'vx-outline-search-input';
        searchInput.style.cssText = `
            flex: 1;
            padding: 6px 10px;
            border: 1px solid #ddd;
            border-radius: 4px;
            font-size: 12px;
            outline: none;
            background: #fff;
            transition: border-color 0.2s ease;
            min-width: 0;
        `;

        // 创建清空按钮
        const clearButton = document.createElement('button');
        clearButton.className = 'vx-outline-clear-btn';
        clearButton.innerHTML = '✕';
        clearButton.title = '清空搜索';
        clearButton.style.cssText = `
            width: 24px;
            height: 24px;
            border: 1px solid #ddd;
            border-radius: 4px;
            background: #fff;
            color: #666;
            cursor: pointer;
            font-size: 14px;
            display: flex;
            align-items: center;
            justify-content: center;
            transition: all 0.2s ease;
            flex-shrink: 0;
            opacity: 0.5;
        `;

        // 搜索框事件
        searchInput.addEventListener('input', (e) => {
            this.searchTerm = e.target.value.toLowerCase().trim();
            this.updateOutlineWindow();
            clearButton.style.opacity = this.searchTerm ? '1' : '0.5';
        });

        searchInput.addEventListener('focus', () => {
            searchInput.style.borderColor = '#4a90e2';
        });

        searchInput.addEventListener('blur', () => {
            searchInput.style.borderColor = '#ddd';
        });

        // 清空按钮事件
        clearButton.addEventListener('click', () => {
            searchInput.value = '';
            this.searchTerm = '';
            this.updateOutlineWindow();
            clearButton.style.opacity = '0.5';
            searchInput.focus();
        });

        clearButton.addEventListener('mouseenter', () => {
            clearButton.style.backgroundColor = '#f0f0f0';
            clearButton.style.borderColor = '#999';
        });

        clearButton.addEventListener('mouseleave', () => {
            clearButton.style.backgroundColor = '#fff';
            clearButton.style.borderColor = '#ddd';
        });

        searchContainer.appendChild(searchInput);
        searchContainer.appendChild(clearButton);

        // 创建内容容器（用于折叠动画）
        const contentWrapper = document.createElement('div');
        contentWrapper.className = 'vx-outline-content-wrapper';
        contentWrapper.style.cssText = `
            flex: 1;
            display: flex;
            flex-direction: column;
            transition: all 0.1s;
            overflow: hidden;
            position: relative;
            z-index: 1;
            height: ${this.originalSize.height - this.titleBarHeight}px;
        `;

        // 创建内容区域
        const content = document.createElement('div');
        content.className = 'vx-outline-content';
        content.style.cssText = `
            padding: 8px;
            overflow-y: auto;
            flex: 1;
            font-size: 13px;
            line-height: 1.4;
            position: relative;
            z-index: 1;
        `;

        // 创建调整大小的手柄
        const resizeHandle = document.createElement('div');
        resizeHandle.className = 'vx-outline-resize-handle';
        resizeHandle.style.cssText = `
            position: absolute;
            bottom: 0;
            right: 0;
            width: 15px;
            height: 15px;
            cursor: nwse-resize;
            background: linear-gradient(135deg, transparent 50%, #ccc 50%);
            border-radius: 0 0 8px 0;
            z-index: 2;
            transition: opacity 0.3s ease;
        `;

        contentWrapper.appendChild(searchContainer);
        contentWrapper.appendChild(content);

        this.outlineWindow.appendChild(titleBar);
        this.outlineWindow.appendChild(contentWrapper);
        this.outlineWindow.appendChild(resizeHandle);

        // 添加到页面
        document.body.appendChild(this.outlineWindow);

        // 设置拖动功能
        this.setupWindowDrag(titleBar);
        // 设置调整大小功能
        this.setupWindowResize(resizeHandle);

        // 初始化变量
        this.outlineVisible = true;
        this.collapsedNodes = new Set();
        this.searchTerm = '';

        console.log('OutlineFeature: Outline window created with search functionality');
    }

    /**
     * 设置窗口拖拽功能
     * @param {HTMLElement} titleBar - 标题栏元素
     */
    setupWindowDrag(titleBar) {
        let isDragging = false;
        let startX, startY;
        let initialX, initialY;
        let lastValidX, lastValidY;
        let animationFrameId = null;

        const updatePosition = (e) => {
            if (!isDragging) return;

            const deltaX = e.clientX - startX;
            const deltaY = e.clientY - startY;

            // 计算新位置
            let newX = initialX + deltaX;
            let newY = initialY + deltaY;

            // 获取窗口尺寸
            const windowWidth = window.innerWidth;
            const windowHeight = window.innerHeight;
            const outlineWidth = this.outlineWindow.offsetWidth;
            const outlineHeight = this.outlineWindow.offsetHeight;

            // 限制在窗口内
            newX = Math.max(0, Math.min(newX, windowWidth - outlineWidth));
            newY = Math.max(0, Math.min(newY, windowHeight - outlineHeight));

            // 使用 transform 进行平滑移动
            this.outlineWindow.style.transform = `translate3d(${newX - initialX}px, ${newY - initialY}px, 0)`;

            // 记录有效位置
            lastValidX = newX;
            lastValidY = newY;
        };

        const handleMouseMove = (e) => {
            if (animationFrameId) {
                cancelAnimationFrame(animationFrameId);
            }
            animationFrameId = requestAnimationFrame(() => updatePosition(e));
        };

        const handleMouseUp = () => {
            if (!isDragging) return;
            isDragging = false;

            // 移除临时事件监听
            document.removeEventListener('mousemove', handleMouseMove);
            document.removeEventListener('mouseup', handleMouseUp);

            // 重置 transform 并设置实际位置
            this.outlineWindow.style.transform = 'none';
            if (lastValidX !== undefined && lastValidY !== undefined) {
                this.outlineWindow.style.left = lastValidX + 'px';
                this.outlineWindow.style.top = lastValidY + 'px';
                this.lastPosition = { left: lastValidX, top: lastValidY };
            }

            if (animationFrameId) {
                cancelAnimationFrame(animationFrameId);
            }
        };

        titleBar.addEventListener('mousedown', (e) => {
            if (e.target.classList.contains('vx-outline-collapse-btn')) return;
            isDragging = true;
            startX = e.clientX;
            startY = e.clientY;
            initialX = this.outlineWindow.offsetLeft;
            initialY = this.outlineWindow.offsetTop;
            lastValidX = initialX;
            lastValidY = initialY;

            // 添加临时事件监听
            document.addEventListener('mousemove', handleMouseMove);
            document.addEventListener('mouseup', handleMouseUp);
        });
    }

    /**
     * 设置窗口大小调整功能
     * @param {HTMLElement} handle - 调整大小的手柄元素
     */
    setupWindowResize(handle) {
        let startX, startY, startWidth, startHeight, startLeft, startTop;

        const handleMouseDown = (e) => {
            // 如果处于折叠状态，不允许调整大小
            if (this.isCollapsed) return;

            this.isResizing = true;
            startX = e.clientX;
            startY = e.clientY;
            startWidth = this.outlineWindow.offsetWidth;
            startHeight = this.outlineWindow.offsetHeight;
            startLeft = this.outlineWindow.offsetLeft;
            startTop = this.outlineWindow.offsetTop;

            document.addEventListener('mousemove', handleMouseMove);
            document.addEventListener('mouseup', handleMouseUp);
            e.preventDefault(); // 防止文本选择
        };

        const handleMouseMove = (e) => {
            if (!this.isResizing) return;

            // 计算新的尺寸
            let newWidth = Math.max(this.minimumSize.width, startWidth + (e.clientX - startX));
            let newHeight = Math.max(this.minimumSize.height, startHeight + (e.clientY - startY));

            // 限制最大尺寸
            const maxWidth = window.innerWidth - startLeft;
            const maxHeight = window.innerHeight - startTop;
            newWidth = Math.min(newWidth, maxWidth);
            newHeight = Math.min(newHeight, maxHeight);

            // 更新窗口大小
            this.outlineWindow.style.width = `${newWidth}px`;
            this.outlineWindow.style.height = `${newHeight}px`;

            // 更新内容区域高度
            const contentWrapper = this.outlineWindow.querySelector('.vx-outline-content-wrapper');
            if (contentWrapper) {
                contentWrapper.style.height = `${newHeight - this.titleBarHeight}px`;
            }

            // 保存新的尺寸
            this.lastSize = { width: newWidth, height: newHeight };

            // 请求动画帧以提高性能
            requestAnimationFrame(() => {
                // 触发内容更新
                this.updateOutlineWindow();
            });
        };

        const handleMouseUp = () => {
            this.isResizing = false;
            document.removeEventListener('mousemove', handleMouseMove);
            document.removeEventListener('mouseup', handleMouseUp);
        };

        handle.addEventListener('mousedown', handleMouseDown);
    }

    /**
     * 设置窗口大小监视器
     */
    setupResizeObserver() {
        const mindmapContainer = document.querySelector('.map-container');
        if (!mindmapContainer) return;

        const resizeObserver = new ResizeObserver(entries => {
            for (const entry of entries) {
                const { width, height } = entry.contentRect;
                const isSmall = width < this.COLLAPSE_THRESHOLD || height < this.COLLAPSE_THRESHOLD;
                
                if (isSmall) {
                    // 保存当前位置和大小（如果还没有保存）
                    if (!this.lastPosition) {
                        this.lastPosition = {
                            left: this.outlineWindow.offsetLeft,
                            top: this.outlineWindow.offsetTop
                        };
                        this.lastSize = {
                            width: this.outlineWindow.offsetWidth,
                            height: this.outlineWindow.offsetHeight
                        };
                    }

                    // 无论当前是否已经折叠，都确保窗口移动到左上角
                    this.outlineWindow.style.top = '80px';
                    this.outlineWindow.style.left = '20px';

                    // 如果还没有折叠，则进行折叠
                    if (!this.isCollapsed) {
                        this.toggleCollapse(true);
                    }
                } else {
                    // 只有在之前是由于窗口大小变化导致的折叠时，才自动展开和恢复位置
                    if (this.isCollapsed && this.lastPosition) {
                        this.toggleCollapse(false);
                        // 恢复到之前保存的位置
                        this.outlineWindow.style.left = this.lastPosition.left + 'px';
                        this.outlineWindow.style.top = this.lastPosition.top + 'px';
                    }
                }
            }
        });

        resizeObserver.observe(mindmapContainer);
    }

    /**
     * 切换大纲窗口的折叠状态
     * @param {boolean} [forceCollapse] - 是否强制折叠
     */
    toggleCollapse(forceCollapse) {
        const newState = forceCollapse !== undefined ? forceCollapse : !this.isCollapsed;
        this.isCollapsed = newState;

        const collapseBtn = this.outlineWindow.querySelector('.vx-outline-collapse-btn');
        const contentWrapper = this.outlineWindow.querySelector('.vx-outline-content-wrapper');
        const resizeHandle = this.outlineWindow.querySelector('.vx-outline-resize-handle');

        if (this.isCollapsed) {
            // 折叠状态 - 只保留标题栏
            contentWrapper.style.height = '0';
            contentWrapper.style.opacity = '0';
            resizeHandle.style.opacity = '0';
            resizeHandle.style.pointerEvents = 'none';
            collapseBtn.innerHTML = '▶';
            
            // 保存当前位置和大小（如果不是强制折叠）
            if (!forceCollapse) {
                this.lastPosition = {
                    left: this.outlineWindow.offsetLeft,
                    top: this.outlineWindow.offsetTop
                };
                this.lastSize = {
                    width: this.outlineWindow.offsetWidth,
                    height: this.outlineWindow.offsetHeight
                };
            }

            // 如果是由于窗口大小变化触发的折叠，则移动到左上角
            if (forceCollapse) {
                this.outlineWindow.style.top = '80px';
                this.outlineWindow.style.left = '20px';
            }
            
            this.outlineWindow.style.height = this.titleBarHeight + 'px';
        } else {
            // 展开状态
            const targetHeight = this.lastSize?.height || this.originalSize.height;
            const targetWidth = this.lastSize?.width || this.originalSize.width;
            
            this.outlineWindow.style.width = targetWidth + 'px';
            this.outlineWindow.style.height = targetHeight + 'px';
            contentWrapper.style.height = (targetHeight - this.titleBarHeight) + 'px';
            contentWrapper.style.opacity = '1';
            resizeHandle.style.opacity = '1';
            resizeHandle.style.pointerEvents = 'auto';
            collapseBtn.innerHTML = '◀';

            // 只在手动折叠后展开时才恢复到之前保存的位置
            if (this.lastPosition && !forceCollapse && !this.isWindowSmall()) {
                this.outlineWindow.style.left = this.lastPosition.left + 'px';
                this.outlineWindow.style.top = this.lastPosition.top + 'px';
            }
        }

        // 更新内容
        setTimeout(() => {
            this.updateOutlineWindow();
        }, 300);
    }

    /**
     * 检查窗口是否处于小尺寸状态
     * @returns {boolean} 如果窗口小于阈值返回 true
     */
    isWindowSmall() {
        const mindmapContainer = document.querySelector('.map-container');
        if (!mindmapContainer) return false;

        const { width, height } = mindmapContainer.getBoundingClientRect();
        return width < this.COLLAPSE_THRESHOLD || height < this.COLLAPSE_THRESHOLD;
    }

    /**
     * 更新大纲窗口内容
     * 步骤：
     * 1. 清空现有内容
     * 2. 获取根节点数据
     * 3. 递归渲染节点结构
     */
    updateOutlineWindow() {
        if (!this.outlineWindow) {
            console.warn('OutlineFeature: outlineWindow not found');
            return;
        }

        const content = this.outlineWindow.querySelector('.vx-outline-content');
        if (!content) {
            console.warn('OutlineFeature: content area not found');
            return;
        }

        try {
            // 获取MindElixir数据
            const allData = this.core.mindElixir && this.core.mindElixir.getAllData();
            
            if (allData && allData.nodeData) {
                content.innerHTML = '';
                this.renderOutlineNode(allData.nodeData, content, 0);
                console.log('OutlineFeature: Outline window updated successfully');
            } else {
                console.warn('OutlineFeature: No valid data found');
                content.innerHTML = '<div style="color: #666; text-align: center; padding: 20px;">暂无数据</div>';
            }
        } catch (error) {
            console.error('OutlineFeature: Error updating outline window:', error);
            content.innerHTML = '<div style="color: #e74c3c; text-align: center; padding: 20px;">数据加载失败</div>';
        }
    }

    // 检查节点是否匹配搜索条件
    nodeMatchesSearch(nodeData) {
        if (!this.searchTerm) return true;
        const topic = (nodeData.topic || '').toLowerCase();
        return topic.includes(this.searchTerm);
    }

    // 检查节点或其子节点是否匹配搜索条件
    nodeOrChildrenMatchSearch(nodeData) {
        if (this.nodeMatchesSearch(nodeData)) return true;
        
        if (nodeData.children && nodeData.children.length > 0) {
            return nodeData.children.some(child => this.nodeOrChildrenMatchSearch(child));
        }
        
        return false;
    }

    /**
     * 渲染大纲节点
     * 步骤：
     * 1. 检查搜索过滤
     * 2. 创建节点元素
     * 3. 添加展开/折叠控件和内容
     * 4. 递归渲染子节点
     * 
     * @param {object} nodeData - 节点数据
     * @param {HTMLElement} container - 容器元素
     * @param {number} level - 节点层级
     */
    renderOutlineNode(nodeData, container, level) {
        if (!nodeData) return;

        // 如果有搜索词，检查是否应该显示此节点
        if (this.searchTerm && !this.nodeOrChildrenMatchSearch(nodeData)) {
            return;
        }

        // 创建节点容器
        const nodeDiv = document.createElement('div');
        nodeDiv.className = 'vx-outline-node';
        nodeDiv.style.cssText = `
            margin-left: ${level * 16}px;
            margin-bottom: 2px;
        `;

        // 创建节点内容
        const nodeContent = document.createElement('div');
        nodeContent.className = 'vx-outline-node-content';
        nodeContent.dataset.nodeid = nodeData.id;
        nodeContent.style.cssText = `
            padding: 6px 8px;
            border-radius: 4px;
            cursor: pointer;
            display: flex;
            align-items: center;
            gap: 6px;
            transition: background-color 0.2s ease;
            word-break: break-word;
            border: 1px solid transparent;
        `;

        // 添加展开/折叠图标
        const hasChildren = nodeData.children && nodeData.children.length > 0;
        const isCollapsed = this.collapsedNodes.has(nodeData.id);
        
        let expandIcon = '';
        if (hasChildren) {
            expandIcon = isCollapsed ? '▶' : '▼';
        } else {
            expandIcon = '●';
        }

        const iconSpan = document.createElement('span');
        iconSpan.className = 'vx-outline-expand-icon';
        iconSpan.style.cssText = `
            font-size: 10px;
            color: #666;
            min-width: 12px;
            text-align: center;
            cursor: ${hasChildren ? 'pointer' : 'default'};
            padding: 2px;
            border-radius: 2px;
            transition: background-color 0.2s ease;
        `;
        iconSpan.textContent = expandIcon;

        // 折叠/展开功能
        if (hasChildren) {
            iconSpan.addEventListener('click', (e) => {
                e.stopPropagation();
                this.toggleNodeCollapse(nodeData.id);
            });
        }

        // 创建文本内容
        const textSpan = document.createElement('span');
        textSpan.style.cssText = `
            flex: 1;
            color: ${nodeData.root ? '#2c3e50' : '#34495e'};
            font-weight: ${nodeData.root ? 'bold' : 'normal'};
            font-size: ${nodeData.root ? '14px' : '13px'};
        `;

        // 高亮搜索匹配的文本
        const topic = nodeData.topic || '未命名节点';
        if (this.searchTerm && this.nodeMatchesSearch(nodeData)) {
            const regex = new RegExp(`(${this.searchTerm.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')})`, 'gi');
            const highlightedText = topic.replace(regex, '<mark style="background: #ffff00; color: #000;">$1</mark>');
            textSpan.innerHTML = highlightedText;
        } else {
            textSpan.textContent = topic;
        }

        nodeContent.appendChild(iconSpan);
        nodeContent.appendChild(textSpan);

        // 添加点击事件
        nodeContent.addEventListener('click', (e) => {
            if (e.target === iconSpan) return; // 避免与折叠图标冲突
            e.stopPropagation();
            
            this.highlightOutlineNode(nodeContent);
            this.locateNodeInMindMap(nodeData.id);
        });

        nodeDiv.appendChild(nodeContent);
        container.appendChild(nodeDiv);

        // 递归渲染子节点（如果未折叠且有子节点）
        if (hasChildren && !isCollapsed) {
            nodeData.children.forEach(child => {
                this.renderOutlineNode(child, container, level + 1);
            });
        }
    }

    /**
     * 切换节点的展开/折叠状态
     * @param {string} nodeId - 节点ID
     */
    toggleNodeCollapse(nodeId) {
        if (this.collapsedNodes.has(nodeId)) {
            this.collapsedNodes.delete(nodeId);
        } else {
            this.collapsedNodes.add(nodeId);
        }
        this.updateOutlineWindow();
    }

    /**
     * 高亮显示大纲节点
     * 步骤：
     * 1. 移除之前的高亮
     * 2. 添加新的高亮
     * 
     * @param {HTMLElement} nodeElement - 节点元素
     */
    highlightOutlineNode(nodeElement) {
        // 清除之前所有大纲节点的高亮
        const prevHighlighted = this.outlineWindow.querySelectorAll('.vx-outline-highlighted');
        prevHighlighted.forEach(el => {
            el.classList.remove('vx-outline-highlighted');
            el.style.backgroundColor = 'transparent';
            el.style.borderColor = 'transparent';
            el.style.boxShadow = '';
            el.style.transition = '';
        });

        // 添加新的高亮
        nodeElement.classList.add('vx-outline-highlighted');
        nodeElement.style.transition = 'all 0.3s ease';
        nodeElement.style.backgroundColor = '#e3f2fd'; // 浅蓝色高亮
        nodeElement.style.borderColor = '#1976d2';
        nodeElement.style.boxShadow = '0 2px 8px rgba(25, 118, 210, 0.3)';

        // 2秒后移除高亮
        setTimeout(() => {
            if (nodeElement.classList.contains('vx-outline-highlighted')) {
                nodeElement.style.transition = 'all 0.3s ease';
                nodeElement.classList.remove('vx-outline-highlighted');
                nodeElement.style.backgroundColor = 'transparent';
                nodeElement.style.borderColor = 'transparent';
                nodeElement.style.boxShadow = '';
                
                setTimeout(() => {
                    nodeElement.style.transition = '';
                }, 300);
            }
        }, 2000);
    }

    /**
     * 定位到思维导图中的节点
     * @param {string} nodeId - 节点ID
     */
    locateNodeInMindMap(nodeId) {
        if (!nodeId) return;
        
        if (this.isLocatingNode) {
            console.log('MindMapEditorCore: Skipping locate request - already locating node:', nodeId);
            return;
        }

        try {
            this.isLocatingNode = true;
            console.log('MindMapEditorCore: Attempting to locate node:', nodeId);
            
            // 查找目标节点元素（限制在脑图区域）
            const mindmapContainer = document.querySelector('.map-container');
            if (!mindmapContainer) {
                console.warn('MindMapEditorCore: Could not find .map-container');
                this.isLocatingNode = false;
                return;
            }
            
            const selectors = [
                `[data-nodeid="${nodeId}"]`,
                `[data-nodeid="me${nodeId}"]`
            ];
            
            let targetElement = null;
            for (const selector of selectors) {
                // 只在脑图容器中查找，避免选择到大纲窗口的节点
                targetElement = mindmapContainer.querySelector(selector);
                if (targetElement) {
                    console.log('MindMapEditorCore: Found target element in mindmap for node:', nodeId);
                    break;
                }
            }
            
            if (!targetElement) {
                console.warn('MindMapEditorCore: Could not find element in mindmap for node:', nodeId);
                this.isLocatingNode = false;
                return;
            }
            
            // 找到 MindElixir 的真实容器（map-container，有overflow:scroll的那个）
            const mapContainer = document.querySelector('.map-container');
            if (!mapContainer) {
                console.warn('MindMapEditorCore: Could not find .map-container');
                this.isLocatingNode = false;
                return;
            }
            
            // 获取节点在20000x20000画布中的绝对位置
            // 直接从style属性获取，因为MindElixir的节点都是绝对定位
            let nodeCanvasX, nodeCanvasY;
            
            // 尝试从父元素或节点本身获取位置信息
            let positionElement = targetElement;
            console.log('MindMapEditorCore: Target element tagName:', targetElement.tagName, 'style:', targetElement.style.cssText);
            
            while (positionElement && !positionElement.style.left) {
                positionElement = positionElement.parentElement;
                if (positionElement) {
                    console.log('MindMapEditorCore: Checking parent:', positionElement.tagName, 'style:', positionElement.style.cssText);
                }
                if (positionElement && positionElement.tagName.toLowerCase() === 'body') {
                    break;
                }
            }
            
            if (positionElement && positionElement.style.left && positionElement.style.top) {
                // 从style属性直接获取位置
                const styleLeft = parseFloat(positionElement.style.left);
                const styleTop = parseFloat(positionElement.style.top);
                nodeCanvasX = styleLeft + positionElement.offsetWidth / 2;
                nodeCanvasY = styleTop + positionElement.offsetHeight / 2;
                
                console.log('MindMapEditorCore: Using style positioning:', JSON.stringify({
                    element: positionElement.tagName,
                    styleLeft: styleLeft,
                    styleTop: styleTop,
                    offsetWidth: positionElement.offsetWidth,
                    offsetHeight: positionElement.offsetHeight,
                    calculatedCenter: { x: nodeCanvasX, y: nodeCanvasY }
                }));
            } else {
                // 回退方案：使用getBoundingClientRect计算
                const nodeRect = targetElement.getBoundingClientRect();
                const canvasRect = document.querySelector('.map-canvas').getBoundingClientRect();
                nodeCanvasX = nodeRect.left - canvasRect.left + nodeRect.width / 2 + mapContainer.scrollLeft;
                nodeCanvasY = nodeRect.top - canvasRect.top + nodeRect.height / 2 + mapContainer.scrollTop;
                
                console.log('MindMapEditorCore: Using fallback getBoundingClientRect positioning');
            }
            
            // MindElixir 居中算法：让容器滚动到 (节点位置 - 容器大小/2)
            const targetScrollX = nodeCanvasX - mapContainer.offsetWidth / 2;
            const targetScrollY = nodeCanvasY - mapContainer.offsetHeight / 2;
            
            console.log('MindMapEditorCore: MindElixir positioning calculation:', JSON.stringify({
                nodeInCanvas: { x: Math.round(nodeCanvasX), y: Math.round(nodeCanvasY) },
                containerSize: { width: mapContainer.offsetWidth, height: mapContainer.offsetHeight },
                targetScroll: { x: Math.round(targetScrollX), y: Math.round(targetScrollY) }
            }));
            
            // 使用 MindElixir 的容器执行滚动（关键！）
            mapContainer.scrollTo({
                left: targetScrollX,
                top: targetScrollY,
                behavior: 'smooth'
            });
            
            // 添加高亮效果，并在高亮后恢复原始背景色
            const originalBg = targetElement.style.backgroundColor;
            targetElement.style.backgroundColor = '#ffff00';
            setTimeout(() => {
                targetElement.style.backgroundColor = originalBg;
            }, 1000);
            
            console.log('MindMapEditorCore: Successfully scrolled MindElixir container to center node');
            
            // 滚动完成后重置状态
            setTimeout(() => {
                this.isLocatingNode = false;
                console.log('MindMapEditorCore: Node location completed for:', nodeId);
            }, 300);
            
        } catch (error) {
            console.error('MindMapEditorCore: Error in locateNodeInMindMap:', error);
            this.isLocatingNode = false;
        }
    }

    /**
     * 设置DOM观察器
     * 监听思维导图变化并更新大纲
     */
    setupDOMObserver() {
        const observer = new MutationObserver(() => {
            this.updateOutlineWindow();
        });

        observer.observe(document.getElementById('vx-mindmap'), {
            childList: true,
            subtree: true,
            characterData: true
        });
    }

    /**
     * 数据变更处理
     * 步骤：
     * 1. 构建节点数据映射
     * 2. 更新大纲显示
     * 
     * @param {object} data - 新的数据
     */
    onDataChange(data) {
        console.log('OutlineFeature: onDataChange called with data:', data);
        this.buildNodeDataMap(data);
        console.log('OutlineFeature: Built nodeDataMap with', this.nodeDataMap.size, 'entries');
        this.updateOutlineWindow();
    }

    /**
     * 构建节点数据映射表
     * @param {object} data - 思维导图数据
     */
    buildNodeDataMap(data) {
        this.nodeDataMap.clear();
        this.buildNodeDataMapRecursive(data, this.nodeDataMap);
    }

    /**
     * 递归构建节点数据映射表
     * @param {object} nodeData - 节点数据
     * @param {Map} map - 映射表
     */
    buildNodeDataMapRecursive(nodeData, map) {
        if (!nodeData) return;
        
        map.set(nodeData.id, nodeData);
        
        if (nodeData.children) {
            nodeData.children.forEach(child => {
                this.buildNodeDataMapRecursive(child, map);
            });
        }
    }
} 