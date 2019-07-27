var imageViewDiv = document.getElementById('image-view-div');

var viewImage = function(imgSrc, background) {
    viewBoxImageMouseDown = false;

    imageViewDiv.style.display = 'block';

    var boxImage = document.getElementById('image-view');
    boxImage.src = imgSrc;
    boxImage.style.backgroundColor = background;

    // Restore image-view.
    boxImage.style.width = '';
    boxImage.style.position = '';
    boxImage.style.zIndex = '';
};

var viewIMG = function(imgNode) {
    viewImage(imgNode.src, 'transparent');
};

var viewSVG = function(svgNode, background) {
    var svg = svgNode.outerHTML.replace(/#/g, '%23').replace(/[\r\n]/g, '');
    var src = 'data:image/svg+xml;utf8,' + svg;

    viewImage(src, background);
};

var viewBoxImageMouseDown = false;
var viewBoxImageOffsetToMouse = [0, 0];

var closeImageViewBox = function() {
    imageViewDiv.style.display = "none";
};

var initImageViewBox = function() {
    // Left and top in pixel.
    var moveImage = function(img, left, top) {
        if (img.style.position != 'absolute') {
            img.style.position = 'absolute';
            img.style.zIndex = parseInt(document.getElementById('image-view-close').style.zIndex) - 1;
        }

        img.style.left = left + 'px';
        img.style.top  = top + 'px';
    };

    // View box.
    imageViewDiv.onclick = function(e) {
        e = e || window.event;
        var boxImage = document.getElementById('image-view');
        if (e.target.id != boxImage.id) {
            // Click outside the image to close the box.
            closeImageViewBox();
        }

        e.preventDefault();
    };

    imageViewDiv.onwheel = function(e) {
        e = e || window.event;
        var ctrl = !!e.ctrlKey;
        if (ctrl) {
            return;
        }

        var target = e.target;
        if (!target || target.id != 'image-view') {
            return;
        }

        var rect = target.getBoundingClientRect();
        var centerX = e.clientX - rect.left;
        var centerY = e.clientY - rect.top;

        var oriWidth = target.getAttribute('oriWidth');
        var oriHeight = target.getAttribute('oriWidth');
        if (!oriWidth) {
            oriWidth = rect.width;
            oriHeight = rect.height;

            target.setAttribute('oriWidth', oriWidth);
            target.setAttribute('oriHeight', oriHeight);
        }

        var step = Math.floor(oriWidth / 4);

        var value = e.wheelDelta || -e.detail;
        // delta >= 0 is up, which will trigger zoom in.
        var delta = Math.max(-1, Math.min(1, value));

        var newWidth = rect.width + (delta < 0 ? -step : step);
        if (newWidth < 200) {
            e.preventDefault();
            return;
        }

        var factor = newWidth / rect.width;

        target.style.width = newWidth + 'px';

        // Adjust the image around the center point.
        moveImage(target, e.clientX - centerX * factor, e.clientY - centerY * factor);

        e.preventDefault();
    };

    // Content image.
    var boxImage = document.getElementById('image-view');
    boxImage.onmousedown = function(e) {
        e = e || window.event;
        var target = this || e.target;
        viewBoxImageMouseDown = true;
        viewBoxImageOffsetToMouse = [
            target.offsetLeft - e.clientX,
            target.offsetTop - e.clientY
        ];
        e.preventDefault();
    };

    boxImage.onmouseup = function(e) {
        e = e || window.event;
        viewBoxImageMouseDown = false;
        e.preventDefault();
    };

    boxImage.onmousemove = function(e) {
        e = e || window.event;
        var target = this || e.target;
        if (viewBoxImageMouseDown) {
            moveImage(target, e.clientX + viewBoxImageOffsetToMouse[0], e.clientY + viewBoxImageOffsetToMouse[1]);
        }

        e.preventDefault();
    };

    // Close button.
    document.getElementById('image-view-close').onclick = closeImageViewBox;
};

initImageViewBox();

var setupImageView = function() {
    closeImageViewBox();

    var imgs = document.getElementsByTagName('img');
    for (var i = 0; i < imgs.length; ++i) {
        if (imgs[i].id == 'image-view') {
            continue;
        }

        setupIMGToView(imgs[i]);
    }
};

var isViewingImage = function() {
    return imageViewDiv.style.display == 'block';
};

var onSVGDoubleClick = function(forceBackground, e) {
    e = e || window.event;
    var name = e.target.nodeName.toLowerCase();
    if (name != 'text' && name != 'tspan') {
        if (forceBackground) {
            // Use <svg>'s parent's background color.
            var svgNode = e.target;
            while (svgNode && svgNode.nodeName.toLowerCase() != 'svg') {
                svgNode = svgNode.parentNode;
            }

            if (svgNode) {
                var style = window.getComputedStyle(svgNode.parentNode, null);
                viewSVG(this, style.backgroundColor);
            }
        } else {
            viewSVG(this, 'transparent');
        }

        e.preventDefault();
    }
};

var setupSVGToView = function(node, forceBackground) {
    if (!node || node.nodeName.toLowerCase() != 'svg') {
        return;
    }

    node.classList.add('view-svg');
    node.ondblclick = onSVGDoubleClick.bind(node, forceBackground);
};

var setupIMGToView = function(node) {
    if (!node || node.nodeName.toLowerCase() != 'img') {
        return;
    }

    node.classList.add('view-image');
    node.ondblclick = function() {
        viewIMG(this);
    };
};
