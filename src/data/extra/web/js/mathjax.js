window.MathJax = {
    tex: {
        inlineMath: [['$', '$'], ['\\(', '\\)']],
        processEscapes: true,
        tags: 'ams'
    },
    options: {
        processHtmlClass: 'tex2jax_process|language-mathjax|lang-mathjax'
    },
    startup: {
        typeset: false,
        ready: function() {
            MathJax.startup.defaultReady();
            MathJax.startup.promise.then(() => {
                window.vxcore.getWorker('mathjax').setMathJaxReady();
            });
        }
    },
    svg: {
        // Make SVG self-contained.
        fontCache: 'local',
        scale: window.vxOptions.mathJaxScale > 0 ? window.vxOptions.mathJaxScale : 1
    }
};

class MathJaxRenderer extends VxWorker {
    constructor() {
        super();

        this.name = 'mathjax';

        this.initialized = false;

        this.nodesToRender = [];

        this.mathJaxScript = 'https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-svg.js';

        this.langs = ['mathjax'];

        // Will be called when MathJax is ready.
        this.readyCallback = function() {};
    }

    registerInternal() {
        this.vxcore.on('basicMarkdownRendered', () => {
            this.render(this.vxcore.contentContainer, 'tex-to-render');
        });

        this.vxcore.getWorker('markdownit').addLangsToSkipHighlight(this.langs);
    }

    initialize(p_callback) {
        if (this.initialized) {
            return true;
        }

        this.initialized = true;
        this.readyCallback = p_callback;
        if (!!window.vxOptions.mathJaxScript) {
            this.mathJaxScript = window.vxOptions.mathJaxScript;
            console.log('override MathJax script', this.mathJaxScript);
        }
        Utils.loadScript(this.mathJaxScript, null);
        return false;
    }

    setMathJaxReady() {
        this.readyCallback();
    }

    // Fetch all nodes of @p_className to render.
    // Will fetch extra code nodes, too.
    render(p_node, p_className) {
        this.nodesToRender = [];

        // Transform extra class nodes.
        let extraNodes = this.vxcore.getWorker('markdownit').getCodeNodes(this.langs);
        this.transformExtraNodes(p_node, p_className, extraNodes);

        // Collect nodes to render.
        let nodes = p_node.getElementsByClassName(p_className);
        if (nodes.length == 0) {
            this.finishWork();
            return;
        }

        this.nodesToRender = Array.from(nodes);

        if (!this.initialize(() => {
            this.renderNodes();
            })) {
            return;
        }

        this.renderNodes();
    }

    // p_callback(svgNode).
    renderText(p_container, p_text, p_callback) {
        let func = () => {
            // Check text and remove the guards.
            let check = this.removeTextGuard(p_text);
            if (!check) {
                p_callback(null);
                return;
            }
            let options = null;
            try {
                options = MathJax.getMetricsFor(p_container, check.display);
            } catch (err) {
                console.error('failed to render MathJax', err);
                p_callback(null);
                return;
            }

            let mathNode = null;
            try {
                mathNode = MathJax.tex2svg(check.text, options);
            } catch (err) {
                console.error('failed to render MathJax', err);
            }
            p_callback(mathNode ? mathNode.firstElementChild : null);
        };

        if (!this.initialize(func)) {
            return;
        }

        func();
    }

    transformExtraNodes(p_node, p_className, p_extraNodes) {
        p_extraNodes.forEach((node) => {
            MathJaxRenderer.transformNode(node, p_className);
        });
    }

    static transformNode(p_node, p_className) {
        // Replace it with <section><eqn></eqn></section>.
        let eqn = document.createElement('eqn');
        eqn.classList.add(p_className);
        eqn.textContent = p_node.textContent;

        let section = document.createElement('section');
        section.appendChild(eqn);

        Utils.replaceNodeWithPreCheck(p_node, section);
    }

    renderNodes() {
        if (this.nodesToRender.length > 0) {
            try {
                MathJax.texReset();
            } catch (err) {
                console.error('MathJax is not ready', err);
                this.postProcessMathJax();
                return;
            }

            MathJax.typesetPromise(this.nodesToRender)
                .then(() => {
                    this.postProcessMathJax();
                })
                .catch((err) => {
                    console.error('failed to render MathJax', err);
                    this.postProcessMathJax();
                });
        }
    }

    postProcessMathJax() {
        this.finishWork();
    }

    convertAllSvgToPng() {

        let svgs = this.vxcore.contentContainer.querySelectorAll('svg');
        if (svgs.length == 0) {
            window.vxMarkdownAdapter.onPdfRenderReady();
            return;
        }

        let pending = svgs.length;
        svgs.forEach(function (svg) {
            let serializer = new XMLSerializer();
            let svgStr = serializer.serializeToString(svg);
            let canvas = document.createElement('canvas');
            let bbox = svg.getBoundingClientRect();
            let scale = 2;
            canvas.width = bbox.width * scale;
            canvas.height = bbox.height * scale;
            let ctx = canvas.getContext('2d');

            let img = new Image();
            let svgBlob = new Blob([svgStr], { type: 'image/svg+xml;charset=utf-8' });
            let url = URL.createObjectURL(svgBlob);
            img.onload = function () {
                ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
                URL.revokeObjectURL(url);
                let pngDataUrl = canvas.toDataURL('image/png');

                let pngImg = document.createElement('img');
                pngImg.src = pngDataUrl;
                pngImg.style.width = bbox.width + 'px';
                pngImg.style.height = bbox.height + 'px';
                pngImg.setAttribute('data-math-png', 'true');

                svg.parentNode.replaceChild(pngImg, svg);

                pending--;
                if (pending == 0) {
                    window.vxMarkdownAdapter.onPdfRenderReady();
                }
            };
            img.onerror = function () {
                URL.revokeObjectURL(url);
                pending--;
                if (pending == 0) {
                    window.vxMarkdownAdapter.onPdfRenderReady();
                }
            };
            img.src = url;
        });
    }

    // Return { text, display }.
    removeTextGuard(p_text) {
        let text = p_text.trim();
        let display = false;

        if (text.startsWith('$$') && text.endsWith('$$')) {
            text = text.substring(2, text.length - 2);
            display = true;
        } else if (text.startsWith('$') && text.endsWith('$')) {
            text = text.substring(1, text.length - 1);
        } else if (text.match(/^\\begin\s*\{\S+}[\s\S]+\\end\s*\{\S+\}$/)) {
            display = true;
        } else {
            return null;
        }

        return { text: text, display: display };
    }
}

window.vxcore.registerWorker(new MathJaxRenderer());
