/**
 * 思维导图编辑器主入口文件
 * 负责初始化和管理思维导图功能
 */

/**
 * 思维导图编辑器主类
 * 负责与Qt后端对接和功能模块的管理
 * 继承自VXCore以获取基础功能
 */
class MindMapEditor extends VXCore {
    /**
     * 构造函数
     * 步骤：
     * 1. 调用父类构造函数
     * 2. 初始化MindMapCore实例
     */
    constructor() {
        super();
        // MindMapCore实例
        this.mindMapCore = null;
        // 初始化标志
        this.initialized = false;
    }

    /**
     * 初始化加载
     * 步骤：
     * 1. 调用父类初始化
     * 2. 初始化MindMapCore
     * 3. 设置事件监听
     */
    initOnLoad() {
        console.log('MindMapEditor: initOnLoad called');
        
        // 确保父类初始化完成
        super.initOnLoad();

        // 创建MindMapCore实例
        console.log('MindMapEditor: Creating MindMapCore instance');
        this.mindMapCore = new MindMapCore();

        // 设置功能模块
        console.log('MindMapEditor: Setting up features');
        this.setupFeatures();

        // 设置事件监听
        console.log('MindMapEditor: Setting up event listeners');
        this.setupEventListeners();

        // 初始化MindMapCore
        console.log('MindMapEditor: Initializing MindMapCore');
        this.mindMapCore.init();

        // 设置初始化标志
        this.initialized = true;
        console.log('MindMapEditor: Initialization complete');
    }

    /**
     * 设置功能模块
     * 步骤：
     * 1. 注册大纲功能模块
     * 2. 注册链接处理模块
     */
    setupFeatures() {
        console.log('MindMapEditor: setupFeatures called');
        // 注册功能模块
        this.mindMapCore.registerFeature('outline', new OutlineFeature());
        this.mindMapCore.registerFeature('linkHandler', new LinkHandlerFeature());
        console.log('MindMapEditor: Features registered:', this.mindMapCore.features.size);
    }

    /**
     * 生成数字ID
     * @returns {number} 时间戳的数字形式
     */
    generateNumericId() {
        return parseInt(Date.now().toString().slice(-8), 10);
    }

    /**
     * 设置事件监听
     */
    setupEventListeners() {
        // 监听MindMapCore的ready事件
        this.mindMapCore.on('ready', () => {
            if (window.vxAdapter) {
                window.vxAdapter.setReady(true);

                // 监听保存请求
                if (typeof window.vxAdapter.saveDataRequested === 'function') {
                    window.vxAdapter.saveDataRequested.connect((id) => {
                        this.saveData(id);
                    });
                }
            }
        });

        // 监听内容变更事件
        this.mindMapCore.on('contentChanged', () => {
            if (window.vxAdapter?.notifyContentsChanged) {
                window.vxAdapter.notifyContentsChanged();
            }
        });

        // 监听保存完成事件
        this.mindMapCore.on('saveCompleted', (result) => {
            // 只有手动保存（ID>0）成功时才显示消息，或在任何保存失败时显示消息
            if (window.vxAdapter?.showMessage) {
                if (result.success) {
                    if (typeof result.id === 'number' && result.id > 0) {
                        window.vxAdapter.showMessage('保存成功');
                    }
                } else {
                    window.vxAdapter.showMessage('保存失败: ' + (result.error || '未知错误'));
                }
            }
        });
    }

    /**
     * 设置思维导图数据
     * @param {object} data - 思维导图数据
     */
    setData(data) {
        // console.log('MindMapEditor: setData called with data:', data);
        if (this.mindMapCore) {
            this.mindMapCore.setData(data);
        }
    }

    /**
     * 保存思维导图数据
     * @param {number} id - 数据ID
     */
    saveData(id) {
        // 验证必要条件
        // const checks = [
        //     { condition: !this.mindMapCore, message: 'mindMapCore is null' },
        //     { condition: !this.initialized, message: 'editor not initialized' },
        //     { condition: !window.vxAdapter?.setSavedData, message: 'setSavedData function not available' },
        //     { condition: typeof id !== 'number' || isNaN(id), message: 'invalid save ID' }
        // ];

        // for (const check of checks) {
        //     if (check.condition) {
        //         const error = 'Cannot save - ' + check.message;
        //         console.error('MindMapEditor:', error);
        //         this.mindMapCore.emitSaveResult(id, false, error);
        //         return;
        //     }
        // }

        // try {
        //     this.mindMapCore.saveData(id);
        // } catch (error) {
        //     console.error('MindMapEditor: Error during save operation:', error);
        //     this.mindMapCore.emitSaveResult(id, false, '保存时发生错误: ' + error.message);
        // }

        if (this.mindMapCore) {
            this.mindMapCore.saveData(id);
        }
    }

    /**
     * 获取功能模块
     * @param {string} name - 功能模块名称
     * @returns {object} 功能模块实例
     */
    getFeature(name) {
        return this.mindMapCore ? this.mindMapCore.getFeature(name) : null;
    }
}

// 等待 DOM 加载完成后初始化
document.addEventListener('DOMContentLoaded', () => {
    // 确保所有依赖都已加载
    if (typeof VXCore === 'undefined') {
        console.error('VXCore not loaded');
        return;
    }
    
    if (typeof MindMapCore === 'undefined') {
        console.error('MindMapCore not loaded');
        return;
    }
    
    if (typeof OutlineFeature === 'undefined') {
        console.error('OutlineFeature not loaded');
        return;
    }
    
    if (typeof LinkHandlerFeature === 'undefined') {
        console.error('LinkHandlerFeature not loaded');
        return;
    }

    // 创建全局实例
    window.mindMapEditor = new MindMapEditor();

    // 设置Qt后端对接
    new QWebChannel(qt.webChannelTransport, function(p_channel) {
        let adapter = p_channel.objects.vxAdapter;
        // Export the adapter globally.
        window.vxAdapter = adapter;

        // Connect signals from CPP side.
        adapter.saveDataRequested.connect(function(p_id) {
            window.mindMapEditor.saveData(p_id);
        });

        adapter.dataUpdated.connect(function(p_data) {
            window.mindMapEditor.setData(p_data);
        });

        // 添加URL点击处理函数到adapter对象
        adapter.handleUrlClick = function(url) {
            console.log('MindMapEditor: handleUrlClick called with URL:', url);
            try {
                if (typeof adapter.urlClicked === 'function') {
                    console.log('MindMapEditor: Calling adapter.urlClicked');
                    adapter.urlClicked(url);
                } else {
                    console.error('MindMapEditor: adapter.urlClicked is not a function');
                    console.log('MindMapEditor: Available adapter methods:', Object.getOwnPropertyNames(adapter));
                }
            } catch (error) {
                console.error('MindMapEditor: Error in handleUrlClick:', error);
            }
        };

        // 添加带方向的URL点击处理函数
        adapter.handleUrlClickWithDirection = function(url, direction) {
            console.log('MindMapEditor: handleUrlClickWithDirection called with URL:', url, 'Direction:', direction);
            try {
                if (typeof adapter.urlClickedWithDirection === 'function') {
                    console.log('MindMapEditor: Calling adapter.urlClickedWithDirection');
                    adapter.urlClickedWithDirection(url, direction);
                } else {
                    console.error('MindMapEditor: adapter.urlClickedWithDirection is not a function');
                }
            } catch (error) {
                console.error('MindMapEditor: Error in handleUrlClickWithDirection:', error);
            }
        };

        console.log('MindMapEditor: QWebChannel has been set up successfully');
        console.log('MindMapEditor: Adapter methods available:', Object.getOwnPropertyNames(adapter));

        // 检查window.load是否已经触发
        if (document.readyState === 'complete') {
            console.log('MindMapEditor: Window already loaded, calling initOnLoad manually');
            window.mindMapEditor.initOnLoad();
        } else {
            console.log('MindMapEditor: Window not yet loaded, VXCore will handle initOnLoad');
        }
    });
});

// 添加全局大纲窗口控制函数
window.showOutline = function() {
    if (window.mindMapEditor) {
        const outlineFeature = window.mindMapEditor.getFeature('outline');
        if (outlineFeature) {
            outlineFeature.showOutlineWindow();
        }
    }
}; 