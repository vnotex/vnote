class Utils {
    constructor() {
    }

    static parentFolder(p_path) {
        return p_path.substr(0, p_path.lastIndexOf('/'));
    }

    // @p_type: 'blob'/'text'.
    // p_callback(response). On a transport failure the callback is still invoked,
    // with null: a caller that never hears back would otherwise leave its render
    // pass permanently unfinished, which deadlocks the whole viewer (see
    // GraphRenderer.completePass). Callers must therefore handle a null response.
    static httpGet(p_url, p_type, p_callback) {
        let xmlHttp = new XMLHttpRequest();
        xmlHttp.open("GET", p_url);
        xmlHttp.responseType = p_type;

        let done = false;
        const finish = function(p_resp) {
            if (done) {
                return;
            }
            done = true;
            p_callback(p_resp);
        };

        xmlHttp.onload = function() { finish(xmlHttp.response); };
        xmlHttp.onerror = function() {
            console.error('request failed', p_url);
            finish(null);
        };
        xmlHttp.ontimeout = xmlHttp.onerror;
        xmlHttp.onabort = xmlHttp.onerror;

        xmlHttp.send(null);
    }

    static loadScript(p_src, p_callback) {
        let script = document.createElement('script');
        if (p_callback) {
            script.onload = p_callback;
            // Continue on failure too. The dependent renderer will then fail per
            // diagram - which is reported and completes its pass - instead of
            // silently never resuming and hanging the viewer forever.
            script.onerror = function() {
                console.error('failed to load script', p_src);
                p_callback();
            };
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

    // Rewrite every element id under @p_root by appending @p_suffix, and update
    // every reference to those ids. Mutates @p_root in place.
    //
    // Required before the SAME rendered SVG string is inserted into the document
    // more than once (i.e. whenever a cached artifact is reused). Renderers bake
    // per-render ids into their output and reference them internally with
    // url(#...), href="#...", ARIA id lists, and - for Mermaid - id selectors
    // inside an embedded <style>. Inserting the string verbatim twice produces
    // duplicate DOM ids, and every marker/gradient/filter/clip-path in the second
    // copy silently resolves to the FIRST copy's definition: subtly wrong diagrams
    // rather than an obvious failure. Replacing only the outer id is not enough.
    //
    // IN PLACE, on nodes already parsed by the caller, deliberately. Taking a
    // string and returning a string would mean parse -> serialize -> re-parse of
    // markup that the renderer sanitized exactly once, and that round trip is the
    // classic mutation-XSS amplifier: fragment serialization writes <style> text
    // back out unescaped, and SVG foreign content can re-parse into a different
    // tree. Keep this to a single parse - do not reintroduce an innerHTML round
    // trip here.
    //
    // Known limits: SMIL timing references ('begin="someId.click"') are not
    // rewritten, and an id that needs CSS escaping to appear in a selector (one
    // containing ':' say) is not matched inside an embedded <style>. Neither form
    // occurs in the output of the renderers that use this today.
    static renamespaceSvgIds(p_root, p_suffix) {
        if (!p_root || !p_suffix) {
            return;
        }

        let idNodes = p_root.querySelectorAll('[id]');
        if (idNodes.length === 0) {
            return;
        }

        let ids = [];
        let idSet = {};
        for (let i = 0; i < idNodes.length; ++i) {
            const id = idNodes[i].getAttribute('id');
            if (id) {
                ids.push(id);
                idSet[id] = true;
            }
        }
        if (ids.length === 0) {
            return;
        }

        // Longest first, so that '#a-1' can never be matched inside '#a-10'. The
        // trailing lookahead rejects a partial match on an id that merely shares a
        // prefix with a shorter one.
        ids.sort((p_a, p_b) => p_b.length - p_a.length);
        const escaped = ids.map((p_id) => p_id.replace(/[.*+?^${}()|[\]\\\-]/g, '\\$&'));
        const refRe = new RegExp('#(?:' + escaped.join('|') + ')(?![\\w\\-.])', 'g');
        const rewriteRefs = (p_text) => p_text.replace(refRe, (p_m) => p_m + p_suffix);

        // Attributes whose value is a whitespace-separated list of BARE ids, with no
        // leading '#'. Mermaid emits aria-labelledby / aria-describedby for accTitle
        // and accDescr, so missing these silently breaks the accessible name of every
        // diagram - including on the first, uncached render.
        const c_idListAttrs = ['aria-labelledby', 'aria-describedby', 'aria-owns',
                               'aria-controls', 'aria-flowto', 'aria-activedescendant',
                               'aria-details', 'aria-errormessage'];
        const rewriteIdList = (p_value) => p_value.split(/\s+/)
                                                  .map((p_id) => idSet[p_id] ? p_id + p_suffix
                                                                             : p_id)
                                                  .join(' ');

        let all = p_root.querySelectorAll('*');
        for (let i = 0; i < all.length; ++i) {
            const node = all[i];
            const attrs = node.attributes;
            for (let j = 0; j < attrs.length; ++j) {
                const attr = attrs[j];
                if (attr.name === 'id') {
                    node.setAttribute('id', attr.value + p_suffix);
                    continue;
                }
                if (c_idListAttrs.indexOf(attr.name) >= 0) {
                    const rewritten = rewriteIdList(attr.value);
                    if (rewritten !== attr.value) {
                        node.setAttribute(attr.name, rewritten);
                    }
                    continue;
                }
                if (attr.value.indexOf('#') >= 0) {
                    const rewritten = rewriteRefs(attr.value);
                    if (rewritten !== attr.value) {
                        node.setAttribute(attr.name, rewritten);
                    }
                }
            }

            // Mermaid scopes its whole stylesheet under '#<diagram id>'.
            if (node.tagName && node.tagName.toLowerCase() === 'style') {
                node.textContent = rewriteRefs(node.textContent);
            }
        }
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
