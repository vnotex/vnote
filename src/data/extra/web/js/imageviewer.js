class ImageViewer {
    constructor() {
        this.viewBoxMouseDown = false;
        this.viewBoxOffsetToMouse = { x: 0, y: 0 }
        this.viewImageClass = 'vx-image-view-image';

        window.vnotex.on('ready', () => {
            this.viewBoxContainer = document.getElementById('vx-image-view-box');
            this.viewBox = document.getElementById('vx-image-view');
            this.closeButton = document.getElementById('vx-image-view-close');

            this.setupImageViewBox();
        });
    }

    setupImageViewBox() {
        // Left and top in pixel.
        let moveImage = (img, left, top) => {
            if (img.style.position != 'absolute') {
                img.style.position = 'absolute';
                img.style.zIndex = parseInt(this.closeButton.style.zIndex) - 1;
            }

            img.style.left = left + 'px';
            img.style.top  = top + 'px';
        };

        // View box.
        this.viewBoxContainer.onclick = (e) => {
            e = e || window.event;
            if (e.target.id != this.viewBox.id) {
                // Click outside the image to close the box.
                this.closeImageViewBox();
            }

            e.preventDefault();
        };

        this.viewBoxContainer.onwheel = (e) => {
            e = e || window.event;
            let ctrl = !!e.ctrlKey;
            if (ctrl) {
                return;
            }

            let target = e.target;
            if (target != this.viewBox) {
                return;
            }

            let rect = target.getBoundingClientRect();
            let centerX = e.clientX - rect.left;
            let centerY = e.clientY - rect.top;

            let oriWidth = target.getAttribute('oriWidth');
            let oriHeight = target.getAttribute('oriWidth');
            if (!oriWidth) {
                oriWidth = rect.width;
                oriHeight = rect.height;

                target.setAttribute('oriWidth', oriWidth);
                target.setAttribute('oriHeight', oriHeight);
            }

            let step = Math.floor(oriWidth / 4);

            let value = e.wheelDelta || -e.detail;
            // delta >= 0 is up, which will trigger zoom in.
            let delta = Math.max(-1, Math.min(1, value));

            let newWidth = rect.width + (delta < 0 ? -step : step);
            if (newWidth < 200) {
                e.preventDefault();
                return;
            }

            let factor = newWidth / rect.width;

            target.style.width = newWidth + 'px';

            // Adjust the image around the center point.
            moveImage(target, e.clientX - centerX * factor, e.clientY - centerY * factor);

            e.preventDefault();
        };

        // Content image.
        this.viewBox.onmousedown = (e) => {
            e = e || window.event;
            let target = e.target;
            this.viewBoxMouseDown = true;
            this.viewBoxOffsetToMouse = {
                x: target.offsetLeft - e.clientX,
                y: target.offsetTop - e.clientY
            };
            e.preventDefault();
        };

        this.viewBox.onmouseup = (e) => {
            e = e || window.event;
            this.viewBoxMouseDown = false;
            e.preventDefault();
        };

        this.viewBox.onmousemove = (e) => {
            e = e || window.event;
            let target = e.target;
            if (this.viewBoxMouseDown) {
                moveImage(target,
                          e.clientX + this.viewBoxOffsetToMouse.x,
                          e.clientY + this.viewBoxOffsetToMouse.y);
            }

            e.preventDefault();
        };

        // Close button.
        this.closeButton.onclick = () => {
            this.closeImageViewBox();
        };
    };

    closeImageViewBox() {
        this.viewBoxContainer.style.display = "none";
    };

    setupForAllImages(p_container) {
        this.closeImageViewBox();

        let images = p_container.getElementsByTagName('img');
        for (let i = 0; i < images.length; ++i) {
            if (images[i] === this.viewBox) {
                continue;
            }

            this.setupIMGToView(images[i]);
        }
    };

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

    viewImage(p_imgSrc, p_background = 'transparent') {
        this.viewBoxMouseDown = false;
        this.viewBoxContainer.style.display = 'block';

        this.viewBox.src = p_imgSrc;
        this.viewBox.style.backgroundColor = p_background;

        // Restore view box.
        this.viewBox.style.width = '';
        this.viewBox.style.position = '';
        this.viewBox.style.zIndex = '';
    };

    viewIMG(p_imgNode) {
        this.viewImage(p_imgNode.src);
    };

    isViewingImage() {
        return this.viewBoxContainer.style.display === 'block';
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
        var svg = p_svgNode.outerHTML.replace(/#/g, '%23').replace(/[\r\n]/g, '');
        var src = 'data:image/svg+xml;utf8,' + svg;

        this.viewImage(src, p_background);
    };
}

window.vxImageViewer = new ImageViewer;
