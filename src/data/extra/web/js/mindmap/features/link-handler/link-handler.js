/**
 * 思维导图链接处理功能模块
 * 提供节点链接的可视化和交互功能
 */
class LinkHandlerFeature {
    constructor() {
        this.core = null;
        this.nodeDataMap = new Map();
    }

    /**
     * 设置核心实例引用
     * @param {MindMapCore} core - 核心实例
     */
    setCore(core) {
        this.core = core;
    }

    /**
     * 初始化链接处理功能
     */
    init() {
        console.log('LinkHandlerFeature: init called');
        this.setupLinkTagClickListener();
        console.log('LinkHandlerFeature: initialization complete');
    }

    /**
     * 处理节点数据添加 link 增强功能
     * 步骤：
     * 1. 验证节点数据
     * 2. 检查是否存在超链接
     * 3. 添加链接标签
     * 
     * @param {HTMLElement} domNode - DOM节点元素
     * @param {object} nodeDataMapOrNodeData - 节点数据映射或单个节点数据
     */
    processNodeWithData(domNode, nodeDataMapOrNodeData) {
        if (!domNode) {
            console.warn('LinkHandlerFeature: No DOM node provided');
            return;
        }

        let nodeData = null;
        let nodeId = null;

        // 检查第二个参数是Map还是单个nodeData对象
        if (nodeDataMapOrNodeData instanceof Map) {
            // 如果是Map，需要通过domNode查找对应的nodeData
            const nodeDataMap = nodeDataMapOrNodeData;
            
            // 通过data-nodeid属性获取节点ID
            if (domNode.hasAttribute('data-nodeid')) {
                nodeId = domNode.getAttribute('data-nodeid');
                // console.log('LinkHandlerFeature: Processing node with ID:', nodeId);
                
                // 处理MindElixir的ID前缀（DOM中可能有"me"前缀，但nodeData中没有）
                let cleanNodeId = nodeId;
                if (nodeId.startsWith('me')) {
                    cleanNodeId = nodeId.substring(2); // 移除"me"前缀
                    // console.log('LinkHandlerFeature: Cleaned node ID:', cleanNodeId);
                }
                
                // 首先尝试用原始ID匹配
                nodeData = nodeDataMap.get(nodeId);
                
                // 如果失败，尝试用清理后的ID匹配
                if (!nodeData) {
                    nodeData = nodeDataMap.get(cleanNodeId);
                }

                // if (nodeData) {
                //     console.log('LinkHandlerFeature: Found node data:', {
                //         id: nodeData.id,
                //         topic: nodeData.topic,
                //         hyperLink: nodeData.hyperLink
                //     });
                // } else {
                //     console.warn('LinkHandlerFeature: No node data found for ID:', nodeId);
                // }
            }
        } else {
            // 如果是单个nodeData对象
            nodeData = nodeDataMapOrNodeData;
            nodeId = nodeData ? nodeData.id : null;
        }

        // 移除MindElixir默认生成的超链接元素，避免重叠
        const defaultLink = domNode.querySelector('a.hyper-link');
        if (defaultLink) {
            defaultLink.remove();
        }

        // 如果没有找到nodeData或没有hyperLink，移除可能存在的旧标签并返回
        if (!nodeData || !nodeData.hyperLink) {
            const existingContainer = domNode.querySelector('.vx-link-container');
            if (existingContainer) {
                existingContainer.remove();
            }
            return;
        }

        // 查找或创建链接容器
        let textContainer = this.findTextContainer(domNode);
        if (!textContainer) {
            console.warn('LinkHandlerFeature: Could not find text container for node:', nodeId);
            return;
        }

        // 检查是否已存在链接标签
        let existingContainer = textContainer.querySelector('.vx-link-container');
        if (existingContainer) {
            existingContainer.remove();
        }

        // 提取文件扩展名
        const extension = this.extractFileExtension(nodeData.hyperLink);
        if (!extension) {
            console.warn('LinkHandlerFeature: Could not extract extension from:', nodeData.hyperLink);
            return;
        }

        // console.log('LinkHandlerFeature: Creating link tag for node:', {
        //     nodeId: nodeId,
        //     extension: extension,
        //     hyperLink: nodeData.hyperLink
        // });

        // 获取样式配置
        const style = this.getLinkTagStyle(extension);

        // 创建链接标签容器
        const linkContainer = document.createElement('span');
        linkContainer.className = 'vx-link-container';
        linkContainer.style.cssText = `
            display: inline-flex;
            align-items: center;
            margin-left: 4px;
            vertical-align: baseline;
            flex-shrink: 0;
            position: relative;
            z-index: 1;
        `;

        // 创建链接标签
        const linkTag = document.createElement('span');
        linkTag.className = 'vx-link-tag';
        linkTag.textContent = `[${extension}]`;
        linkTag.dataset.url = nodeData.hyperLink;
        linkTag.dataset.nodeid = nodeId;
        linkTag.title = `点击打开: ${nodeData.hyperLink}\n拖拽到不同方向可以控制打开位置\n↑上方 ↓下方 ←左侧 →右侧（默认）`;
        linkTag.style.cssText = `
            background: ${style.backgroundColor};
            color: ${style.textColor};
            padding: 2px 4px;
            border-radius: 3px;
            font-size: 10px;
            font-weight: bold;
            cursor: pointer;
            user-select: none;
            border: 1px solid ${style.borderColor};
            display: inline-flex;
            align-items: center;
            line-height: 1;
            min-width: 16px;
            text-align: center;
            transition: all 0.2s ease;
            font-family: monospace;
            box-shadow: 0 1px 2px rgba(0,0,0,0.1);
            white-space: nowrap;
        `;

        // 将链接标签添加到容器中
        linkContainer.appendChild(linkTag);

        // 确保文本容器使用正确的布局
        textContainer.style.display = 'inline-flex';
        textContainer.style.alignItems = 'center';
        textContainer.style.flexWrap = 'nowrap';
        textContainer.style.gap = '4px';
        textContainer.style.width = 'auto';
        textContainer.style.position = 'relative';

        // 添加链接标签到文本容器
        textContainer.appendChild(linkContainer);

        // 设置拖拽事件处理
        this.setupDragEvents(linkTag);

        // 确保父节点计算正确的宽度
        const parentNode = domNode.closest('.map-node');
        if (parentNode) {
            parentNode.style.width = 'auto';
            parentNode.style.minWidth = 'fit-content';
        }

        // console.log('LinkHandlerFeature: Link tag added successfully for node:', nodeId);
    }

    /**
     * 设置拖拽事件处理
     * @param {HTMLElement} linkTag - 链接标签元素
     */
    setupDragEvents(linkTag) {
        // 拖拽状态变量
        let isDragging = false;
        let startX = 0;
        let startY = 0;
        let dragThreshold = 15; // 拖拽阈值（像素）

        // 添加hover效果
        linkTag.addEventListener('mouseenter', () => {
            if (!isDragging) {
                linkTag.style.transform = 'scale(1.05)';
                linkTag.style.boxShadow = '0 3px 6px rgba(0,0,0,0.2)';
            }
        });
        
        linkTag.addEventListener('mouseleave', () => {
            if (!isDragging) {
                linkTag.style.transform = 'scale(1)';
                linkTag.style.boxShadow = '0 2px 4px rgba(0,0,0,0.1)';
            }
        });

        // 鼠标按下事件 - 开始拖拽检测
        linkTag.addEventListener('mousedown', (event) => {
            event.preventDefault();
            event.stopPropagation();
            
            isDragging = false;
            startX = event.clientX;
            startY = event.clientY;
            
            // 添加拖拽样式
            linkTag.style.cursor = 'grabbing';
            linkTag.style.transform = 'scale(1.1)';
            linkTag.style.boxShadow = '0 4px 12px rgba(0,0,0,0.3)';
            linkTag.style.transition = 'none';
            
            // 显示拖拽指示器（初始状态）
            this.showDragIndicator(startX, startY, 0, 0, 'Right');

            // 添加文档级别的事件监听器
            document.addEventListener('mousemove', handleMouseMove);
            document.addEventListener('mouseup', handleMouseUp);
        });

        // 鼠标移动事件 - 检测拖拽方向
        const handleMouseMove = (event) => {
            if (event.buttons !== 1) return; // 确保鼠标左键按下
            
            const deltaX = event.clientX - startX;
            const deltaY = event.clientY - startY;
            const distance = Math.sqrt(deltaX * deltaX + deltaY * deltaY);
            
            if (distance > dragThreshold) {
                isDragging = true;
            }
            
            // 如果开始拖拽，更新指示器和方向线
            if (distance > 5) { // 更低的阈值，更敏感的响应
                this.updateDragIndicator(startX, startY, deltaX, deltaY);
            }
        };

        // 鼠标释放事件 - 处理点击或拖拽
        const handleMouseUp = (event) => {
            event.preventDefault();
            event.stopPropagation();
            
            // 移除事件监听器
            document.removeEventListener('mousemove', handleMouseMove);
            document.removeEventListener('mouseup', handleMouseUp);
            
            // 恢复样式
            linkTag.style.cursor = 'pointer';
            linkTag.style.transform = 'scale(1)';
            linkTag.style.boxShadow = '0 2px 4px rgba(0,0,0,0.1)';
            linkTag.style.transition = 'all 0.2s ease';
            
            // 移除拖拽指示器
            this.hideDragIndicator();
            
            if (isDragging) {
                // 计算拖拽方向
                const deltaX = event.clientX - startX;
                const deltaY = event.clientY - startY;
                const direction = this.calculateDragDirection(deltaX, deltaY);
                
                // 发送带方向的URL点击事件
                this.handleUrlClickWithDirection(linkTag.dataset.url, direction);
            } else {
                // 普通点击 - 默认右边
                this.handleUrlClickWithDirection(linkTag.dataset.url, 'Right');
            }
            
            isDragging = false;
        };
    }

    // 提取节点文本
    extractNodeText(domNode) {
        let text = '';
        
        // 尝试多种方式获取文本
        const textElement = domNode.querySelector('tpc') || 
                           domNode.querySelector('.topic') ||
                           domNode;
        
        if (textElement) {
            // 排除已有的链接标签
            const cloned = textElement.cloneNode(true);
            const linkContainers = cloned.querySelectorAll('.vx-link-container');
            linkContainers.forEach(container => container.remove());
            text = cloned.textContent || cloned.innerText || '';
        }
        
        return text.trim();
    }

    /**
     * 查找节点的文本容器
     * 步骤：
     * 1. 查找内容元素
     * 2. 查找或使用文本容器
     * 
     * @param {HTMLElement} nodeElement - 节点元素
     * @returns {HTMLElement} 文本容器元素
     */
    findTextContainer(nodeElement) {
        const selectors = ['tpc', '.topic', '.node-topic', '.mind-elixir-topic'];
        
        for (const selector of selectors) {
            const container = nodeElement.querySelector(selector);
            if (container) {
                return container;
            }
        }
        
        // 如果找不到特定容器，返回节点本身
        return nodeElement;
    }

    /**
     * 提取文件扩展名或URL类型
     * @param {string} hyperLink - 超链接URL
     * @returns {string} 文件扩展名或URL类型
     */
    extractFileExtension(hyperLink) {
        if (!hyperLink) {
            return 'link';
        }

        // HTTP/HTTPS URLs
        if (hyperLink.startsWith('https://')) {
            return 'https';
        }
        if (hyperLink.startsWith('http://')) {
            return 'http';
        }

        // 文件路径 - 提取扩展名
        const match = hyperLink.match(/\.([a-zA-Z0-9]+)$/);
        if (match) {
            return match[1].toLowerCase();
        }

        // 如果无法识别，返回通用的'link'
        return 'link';
    }

    /**
     * 根据链接类型获取样式配置
     * @param {string} extension - 文件扩展名
     * @returns {object} 样式配置对象
     */
    getLinkTagStyle(extension) {
        let backgroundColor, borderColor, textColor;
        
        switch (extension) {
            case 'md':
                backgroundColor = '#276f86';
                borderColor = '#276f86';
                textColor = '#f7f7f7';
                break;
            case 'pdf':
                backgroundColor = '#f6f6f6';
                borderColor = '#ff6b35';
                textColor = '#ff6b35';
                break;
            case 'http':
            case 'https':
                backgroundColor = '#f7f7f7';
                borderColor = '#00aaff';
                textColor = '#26b4f9';
                break;
            default:
                backgroundColor = '#f7f7f7';
                borderColor = '#444444';
                textColor = '#444444';
                break;
        }
        
        return { backgroundColor, borderColor, textColor };
    }

    /**
     * 创建链接标签
     * 步骤：
     * 1. 创建标签容器和标签
     * 2. 设置样式和内容
     * 3. 添加拖拽事件
     * 4. 添加到容器
     * 
     * @param {HTMLElement} textContainer - 文本容器元素
     * @param {string} nodeId - 节点ID
     * @param {string} hyperLink - 超链接URL
     * @param {string} extension - 文件扩展名
     */
    createLinkTag(textContainer, nodeId, hyperLink, extension) {
        // 获取样式配置
        const style = this.getLinkTagStyle(extension);

        // 创建链接标签容器
        const linkContainer = document.createElement('span');
        linkContainer.className = 'vx-link-container';
        linkContainer.style.cssText = `
            display: inline-flex;
            align-items: center;
            margin-left: 4px;
            vertical-align: baseline;
            flex-shrink: 0;
        `;

        // 创建链接标签
        const linkTag = document.createElement('span');
        linkTag.className = 'vx-link-tag';
        linkTag.textContent = `[${extension}]`;
        linkTag.dataset.url = hyperLink;
        linkTag.dataset.nodeid = nodeId;
        linkTag.title = `点击打开: ${hyperLink}\n拖拽到不同方向可以控制打开位置\n↑上方 ↓下方 ←左侧 →右侧（默认）`;
        linkTag.style.cssText = `
            background: ${style.backgroundColor};
            color: ${style.textColor};
            padding: 2px 4px;
            border-radius: 3px;
            font-size: 10px;
            font-weight: bold;
            cursor: pointer;
            user-select: none;
            border: 1px solid ${style.borderColor};
            display: inline-flex;
            align-items: center;
            line-height: 1;
            min-width: 16px;
            text-align: center;
            transition: all 0.2s ease;
            font-family: monospace;
            box-shadow: 0 1px 2px rgba(0,0,0,0.1);
            white-space: nowrap;
            position: relative;
            z-index: 1;
        `;

        // 拖拽状态变量
        let isDragging = false;
        let startX = 0;
        let startY = 0;
        let dragThreshold = 15; // 拖拽阈值（像素）

        // 添加hover效果
        linkTag.addEventListener('mouseenter', () => {
            if (!isDragging) {
                linkTag.style.transform = 'scale(1.05)';
                linkTag.style.boxShadow = '0 3px 6px rgba(0,0,0,0.2)';
            }
        });
        
        linkTag.addEventListener('mouseleave', () => {
            if (!isDragging) {
                linkTag.style.transform = 'scale(1)';
                linkTag.style.boxShadow = '0 2px 4px rgba(0,0,0,0.1)';
            }
        });

        // 鼠标按下事件 - 开始拖拽检测
        linkTag.addEventListener('mousedown', (event) => {
            event.preventDefault();
            event.stopPropagation();
            
            isDragging = false;
            startX = event.clientX;
            startY = event.clientY;
            
            // 添加拖拽样式
            linkTag.style.cursor = 'grabbing';
            linkTag.style.transform = 'scale(1.1)';
            linkTag.style.boxShadow = '0 4px 12px rgba(0,0,0,0.3)';
            linkTag.style.transition = 'none';
            
            // 显示拖拽指示器（初始状态）
            this.showDragIndicator(startX, startY, 0, 0, 'Right');
        });

        // 鼠标移动事件 - 检测拖拽方向
        const handleMouseMove = (event) => {
            if (event.buttons !== 1) return; // 确保鼠标左键按下
            
            const deltaX = event.clientX - startX;
            const deltaY = event.clientY - startY;
            const distance = Math.sqrt(deltaX * deltaX + deltaY * deltaY);
            
            if (distance > dragThreshold) {
                isDragging = true;
            }
            
            // 如果开始拖拽，更新指示器和方向线
            if (distance > 5) { // 更低的阈值，更敏感的响应
                this.updateDragIndicator(startX, startY, deltaX, deltaY);
            }
        };

        // 鼠标释放事件 - 处理点击或拖拽
        const handleMouseUp = (event) => {
            event.preventDefault();
            event.stopPropagation();
            
            // 移除事件监听器
            document.removeEventListener('mousemove', handleMouseMove);
            document.removeEventListener('mouseup', handleMouseUp);
            
            // 恢复样式
            linkTag.style.cursor = 'pointer';
            linkTag.style.transform = 'scale(1)';
            linkTag.style.boxShadow = '0 2px 4px rgba(0,0,0,0.1)';
            linkTag.style.transition = 'all 0.2s ease';
            
            // 移除拖拽指示器
            this.hideDragIndicator();
            
            if (isDragging) {
                // 计算拖拽方向
                const deltaX = event.clientX - startX;
                const deltaY = event.clientY - startY;
                const direction = this.calculateDragDirection(deltaX, deltaY);
                
                console.log(`LinkHandlerFeature: Drag detected, direction: ${direction}`);
                
                // 发送带方向的URL点击事件
                this.handleUrlClickWithDirection(hyperLink, direction);
            } else {
                // 普通点击 - 默认右边
                console.log('LinkHandlerFeature: Normal click, using default right direction');
                this.handleUrlClickWithDirection(hyperLink, 'Right');
            }
            
            isDragging = false;
        };

        // 添加文档级别的事件监听器
        linkTag.addEventListener('mousedown', () => {
            document.addEventListener('mousemove', handleMouseMove);
            document.addEventListener('mouseup', handleMouseUp);
        });

        linkContainer.appendChild(linkTag);
        textContainer.appendChild(linkContainer);

        console.log(`LinkHandlerFeature: Link tag [${extension}] created successfully with style:`, style);
    }

    /**
     * 显示拖拽方向指示器
     */
    showDragIndicator(startX, startY, deltaX, deltaY, initialDirection) {
        // 移除现有指示器
        this.hideDragIndicator();
        
        const direction = deltaX === 0 && deltaY === 0 ? initialDirection : this.calculateDragDirection(deltaX, deltaY);
        
        // 创建指示器容器
        const container = document.createElement('div');
        container.id = 'vx-drag-indicator-container';
        container.style.cssText = `
            position: fixed;
            top: 0;
            left: 0;
            width: 100vw;
            height: 100vh;
            pointer-events: none;
            z-index: 10000;
        `;

        // 创建方向线条（如果有移动）
        if (Math.abs(deltaX) > 5 || Math.abs(deltaY) > 5) {
            const line = document.createElement('div');
            line.className = 'vx-drag-line';
            
            const endX = startX + deltaX;
            const endY = startY + deltaY;
            const length = Math.sqrt(deltaX * deltaX + deltaY * deltaY);
            const angle = Math.atan2(deltaY, deltaX) * 180 / Math.PI;
            
            line.style.cssText = `
                position: absolute;
                left: ${startX}px;
                top: ${startY}px;
                width: ${length}px;
                height: 2px;
                background: linear-gradient(to right, 
                    rgba(74, 144, 226, 0.8) 0%, 
                    rgba(74, 144, 226, 0.6) 50%,
                    rgba(74, 144, 226, 1) 100%);
                transform-origin: 0 50%;
                transform: rotate(${angle}deg);
                border-radius: 1px;
                box-shadow: 0 0 6px rgba(74, 144, 226, 0.4);
                transition: none;
            `;
            container.appendChild(line);

            // 在线条末端添加箭头
            const arrowHead = document.createElement('div');
            arrowHead.className = 'vx-drag-arrow';
            arrowHead.style.cssText = `
                position: absolute;
                left: ${endX - 6}px;
                top: ${endY - 6}px;
                width: 12px;
                height: 12px;
                background: #4a90e2;
                border-radius: 50%;
                box-shadow: 0 2px 8px rgba(74, 144, 226, 0.6);
            `;
            container.appendChild(arrowHead);
        }
        
        // 创建文字指示器
        const indicator = document.createElement('div');
        indicator.id = 'vx-drag-indicator';
        indicator.style.cssText = `
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            background: rgba(0, 0, 0, 0.85);
            color: white;
            padding: 12px 24px;
            border-radius: 8px;
            font-size: 18px;
            font-weight: bold;
            white-space: nowrap;
            box-shadow: 0 4px 20px rgba(0, 0, 0, 0.3);
            border: 2px solid #4a90e2;
        `;
        
        let directionText = '';
        let arrow = '';
        switch (direction) {
            case 'Up':
                directionText = '上方打开';
                arrow = '↑';
                break;
            case 'Down':
                directionText = '下方打开';
                arrow = '↓';
                break;
            case 'Left':
                directionText = '左侧打开';
                arrow = '←';
                break;
            case 'Right':
            default:
                directionText = '右侧打开';
                arrow = '→';
                break;
        }
        
        indicator.innerHTML = `${arrow} ${directionText}`;
        container.appendChild(indicator);
        
        // 添加CSS动画
        if (!document.getElementById('vx-drag-styles')) {
            const style = document.createElement('style');
            style.id = 'vx-drag-styles';
            style.textContent = `
                @keyframes dragFadeIn {
                    from { opacity: 0; transform: translate(-50%, -50%) scale(0.8); }
                    to { opacity: 1; transform: translate(-50%, -50%) scale(1); }
                }
                #vx-drag-indicator {
                    animation: dragFadeIn 0.2s ease;
                }
            `;
            document.head.appendChild(style);
        }
        
        document.body.appendChild(container);
    }

    /**
     * 更新拖拽指示器
     */
    updateDragIndicator(startX, startY, deltaX, deltaY) {
        const container = document.getElementById('vx-drag-indicator-container');
        if (!container) {
            // 如果容器不存在，重新创建
            this.showDragIndicator(startX, startY, deltaX, deltaY, 'Right');
            return;
        }

        const direction = this.calculateDragDirection(deltaX, deltaY);
        
        // 更新方向线条
        let line = container.querySelector('.vx-drag-line');
        let arrowHead = container.querySelector('.vx-drag-arrow');
        
        if (Math.abs(deltaX) > 5 || Math.abs(deltaY) > 5) {
            const endX = startX + deltaX;
            const endY = startY + deltaY;
            const length = Math.sqrt(deltaX * deltaX + deltaY * deltaY);
            const angle = Math.atan2(deltaY, deltaX) * 180 / Math.PI;
            
            if (!line) {
                line = document.createElement('div');
                line.className = 'vx-drag-line';
                container.appendChild(line);
            }
            
            line.style.cssText = `
                position: absolute;
                left: ${startX}px;
                top: ${startY}px;
                width: ${length}px;
                height: 2px;
                background: linear-gradient(to right, 
                    rgba(74, 144, 226, 0.8) 0%, 
                    rgba(74, 144, 226, 0.6) 50%,
                    rgba(74, 144, 226, 1) 100%);
                transform-origin: 0 50%;
                transform: rotate(${angle}deg);
                border-radius: 1px;
                box-shadow: 0 0 6px rgba(74, 144, 226, 0.4);
                transition: none;
            `;

            if (!arrowHead) {
                arrowHead = document.createElement('div');
                arrowHead.className = 'vx-drag-arrow';
                container.appendChild(arrowHead);
            }
            
            arrowHead.style.cssText = `
                position: absolute;
                left: ${endX - 6}px;
                top: ${endY - 6}px;
                width: 12px;
                height: 12px;
                background: #4a90e2;
                border-radius: 50%;
                box-shadow: 0 2px 8px rgba(74, 144, 226, 0.6);
            `;
        }
        
        // 更新文字指示器
        const indicator = container.querySelector('#vx-drag-indicator');
        if (indicator) {
            let directionText = '';
            let arrow = '';
            switch (direction) {
                case 'Up':
                    directionText = '上方打开';
                    arrow = '↑';
                    break;
                case 'Down':
                    directionText = '下方打开';
                    arrow = '↓';
                    break;
                case 'Left':
                    directionText = '左侧打开';
                    arrow = '←';
                    break;
                case 'Right':
                default:
                    directionText = '右侧打开';
                    arrow = '→';
                    break;
            }
            indicator.innerHTML = `${arrow} ${directionText}`;
        }
    }

    /**
     * 隐藏拖拽指示器
     */
    hideDragIndicator() {
        const container = document.getElementById('vx-drag-indicator-container');
        if (container) {
            container.remove();
        }
        
        // 清理旧的指示器（向后兼容）
        const oldIndicator = document.getElementById('vx-drag-indicator');
        if (oldIndicator) {
            oldIndicator.remove();
        }
    }

    /**
     * 计算拖拽方向
     */
    calculateDragDirection(deltaX, deltaY) {
        const absDeltaX = Math.abs(deltaX);
        const absDeltaY = Math.abs(deltaY);
        
        // 判断主要拖拽方向
        if (absDeltaX > absDeltaY) {
            // 水平方向
            return deltaX > 0 ? 'Right' : 'Left';
        } else {
            // 垂直方向
            return deltaY > 0 ? 'Down' : 'Up';
        }
    }

    /**
     * 根据方向处理URL点击
     * 步骤：
     * 1. 验证URL
     * 2. 根据方向选择打开方式
     * 
     * @param {string} url - 链接URL
     * @param {string} direction - 拖拽方向
     */
    handleUrlClickWithDirection(url, direction) {
        if (!url) return;

        // 根据方向处理点击
        if (window.vxAdapter && window.vxAdapter.handleUrlClickWithDirection) {
            window.vxAdapter.handleUrlClickWithDirection(url, direction);
        } else {
            console.warn('vxAdapter.handleUrlClickWithDirection not available, falling back to normal click');
            this.handleUrlClick(url);
        }
    }

    /**
     * 设置链接标签点击监听器
     */
    setupLinkTagClickListener() {
        document.addEventListener('click', (e) => {
            const linkTag = e.target.closest('.link-tag');
            if (linkTag) {
                const url = linkTag.dataset.url;
                if (url) {
                    this.handleUrlClick(url);
                }
            }
        });
    }

    /**
     * 处理URL点击
     * @param {string} url - 链接URL
     */
    handleUrlClick(url) {
        if (!url) return;
        
        if (window.vxAdapter && window.vxAdapter.handleUrlClick) {
            window.vxAdapter.handleUrlClick(url);
        } else {
            console.warn('vxAdapter.handleUrlClick not available');
        }
    }

    /**
     * 移除所有链接标签
     */
    removeAllLinkTags() {
        try {
            const linkContainers = document.querySelectorAll('.vx-link-container');
            console.log('LinkHandlerFeature: Removing', linkContainers.length, 'existing link tags');
            linkContainers.forEach(container => container.remove());
        } catch (error) {
            console.error('LinkHandlerFeature: Error removing link tags:', error);
        }
    }

    /**
     * 数据变更处理
     * 步骤：
     * 1. 清空节点数据映射
     * 2. 重建节点数据映射
     * 3. 处理所有节点
     * 
     * @param {object} data - 新的数据
     */
    onDataChange(data) {
        console.log('LinkHandlerFeature: onDataChange called with data:', data);
        
        // 清空现有映射
        this.nodeDataMap.clear();
        
        if (!data || !data.nodeData) {
            console.warn('LinkHandlerFeature: Invalid data structure');
            return;
        }

        // 重建节点数据映射
        this.buildNodeDataMapRecursive(data, this.nodeDataMap);
        
        // 验证映射结果
        this.validateNodeDataMap();
        
        console.log('LinkHandlerFeature: Built nodeDataMap with', this.nodeDataMap.size, 'entries');
        
        // 处理所有节点
        this.processAllNodes();
    }

    /**
     * 验证节点数据映射
     */
    validateNodeDataMap() {
        console.log('LinkHandlerFeature: Validating nodeDataMap');
        let hyperLinkCount = 0;
        
        this.nodeDataMap.forEach((nodeData, nodeId) => {
            if (nodeData.hyperLink) {
                hyperLinkCount++;
                console.log('LinkHandlerFeature: Found node with hyperlink:', {
                    nodeId: nodeId,
                    topic: nodeData.topic,
                    hyperLink: nodeData.hyperLink
                });
            }
        });
        
        console.log(`LinkHandlerFeature: Found ${hyperLinkCount} nodes with hyperlinks out of ${this.nodeDataMap.size} total nodes`);
    }

    /**
     * 递归构建节点数据映射
     * @param {object} data - 节点数据
     * @param {Map} map - 映射表
     */
    buildNodeDataMapRecursive(data, map) {
        if (!data) return;

        // 如果是根节点，从nodeData开始
        const nodeData = data.nodeData || data;
        if (!nodeData) return;

        // 添加当前节点到映射
        // console.log('LinkHandlerFeature: Adding node to map:', {
        //     id: nodeData.id,
        //     topic: nodeData.topic,
        //     hyperLink: nodeData.hyperLink
        // });
        map.set(nodeData.id, nodeData);

        // 如果有子节点，递归处理
        if (nodeData.children && Array.isArray(nodeData.children)) {
            nodeData.children.forEach(child => {
                this.buildNodeDataMapRecursive(child, map);
            });
        }
    }

    // 检查是否是脑图节点
    isMindmapNode(element) {
        // 检查多种可能的脑图节点特征
        return element.hasAttribute && (
            element.hasAttribute('data-nodeid') ||
            element.classList.contains('topic') ||
            element.classList.contains('node') ||
            element.tagName.toLowerCase() === 'tpc'
        );
    }

    /**
     * 处理所有节点
     * 步骤：
     * 1. 移除现有链接标签
     * 2. 获取所有思维导图节点
     * 3. 为每个节点处理链接
     */
    processAllNodes() {
        if (!this.core) {
            console.warn('LinkHandlerFeature: Core not available, cannot process nodes.');
            return;
        }

        try {
            // 在处理DOM前禁用观察者，防止无限循环
            this.core.disableObserver();

            console.log('LinkHandlerFeature: processAllNodes called');
            
            // 关键修复：每次处理时，都从core主动获取最新的数据，确保数据同步
            if (this.core && this.core.mindElixir) {
                const mindmapData = this.core.mindElixir.getAllData();
                if (mindmapData && mindmapData.nodeData) {
                    this.nodeDataMap.clear();
                    this.buildNodeDataMapRecursive(mindmapData.nodeData, this.nodeDataMap);
                    console.log('LinkHandlerFeature: Node data map rebuilt with latest data. Size:', this.nodeDataMap.size);
                } else {
                    console.warn('LinkHandlerFeature: Could not get latest data from core.');
                }
            } else {
                console.warn('LinkHandlerFeature: Core or MindElixir instance not available to fetch latest data.');
                return; // 如果没有核心实例，无法继续
            }
            
            this.removeAllLinkTags();
            
            try {
                // 查找所有可能的脑图节点
                const mindmapElement = document.getElementById('vx-mindmap');
                if (!mindmapElement) {
                    console.warn('LinkHandlerFeature: Could not find #vx-mindmap element');
                    return;
                }

                // 查找所有节点
                const mindmapNodes = mindmapElement.querySelectorAll('tpc[data-nodeid]');
                
                console.log('LinkHandlerFeature: Found', mindmapNodes.length, 'potential mindmap nodes');
                console.log('LinkHandlerFeature: nodeDataMap size:', this.nodeDataMap.size);
                
                // 处理每个节点
                mindmapNodes.forEach((domNode, index) => {
                    const nodeId = domNode.dataset.nodeid;
                    if (nodeId) {
                        const cleanNodeId = nodeId.startsWith('me') ? nodeId.substring(2) : nodeId;
                        const nodeData = this.nodeDataMap.get(cleanNodeId);
                        if (nodeData && nodeData.hyperLink) {
                            // console.log(`LinkHandlerFeature: Processing node ${index + 1}/${mindmapNodes.length}:`, {
                            //     nodeId: cleanNodeId,
                            //     hyperLink: nodeData.hyperLink
                            // });
                            this.processNodeWithData(domNode, this.nodeDataMap);
                        }
                    }
                });

                // 验证处理结果
                const addedTags = document.querySelectorAll('.vx-link-container');
                console.log('LinkHandlerFeature: Added', addedTags.length, 'link tags');

            } catch (error) {
                console.error('LinkHandlerFeature: Error processing nodes:', error);
            }
        } finally {
            // 在finally块中重新启用观察者，确保即使发生错误也能恢复
            // 使用setTimeout确保在当前事件循环结束后再启用，避免立即重新触发
            setTimeout(() => {
                if (this.core) {
                    this.core.enableObserver();
                }
            }, 50);
        }
    }
} 