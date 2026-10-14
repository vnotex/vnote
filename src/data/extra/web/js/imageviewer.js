class ImageViewer {
    constructor() {
        this.viewImageClass = 'vx-image-view-image';
        this.loadGeneration = 0;
        this.activePointerId = null;
        this.dragMoved = false;
        this.naturalWidth = 0;
        this.naturalHeight = 0;
        this.scale = 1;
        this.fitMode = true;
        this.left = 0;
        this.top = 0;
        this.viewport = { width: 0, height: 0 };

        this.pendingNativeRequest = null;
        this.activeNativeId = 0;
        this.lastNativeId = 0;
        this.nativeAdapter = null;
        window.vxcore.on('ready', () => {
            this.setupImageViewBox();
            this._scheduleNativeOpen();
        });
        window.vxcore.on('fullMarkdownRendered', () => this._scheduleNativeOpen());
    }

    bindAdapter(p_adapter) {
        if (!p_adapter || this.nativeAdapter === p_adapter
            || !p_adapter.imageViewRequested || !p_adapter.imageViewCancelRequested
            || !p_adapter.setImageViewState) {
            return;
        }
        this.nativeAdapter = p_adapter;
        p_adapter.imageViewRequested.connect((id, url) => {
            if (id <= this.lastNativeId) {
                return;
            }
            this.closeImageViewBox();
            this.lastNativeId = id;
            this.pendingNativeRequest = { id, url };
            this._scheduleNativeOpen();
        });
        p_adapter.imageViewCancelRequested.connect((id) => {
            this.lastNativeId = Math.max(this.lastNativeId, id);
            if (this.activeNativeId === id
                || (this.pendingNativeRequest && this.pendingNativeRequest.id === id)) {
                this.closeImageViewBox();
            }
        });
    }

    _scheduleNativeOpen() {
        // finishWorker emits before draining pendingData and other render listeners.
        // One microtask after that event is sufficient; never poll a busy renderer.
        Promise.resolve().then(() => this._openPendingNativeImage());
    }

    _openPendingNativeImage() {
        const request = this.pendingNativeRequest;
        const core = window.vxcore;
        if (!request || !core.initialized || core.numOfOngoingWorkers !== 0
            || !core.pendingData || core.pendingData.text !== null) {
            return;
        }
        this.pendingNativeRequest = null;
        const generation = this._beginLoad('transparent');
        if (generation === null) {
            this.nativeAdapter.setImageViewState(request.id, false);
            return;
        }
        this.activeNativeId = request.id;
        this._loadImage(request.url, generation);
        // The modal (including its loading/error state and Close) now exists.
        this.nativeAdapter.setImageViewState(request.id, true);
    }

    setupImageViewBox() {
        if (this.initialized) {
            return true;
        }
        this.viewBoxContainer = document.getElementById('vx-image-view-box');
        this.viewBox = document.getElementById('vx-image-view');
        this.closeButton = document.getElementById('vx-image-view-close');
        this.controls = document.getElementById('vx-image-view-controls');
        this.zoomInButton = document.getElementById('vx-image-view-zoom-in');
        this.zoomOutButton = document.getElementById('vx-image-view-zoom-out');
        this.resetButton = document.getElementById('vx-image-view-reset');
        this.error = document.getElementById('vx-image-view-error');
        // Exported HTML and renderer harnesses need no modal markup.
        if (!this.viewBoxContainer || !this.viewBox || !this.closeButton || !this.controls
            || !this.zoomInButton || !this.zoomOutButton || !this.resetButton || !this.error) {
            return false;
        }
        this.initialized = true;
        for (const [button, key] of [[this.zoomInButton, 'image.zoomIn'],
                                    [this.zoomOutButton, 'image.zoomOut'],
                                    [this.resetButton, 'image.reset']]) {
            const label = window.vxI18n.tr(key);
            button.title = label;
            button.setAttribute('aria-label', label);
        }
        this.error.textContent = window.vxI18n.tr('image.error');
        this.zoomInButton.onclick = () => this.zoom(true);
        this.zoomOutButton.onclick = () => this.zoom(false);
        this.resetButton.onclick = () => this.resetZoom();
        this.controls.onclick = (e) => e.stopPropagation();
        this.closeButton.onclick = () => this.closeImageViewBox();
        this.viewBoxContainer.onclick = (e) => {
            if (e.target === this.viewBoxContainer && !this.dragMoved) {
                this.closeImageViewBox();
            }
        };
        this.viewBoxContainer.onpointerdown = (e) => {
            if (e.target === this.viewBoxContainer) {
                this.dragMoved = false;
            }
        };
        this.viewBoxContainer.addEventListener('wheel', (e) => {
            if (e.ctrlKey || e.target !== this.viewBox || !this.naturalWidth) {
                return;
            }
            if (e.deltaY) {
                this._zoomAt(e.deltaY < 0, e.clientX, e.clientY);
            }
            e.preventDefault();
            e.stopPropagation();
        }, { passive: false });
        this.viewBox.draggable = false;
        this.viewBox.ondragstart = (e) => e.preventDefault();
        this.viewBox.onpointerdown = (e) => {
            if (e.button !== 0 || this.activePointerId !== null || !this.naturalWidth) {
                return;
            }
            this.activePointerId = e.pointerId;
            this.dragMoved = false;
            this.pointerX = e.clientX;
            this.pointerY = e.clientY;
            this.viewBox.setPointerCapture(e.pointerId);
            this.viewBox.classList.add('vx-image-dragging');
            e.preventDefault();
        };
        this.viewBox.onpointermove = (e) => {
            if (e.pointerId !== this.activePointerId) {
                return;
            }
            const dx = e.clientX - this.pointerX;
            const dy = e.clientY - this.pointerY;
            this.dragMoved = this.dragMoved || dx !== 0 || dy !== 0;
            this.left += dx;
            this.top += dy;
            this.pointerX = e.clientX;
            this.pointerY = e.clientY;
            this._applyGeometry();
            e.preventDefault();
        };
        const endDrag = (e) => {
            if (e.pointerId === this.activePointerId) {
                this._endDrag();
            }
        };
        this.viewBox.onpointerup = endDrag;
        this.viewBox.onpointercancel = endDrag;
        this.viewBox.onlostpointercapture = endDrag;
        window.addEventListener('blur', () => this._endDrag());
        window.addEventListener('resize', () => this._resize());
        window.addEventListener('keydown', (e) => {
            if (e.key === 'Escape' && (this.isViewingImage() || this.pendingNativeRequest)) {
                this.closeImageViewBox();
                e.preventDefault();
                e.stopImmediatePropagation();
            }
        }, true);
        return true;
    }

    _endDrag() {
        const id = this.activePointerId;
        this.activePointerId = null;
        if (this.viewBox) {
            if (id !== null && this.viewBox.hasPointerCapture(id)) {
                this.viewBox.releasePointerCapture(id);
            }
            this.viewBox.classList.remove('vx-image-dragging');
        }
    }

    _detachLoad() {
        if (this.viewBox) {
            this.viewBox.onload = null;
            this.viewBox.onerror = null;
        }
    }

    closeImageViewBox() {
        const id = this.activeNativeId
            || (this.pendingNativeRequest ? this.pendingNativeRequest.id : 0);
        this.activeNativeId = 0;
        this.pendingNativeRequest = null;
        ++this.loadGeneration;
        this._detachLoad();
        this._endDrag();
        if (this.viewBoxContainer) {
            this.viewBoxContainer.style.display = 'none';
        }
        if (this.viewBox) {
            this.viewBox.removeAttribute('src');
        }
        if (id && this.nativeAdapter) {
            this.nativeAdapter.setImageViewState(id, false);
        }
    }

    isViewingImage() {
        return !!this.viewBoxContainer && this.viewBoxContainer.style.display === 'block';
    }

    _beginLoad(p_background) {
        this.closeImageViewBox();
        if (!this.setupImageViewBox()) {
            return null;
        }
        const generation = ++this.loadGeneration;
        this._detachLoad();
        this._endDrag();
        this.dragMoved = false;
        this.naturalWidth = this.naturalHeight = 0;
        this.scale = 1;
        this.fitMode = true;
        this.left = this.top = 0;
        this.viewBox.removeAttribute('src');
        this.viewBox.removeAttribute('style');
        this.viewBox.style.display = 'none';
        this.viewBox.style.backgroundColor = p_background;
        this.error.hidden = true;
        const colors = window.getComputedStyle(document.body);
        this.viewBoxContainer.style.setProperty('--vx-image-view-fg', colors.color);
        this.viewBoxContainer.style.setProperty('--vx-image-view-bg', colors.backgroundColor);
        this.viewBoxContainer.style.display = 'block';
        this._resize();
        this._updateControls();
        return generation;
    }

    _loadImage(p_src, p_generation) {
        if (p_generation !== this.loadGeneration || !this.isViewingImage()) {
            return;
        }
        let settled = false;
        const finish = (failed) => {
            if (settled || p_generation !== this.loadGeneration || !this.isViewingImage()) {
                return;
            }
            settled = true;
            this._detachLoad();
            if (failed || this.viewBox.naturalWidth <= 0 || this.viewBox.naturalHeight <= 0) {
                this.viewBox.style.display = 'none';
                this.viewBox.removeAttribute('src');
                this.naturalWidth = this.naturalHeight = 0;
                this.error.hidden = false;
                this._updateControls();
                return;
            }
            this.naturalWidth = this.viewBox.naturalWidth;
            this.naturalHeight = this.viewBox.naturalHeight;
            this.viewBox.style.display = 'block';
            this.resetZoom();
        };
        this.viewBox.onload = () => finish(false);
        this.viewBox.onerror = () => finish(true);
        if (!p_src) {
            finish(true);
            return;
        }
        this.viewBox.src = p_src;
        if (this.viewBox.complete) {
            finish(false);
        }
    }

    viewImage(p_imgSrc, p_background = 'transparent') {
        const generation = this._beginLoad(p_background);
        if (generation !== null) {
            this._loadImage(p_imgSrc, generation);
        }
    }

    viewIMG(p_imgNode) {
        this.viewImage(p_imgNode.src);
    }

    _fitScale() {
        return Math.min(1, this.viewport.width / this.naturalWidth,
                        this.viewport.height / this.naturalHeight);
    }

    _updateControls() {
        const loaded = this.naturalWidth > 0 && this.naturalHeight > 0;
        this.zoomInButton.disabled = !loaded || this.scale >= 16;
        this.zoomOutButton.disabled = !loaded || this.scale <= Math.min(0.05, this._fitScale());
        this.resetButton.disabled = !loaded;
    }

    _applyGeometry() {
        if (!this.naturalWidth || !this.isViewingImage()) {
            return;
        }
        const width = this.naturalWidth * this.scale;
        const height = this.naturalHeight * this.scale;
        const clamp = (offset, image, viewport) => image <= viewport
            ? (viewport - image) / 2 : Math.max(viewport - image, Math.min(0, offset));
        this.left = clamp(this.left, width, this.viewport.width);
        this.top = clamp(this.top, height, this.viewport.height);
        Object.assign(this.viewBox.style, {
            width: width + 'px', height: height + 'px',
            left: this.left + 'px', top: this.top + 'px'
        });
        this._updateControls();
    }

    resetZoom() {
        this._endDrag();
        this.fitMode = true;
        if (!this.naturalWidth || !this.isViewingImage()
            || this.viewport.width <= 0 || this.viewport.height <= 0) {
            return;
        }
        this.scale = this._fitScale();
        this.left = (this.viewport.width - this.naturalWidth * this.scale) / 2;
        this.top = (this.viewport.height - this.naturalHeight * this.scale) / 2;
        this._applyGeometry();
    }

    _zoomAt(p_zoomIn, p_x, p_y) {
        if (!this.naturalWidth || !this.isViewingImage()
            || this.viewport.width <= 0 || this.viewport.height <= 0) {
            return;
        }
        const scale = Math.max(Math.min(0.05, this._fitScale()),
                               Math.min(16, p_zoomIn ? this.scale * 1.25 : this.scale / 1.25));
        const ratio = scale / this.scale;
        this.left = p_x - (p_x - this.left) * ratio;
        this.top = p_y - (p_y - this.top) * ratio;
        this.scale = scale;
        this.fitMode = false;
        this._applyGeometry();
    }

    zoom(p_zoomIn) {
        this._zoomAt(p_zoomIn, this.viewport.width / 2, this.viewport.height / 2);
    }

    _resize() {
        if (!this.isViewingImage()) {
            return;
        }
        const previous = this.viewport;
        this.viewport = { width: this.viewBoxContainer.clientWidth,
                          height: this.viewBoxContainer.clientHeight };
        this.controls.style.left = Math.max(0, this.viewport.width - this.controls.offsetWidth - 12) + 'px';
        this.controls.style.top = Math.max(0, this.viewport.height - this.controls.offsetHeight - 12) + 'px';
        if (this.fitMode) {
            this.resetZoom();
        } else {
            this.left += (this.viewport.width - previous.width) / 2;
            this.top += (this.viewport.height - previous.height) / 2;
            this._applyGeometry();
        }
    }

    setupForAllImages(p_container) {
        // Rebinding rendered images closes an open popup, not an unopened native intent.
        if (this.isViewingImage()) {
            this.closeImageViewBox();
        }
        for (const image of p_container.getElementsByTagName('img')) {
            if (image !== this.viewBox) {
                this.setupIMGToView(image);
            }
        }
    }

    setupIMGToView(p_node) {
        if (!p_node || p_node.nodeName.toLowerCase() != 'img') {
            return;
        }

        p_node.classList.add(this.viewImageClass);
        let func = function(p_imageViewer, p_node) {
            let imageViewer = p_imageViewer;
            let node = p_node;
            return function() {
                imageViewer.viewIMG(node);
            };
        };
        p_node.ondblclick = func(this, p_node);
    };

    setupSVGToView(p_node, p_forceBackground = false) {
        if (!p_node || p_node.nodeName.toLowerCase() != 'svg') {
            return;
        }

        let onSVGDoubleClick = function(imageViewer, node, forceBackground, e) {
            e = e || window.event;
            let name = e.target.nodeName.toLowerCase();
            if (name != 'text' && name != 'tspan') {
                if (forceBackground) {
                    // Use <svg>'s parent's background color.
                    let svgNode = e.target;
                    while (svgNode && svgNode.nodeName.toLowerCase() != 'svg') {
                        svgNode = svgNode.parentNode;
                    }

                    if (svgNode) {
                        let style = window.getComputedStyle(svgNode.parentNode, null);
                        if (style.backgroundColor === 'rgba(0, 0, 0, 0)') {
                            imageViewer.viewSVG(node, '#ffffff');
                        } else {
                            imageViewer.viewSVG(node, style.backgroundColor);
                        }
                    }
                } else {
                    imageViewer.viewSVG(node);
                }

                e.preventDefault();
            }
        };

        p_node.classList.add(this.viewImageClass);
        let func = function(p_imageViewer, p_node, p_forceBackground) {
            let imageViewer = p_imageViewer;
            let node = p_node;
            let forceBackground = p_forceBackground;
            return function(e) {
                onSVGDoubleClick(imageViewer, node, forceBackground, e);
            };
        };
        p_node.ondblclick = func(this, p_node, p_forceBackground);
    };

    viewSVG(p_svgNode, p_background = 'transparent') {
        // XMLSerializer keeps SVG/foreignObject well formed; URI escaping preserves '#'.
        const svg = new XMLSerializer().serializeToString(p_svgNode);
        const src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg);
        if (window.vxOptions.protectedView) {
            const generation = this._beginLoad(p_background);
            if (generation === null) {
                return;
            }
            window.vxMarkdownAdapter.protectedImageUrl(src, (url) => {
                this._loadImage(url, generation);
            });
            return;
        }
        this.viewImage(src, p_background);
    }
}

window.vxImageViewer = new ImageViewer;
