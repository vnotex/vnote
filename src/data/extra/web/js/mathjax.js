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

    // Rasterize MathJax's SVG output to PNG before PDF export.
    //
    // Qt WebEngine's printToPdf renders MathJax's <use>-referenced SVG glyphs
    // with the wrong font, so each equation is flattened to a PNG first (issue
    // #2681). The scope is deliberately limited to MathJax containers: other
    // inline SVGs (Mermaid, Graphviz, Flowchart, WaveDrom, ...) must stay
    // vector, and some of them (e.g. Mermaid's <foreignObject>) taint the
    // canvas, which would make toDataURL() throw.
    convertAllSvgToPng() {
        let container = this.vxcore.contentContainer;
        // Only the root <svg> of each equation (the direct child of
        // mjx-container). A nested inner <svg> (e.g. MathJax's data-table for
        // matrices/aligned equations) must NOT be rasterized on its own: it is
        // captured as part of the root and, sized in relative units, scales to
        // fill the root's pinned viewport below.
        let svgs = container ? container.querySelectorAll('mjx-container > svg') : [];
        if (svgs.length == 0) {
            window.vxMarkdownAdapter.onPdfRenderReady();
            return;
        }

        let pending = svgs.length;
        // Runs exactly once per SVG so a failure on one equation can never
        // strand the counter and hang the export.
        let finalizeOne = function () {
            if (--pending == 0) {
                window.vxMarkdownAdapter.onPdfRenderReady();
            }
        };

        // At least 2x, higher on Hi-DPI displays, to keep the raster crisp.
        let scale = Math.max(2, window.devicePixelRatio || 1);
        svgs.forEach(function (svg) {
            let url = null;
            try {
                // Measure the live, laid-out viewport first. The export body is
                // sized to the PDF page (markdownviewercore.js setBodySize), so
                // this is the true CSS-pixel box the equation occupies.
                let bbox = svg.getBoundingClientRect();
                let width = Math.max(1, Math.ceil(bbox.width));
                let height = Math.max(1, Math.ceil(bbox.height));

                // Serialize a clone so the live DOM stays untouched until the
                // final replaceChild in the onload handler below.
                let clone = svg.cloneNode(true);
                // Inline the resolved color so `fill: currentColor` keeps the
                // on-screen color once the SVG is loaded as a standalone image.
                // Read it from the LIVE node: a detached clone reports empty.
                clone.style.color = window.getComputedStyle(svg).color;
                // Pin the measured viewport onto the clone. MathJax's root <svg>
                // can be sized in page-relative units (e.g. width="100%" with a
                // height in ex) whose only containing block is the live page;
                // loaded as a standalone image those collapse and squeeze the
                // equation. Pinning the measured CSS pixels recreates the exact
                // on-page viewport so the raster matches the screen.
                clone.setAttribute('width', width + 'px');
                clone.setAttribute('height', height + 'px');
                let svgStr = new XMLSerializer().serializeToString(clone);

                let canvas = document.createElement('canvas');
                canvas.width = width * scale;
                canvas.height = height * scale;
                let ctx = canvas.getContext('2d');

                let svgBlob = new Blob([svgStr], { type: 'image/svg+xml;charset=utf-8' });
                url = URL.createObjectURL(svgBlob);

                let img = new Image();
                img.onload = function () {
                    try {
                        ctx.drawImage(img, 0, 0, canvas.width, canvas.height);

                        let pngImg = document.createElement('img');
                        pngImg.src = canvas.toDataURL('image/png');
                        pngImg.style.width = width + 'px';
                        pngImg.style.height = height + 'px';
                        pngImg.setAttribute('data-math-png', 'true');

                        if (svg.parentNode) {
                            svg.parentNode.replaceChild(pngImg, svg);
                        }
                    } catch (err) {
                        console.error('failed to rasterize MathJax SVG', err);
                    } finally {
                        URL.revokeObjectURL(url);
                        finalizeOne();
                    }
                };
                img.onerror = function () {
                    URL.revokeObjectURL(url);
                    finalizeOne();
                };
                img.src = url;
            } catch (err) {
                console.error('failed to prepare MathJax SVG for rasterization', err);
                if (url) {
                    URL.revokeObjectURL(url);
                }
                finalizeOne();
            }
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
