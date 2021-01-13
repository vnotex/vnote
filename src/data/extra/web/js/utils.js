class Utils {
    constructor() {
    }

    static parentFolder(p_path) {
        return p_path.substr(0, p_path.lastIndexOf('/'));
    }

    // @p_type: 'blob'/'text'.
    static httpGet(p_url, p_type, p_callback) {
        let xmlHttp = new XMLHttpRequest();
        xmlHttp.open("GET", p_url);
        xmlHttp.responseType = p_type;

        xmlHttp.onload = function() {
            p_callback(xmlHttp.response);
        };

        xmlHttp.send(null);
    }

    static loadScript(p_src, p_callback) {
        let script = document.createElement('script');
        if (p_callback) {
            script.onload = p_callback;
        }
        script.type = 'text/javascript';
        script.src = p_src;
        document.head.appendChild(script);
    }

    static loadScripts(p_srcs, p_callback) {
        if (p_srcs.length == 1) {
            this.loadScript(p_srcs[0], p_callback);
            return;
        }

        let func = (function() {
            let scriptsToLoad = p_srcs.length;
            let callback = p_callback;
            return function() {
                --scriptsToLoad;
                if (scriptsToLoad == 0) {
                    callback();
                }
            };
        })();

        let scriptsToLoad = p_srcs.length;
        p_srcs.forEach((p_src) => {
            this.loadScript(p_src, func);
        });
    }

    // 'lang-' + ['cpp', 'md'] => ['lang-cpp', 'lang-md'].
    static addPrefix(p_prefix, p_elements) {
        let res = [];
        p_elements.forEach(function(p_ele) {
            res.push(p_prefix + p_ele);
        });
        return res;
    }

    // Check if @p_node contains source line info. If yes, add it to @p_newNode.
    static checkSourceLine(p_node, p_newNode) {
        if (p_node.classList.contains('source-line')) {
            p_newNode.classList.add('source-line');
            p_newNode.setAttribute('data-source-line', p_node.getAttribute('data-source-line'));
        }
    }

    // Replace @p_node with @p_newNode.
    static replaceNodeWithPreCheck(p_node, p_newNode) {
        let childNode = p_node;
        let parentNode = p_node.parentNode;
        if (parentNode.tagName.toLowerCase() == 'pre') {
            childNode = parentNode;
            parentNode = parentNode.parentNode;
        }
        parentNode.replaceChild(p_newNode, childNode);
    }

    static viewPortRect() {
        return {
            left: document.documentElement.scrollLeft || document.body.scrollLeft || window.pageXOffset,
            top: document.documentElement.scrollTop || document.body.scrollTop || window.pageYOffset,
            width: document.documentElement.clientWidth || document.body.clientWidth,
            height: document.documentElement.clientHeight || document.body.clientHeight
        }
    }

    static nodeRectInContent(p_node) {
        let rect = p_node.getBoundingClientRect();
        let vrect = this.viewPortRect();
        return {
            left: vrect.left + rect.left,
            top: vrect.top + rect.top,
            width: rect.width,
            height: rect.height
        };
    }

    static isVisible(p_node) {
        let rect = p_node.getBoundingClientRect();
        let vrect = this.viewPortRect();
        if (rect.top < 0 || rect.left < 0
            || rect.bottom > vrect.height || rect.right > vrect.width) {
            return false;
        }
        return true;
    }

    static headingSequenceRegExp() {
        return /^\d{1,3}(?:\.\d+)*\. /;
    }

    static fetchStyleContent() {
        let styles = "";
        for (let styleIdx = 0; styleIdx < document.styleSheets.length; ++styleIdx) {
            let styleSheet = document.styleSheets[styleIdx];
            if (styleSheet.cssRules) {
                let baseUrl = null;
                if (styleSheet.href) {
                    let scheme = Utils.getUrlScheme(styleSheet.href);
                    // We only translate local resources.
                    if (scheme === 'file' || scheme === 'qrc') {
                        baseUrl = styleSheet.href.substr(0, styleSheet.href.lastIndexOf('/'));
                    }
                }

                for (let ruleIdx = 0; ruleIdx < styleSheet.cssRules.length; ++ruleIdx) {
                    let css = styleSheet.cssRules[ruleIdx].cssText;
                    if (baseUrl) {
                        // Try to replace the url() with absolute path.
                        css = Utils.translateCssUrlToAbsolute(baseUrl, css);
                    }

                    styles = styles + css + "\n";
                }
            }
        }

        return styles;
    }

    static translateCssUrlToAbsolute(p_baseUrl, p_css) {
        let replaceCssUrl = function(baseUrl, match, p1, offset, str) {
            if (Utils.getUrlScheme(p1)) {
                return match;
            }

            let url = baseUrl + '/' + p1;
            return "url(\"" + url + "\");";
        };

        return p_css.replace(/\burl\(\"([^\"\)]+)\"\);/g, replaceCssUrl.bind(undefined, p_baseUrl));
    }

    static getUrlScheme(p_url) {
        let idx = p_url.indexOf(':');
        if (idx > -1) {
            return p_url.substr(0, idx);
        } else {
            return null;
        }
    }
}
