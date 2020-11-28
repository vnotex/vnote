class SvgToImage {
    constructor() {

    }

    static loadImage(p_src, p_opt, p_callback) {
        if (typeof p_opt === 'function') {
            callback = p_opt;
            p_opt = null;
        }

        let el = document.createElement('img');
        let locked;

        el.onload = function onLoaded () {
            if (locked) return;
            locked = true;

            if (p_callback) p_callback(undefined, el);
        };

        el.onerror = function onError () {
            if (locked) return;
            locked = true;

            if (p_callback) p_callback(new Error('Unable to load "' + p_src + '"'), el);
        };

        if (p_opt && p_opt.crossOrigin) {
            el.crossOrigin = p_opt.crossOrigin;
        }

        el.src = p_src;

        return el;
    }

    static svgToImage(p_svg, p_opt, p_callback) {
        if (typeof p_opt === 'function') {
            p_callback = p_opt;
            p_opt = {};
        }
        p_callback = p_callback || function() {};
        p_opt = p_opt || {};

        let domUrl = this.getUrl();
        if (!Array.isArray(p_svg)) {
            p_svg = [ p_svg ];
        }

        let blob = null;
        try {
            blob = new window.Blob(p_svg, {
                type: 'image/svg+xml;charset=utf-8'
            });
        } catch (e) {
            console.error('failed to create blob', e);
        }

        let url = domUrl.createObjectURL(blob);
        this.loadImage(url, p_opt, function(err, img) {
            domUrl.revokeObjectURL(url);
            if (err) {
                // try again for Safari 8.0, using simple encodeURIComponent
                // this will fail with DOM content but at least it works with SVG.
                let url2 = 'data:image/svg+xml,' + encodeURIComponent(p_svg.join(''));
                return SvgToImage.loadImage(url2, p_opt, p_callback);
            }

            p_callback(err, img)
        });
    }

    static getUrl() {
        return window.URL || window.webkitURL || window.mozURL || window.msURL;
    }
}
