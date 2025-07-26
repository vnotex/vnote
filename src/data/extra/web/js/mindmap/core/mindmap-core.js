/**
 * 思维导图核心类
 * 负责功能模块的管理和基础功能的实现
 */
class MindMapCore {
    constructor() {
        // 功能模块映射表
        this.features = new Map();
        // MindElixir 实例
        this.mindElixir = null;
        // 事件发射器
        this.eventEmitter = new EventEmitter();
        // 初始化标志
        this.initialized = false;
        // MutationObserver 实例
        this.observer = null;
    }

    /**
     * 初始化
     * 步骤：
     * 1. 初始化思维导图实例
     * 2. 设置功能模块
     * 3. 初始化各功能模块
     */
    init() {
        console.log('MindMapCore: init called');

        // 初始化思维导图实例
        console.log('MindMapCore: About to init MindElixir');
        this.initMindElixir();

        // 设置和初始化功能模块
        console.log('MindMapCore: About to setup features');
        this.setupFeatures();
        console.log('MindMapCore: About to init features');
        this.initFeatures();

        // 监听内容变更事件
        this.on('contentChanged', () => {
            console.log('MindMapCore: Content changed, triggering auto-save');
            // 自动保存统一使用ID 'auto_save'，在saveData中会被转换成0
            this.saveData('auto_save');
        });

        // 添加键盘快捷键监听
        this.setupKeyboardShortcuts();

        // 设置初始化标志并触发ready事件
        this.initialized = true;
        console.log('MindMapCore: Emitting ready event');
        this.emit('ready');
    }

    /**
     * 事件监听
     * @param {string} event - 事件名称
     * @param {function} callback - 回调函数
     */
    on(event, callback) {
        this.eventEmitter.on(event, callback);
    }

    /**
     * 触发事件
     * @param {string} event - 事件名称
     * @param {...any} args - 事件参数
     */
    emit(event, ...args) {
        this.eventEmitter.emit(event, ...args);
    }



    /**
     * 初始化思维导图实例
     */
    initMindElixir() {
        // 确保 MindElixir 已加载
        if (typeof MindElixir === 'undefined') {
            console.error('MindElixir library not loaded');
            return;
        }

        // 创建思维导图实例
        this.mindElixir = new MindElixir({
            el: '#vx-mindmap',
            direction: 2,
            draggable: true,
            contextMenu: true,
            toolBar: true,
            nodeMenu: true,
            keypress: true,
            allowUndo: true,
            theme: {
                primary: 'var(--vx-mindmap-primary-color)',
                box: 'var(--vx-mindmap-box-color)',
                line: 'var(--vx-mindmap-line-color)',
                root: {
                    color: 'var(--vx-mindmap-root-color)',
                    background: 'var(--vx-mindmap-root-background)',
                    fontSize: '16px',
                    borderRadius: '4px',
                    padding: '8px 16px'
                },
                child: {
                    color: 'var(--vx-mindmap-child-color)',
                    background: 'var(--vx-mindmap-child-background)',
                    fontSize: '14px',
                    borderRadius: '4px',
                    padding: '6px 12px'
                }
            },
            before: {
                insertSibling: () => true,
                async addChild() { return true; }
            }
        });

        // 等待MindElixir实例初始化完成
        const waitForInit = () => {
            if (this.mindElixir && typeof this.mindElixir.getData === 'function') {
                this.setupMindElixirEvents();
            } else {
                setTimeout(waitForInit, 100);
            }
        };
        waitForInit();

        // 使用MutationObserver监听DOM变化，确保链接在所有操作后都能重新渲染
        this.setupMutationObserver();

        console.log('MindMapCore: MindElixir instance created');
    }

    /**
     * 设置MindElixir事件监听器
     */
    setupMindElixirEvents() {
        console.log('MindMapCore: Setting up MindElixir events');

        // 监听操作事件，这些事件包括节点的添加、删除、移动和编辑
        this.mindElixir.bus.addListener('operation', (name, obj) => {
            console.log('MindMapCore: MindElixir operation event received. Name:', name, 'Object:', obj);

            // 针对Hyperlink的编辑，进行一次即时的、有针对性的重绘
            if (name === 'editHyperLink' && obj) {
                // 使用微任务或短延迟确保在MindElixir的DOM操作后执行
                setTimeout(() => {
                    const linkHandler = this.getFeature('linkHandler');
                    const domNode = document.querySelector(`tpc[data-nodeid=me${obj.id}]`);
                    if (linkHandler && domNode) {
                        console.log('MindMapCore: Directly processing node after hyperlink edit:', obj.id);
                        linkHandler.processNodeWithData(domNode, linkHandler.nodeDataMap);
                    } else {
                        console.warn('MindMapCore: Could not find linkHandler or domNode for hyperlink edit.');
                    }
                }, 50);
                // 此次操作已精确处理，无需触发全局重绘
                return;
            }
            
            // 对其他所有操作使用防抖处理，避免频繁的全局更新
            if (this._processNodesTimeout) {
                clearTimeout(this._processNodesTimeout);
            }
            
            this._processNodesTimeout = setTimeout(() => {
                this.processNodesAndRelayout();
            }, 100);
        });

        // 监听展开/折叠事件
        // MindElixir的expandNode事件同时处理展开和折叠
        this.mindElixir.bus.addListener('expandNode', () => {
            console.log('MindMapCore: Node expanded/collapsed');
            // 添加一个短暂的延迟，以确保DOM更新稳定后再进行处理
            setTimeout(() => {
                this.processNodesAndRelayout();
            }, 50);
        });

        console.log('MindMapCore: MindElixir events setup complete');
    }

    /**
     * 设置MutationObserver来监听DOM变化
     * 这是一种更可靠的方式来捕捉所有由MindElixir引起的UI更新
     */
    setupMutationObserver() {
        if (!this.mindElixir || !this.mindElixir.box) {
            console.error('MindMapCore: Cannot setup MutationObserver, mindElixir.box is not available.');
            return;
        }

        this.observer = new MutationObserver((mutations) => {
            // 使用防抖避免过于频繁的调用
            if (this._mutationTimeout) {
                clearTimeout(this._mutationTimeout);
            }
            this._mutationTimeout = setTimeout(() => {
                console.log('MindMapCore: DOM changed, processing nodes due to mutation.');
                this.processNodesAndRelayout();
            }, 150);
        });

        this.observer.observe(this.mindElixir.box, {
            childList: true, // 监听子节点的添加或删除
            subtree: true,   // 监听所有后代节点
        });

        console.log('MindMapCore: MutationObserver setup complete, watching for changes.');
    }

    /**
     * 禁用MutationObserver
     */
    disableObserver() {
        if (this.observer) {
            this.observer.disconnect();
            // console.log('MindMapCore: MutationObserver disabled.');
        }
    }

    /**
     * 启用MutationObserver
     */
    enableObserver() {
        if (this.observer) {
            this.observer.observe(this.mindElixir.box, {
                childList: true,
                subtree: true,
            });
            // console.log('MindMapCore: MutationObserver enabled.');
        }
    }

    /**
     * 处理节点并强制重新布局
     * 确保在添加自定义元素（如链接图标）后，思维导图的布局能够更新
     */
    processNodesAndRelayout() {
        if (!this.mindElixir || typeof this.mindElixir.getAllData !== 'function') {
            console.warn('MindMapCore: MindElixir not ready, skipping node processing.');
            return;
        }

        const linkHandler = this.getFeature('linkHandler');
        if (!linkHandler) {
            console.warn('MindMapCore: LinkHandler feature not available');
            return;
        }

        try {
            // 1. 触发linkHandler处理所有节点，添加自定义图标
            linkHandler.processAllNodes();

            // 2. 强制MindElixir重新计算布局和连线
            //    这是解决布局错乱的关键
            if (this.mindElixir && typeof this.mindElixir.linkDiv === 'function') {
                console.log('MindMapCore: Forcing re-layout after node processing.');
                this.mindElixir.linkDiv();
            }

            // 3. 触发内容变更事件，以启动自动保存
            this.emit('contentChanged');

        } catch (error) {
            console.error('MindMapCore: Error processing nodes and re-layouting:', error);
        }
    }

    /**
     * 设置功能模块
     * 在此方法中注册所需的功能模块
     * 子类应该重写此方法来注册具体的功能模块
     */
    setupFeatures() {
        // 子类应该重写此方法
        console.log('MindMapCore: setupFeatures called - should be overridden by subclass');
    }

    /**
     * 初始化所有功能模块
     * 步骤：
     * 1. 遍历所有已注册的功能模块
     * 2. 调用每个模块的init方法进行初始化
     */
    initFeatures() {
        console.log('MindMapCore: initFeatures called, features count:', this.features.size);
        for (const [name, feature] of this.features.entries()) {
            console.log('MindMapCore: Initializing feature:', name);
            if (typeof feature.init === 'function') {
                feature.init();
                console.log('MindMapCore: Feature', name, 'initialized');
            } else {
                console.warn('MindMapCore: Feature', name, 'has no init method');
            }
        }
    }

    /**
     * 注册功能模块
     * 步骤：
     * 1. 将功能模块实例保存到映射表中
     * 2. 注入核心实例到功能模块中
     * 
     * @param {string} name - 功能模块名称
     * @param {object} feature - 功能模块实例
     */
    registerFeature(name, feature) {
        this.features.set(name, feature);
        // 注入核心实例到功能模块
        if (typeof feature.setCore === 'function') {
            feature.setCore(this);
        }
    }

    /**
     * 获取功能模块实例
     * @param {string} name - 功能模块名称
     * @returns {object} 功能模块实例
     */
    getFeature(name) {
        return this.features.get(name);
    }

    /**
     * 设置思维导图数据
     * 步骤：
     * 1. 验证数据有效性
     * 2. 保存数据
     * 3. 更新思维导图显示
     * 4. 通知所有功能模块数据变更
     * 
     * @param {object} p_data - 思维导图数据
     */
    setData(p_data) {
        console.log('MindMapCore: setData called with:', p_data);

        let data;
        try {
            // 解析数据或使用默认数据
            if (p_data && p_data !== "") {
                // 检查p_data是否已经是对象
                if (typeof p_data === 'object') {
                    data = p_data;
                } else {
                    data = JSON.parse(p_data);
                }
                console.log('MindMapCore: Using data:', data);
            } else {
                data = MindElixir.new('New Topic');
                console.log('MindMapCore: Using default data');
            }

            // 检查数据格式
            if (!data.nodeData) {
                console.error('MindMapCore: Invalid data format - missing nodeData');
                data = MindElixir.new('New Topic');
            }

            // 保存数据供功能模块使用
            this.data = data;

            // 初始化思维导图
            console.log('MindMapCore: Initializing MindElixir with data');
            this.mindElixir.init(data);

            // 通知所有功能模块数据变更
            console.log('MindMapCore: Notifying features of data change');
            for (const feature of this.features.values()) {
                if (typeof feature.onDataChange === 'function') {
                    feature.onDataChange(data);
                }
            }

            // 等待MindElixir渲染完成后处理节点，确保链接标签正确显示
            this.processNodesAndRelayout();
            
            // 触发渲染完成事件
            console.log('MindMapCore: Emitting rendered event');
            this.emit('rendered');

        } catch (error) {
            console.error('MindMapCore: Error in setData:', error);
            // 如果解析失败，使用默认数据
            data = MindElixir.new('New Topic');
            this.mindElixir.init(data);
        }
    }

    /**
     * 设置键盘快捷键
     * 监听保存快捷键 (Ctrl+S / Cmd+S)
     */
    setupKeyboardShortcuts() {
        document.addEventListener('keydown', (event) => {
            // 检查是否是保存快捷键
            const isSaveShortcut = (event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 's';
            
            if (isSaveShortcut) {
                event.preventDefault(); // 阻止浏览器默认的保存行为
                console.log('MindMapCore: Save shortcut detected, notifying contents changed.');
                
                // 标准做法：只通知后端内容已变更，由后端处理后续保存逻辑
                if (window.vxAdapter?.notifyContentsChanged) {
                    window.vxAdapter.notifyContentsChanged();
                }
            }
        });
        
        console.log('MindMapCore: Keyboard shortcuts setup complete');
    }

    /**
     * 保存思维导图数据
     * @param {number|string} p_id - 数据ID
     */
    saveData(p_id) {
        console.log('MindMapCore: saveData called with id:', p_id);
        
        if (!this.mindElixir) {
            const error = 'Cannot save - mindElixir instance is null';
            console.error('MindMapCore:', error);
            this.emitSaveResult(p_id, false, error);
            return;
        }

        try {
            console.log('MindMapCore: Getting all data from mindElixir');
            const allData = this.mindElixir.getAllData();
            
            // 验证数据有效性
            if (!allData || !allData.nodeData) {
                const error = 'Invalid mind map data structure';
                console.error('MindMapCore:', error);
                this.emitSaveResult(p_id, false, error);
                return;
            }

            // 准备要保存的数据
            const dataToSave = JSON.stringify(allData);

            if (window.vxAdapter?.setSavedData) {
                // 将内部使用的 'auto_save' ID 转换为后端能理解的 0
                const saveId = p_id === 'auto_save' ? 0 : p_id;
                window.vxAdapter.setSavedData(saveId, dataToSave);
                this.emitSaveResult(saveId, true, '', dataToSave);
            } else {
                const error = 'vxAdapter.setSavedData is not available';
                console.error('MindMapCore:', error);
                this.emitSaveResult(p_id, false, error);
            }
        } catch (error) {
            console.error('MindMapCore: Error in save process:', error);
            this.emitSaveResult(p_id, false, error.message);
        }
    }

    /**
     * 发送保存结果事件
     * @param {number|string} id - 保存ID
     * @param {boolean} success - 是否成功
     * @param {string} [error] - 错误信息
     * @param {string} [data] - 保存的数据
     */
    emitSaveResult(id, success, error = '', data = '') {
        const result = {
            id: id,
            success: success,
            error: error,
            timestamp: Date.now(),
            data: data
        };

        this.emit('saveCompleted', result);
    }
} 