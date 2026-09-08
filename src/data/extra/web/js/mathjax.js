class MathRenderer extends VxWorker {
    constructor() {
        super();
        this.name = 'math';
        this.renderer = window.vxOptions.mathRenderer === 'mathjax' ? 'mathjax' : 'katex';
        this.initialization = null;
        this.rasterInitialization = null;
        this.langs = ['mathjax'];
    }

    registerInternal() {
        this.vxcore.on('basicMarkdownRendered', () => {
            this.render(this.vxcore.contentContainer, 'tex-to-render');
        });
        this.vxcore.getWorker('markdownit').addLangsToSkipHighlight(this.langs);
    }

    initialize() {
        if (!this.initialization) {
            this.initialization = Promise.resolve().then(() => {
                if (this.renderer === 'mathjax') {
                    window.MathJax = {
                        tex: {
                            inlineMath: [['$', '$'], ['\\(', '\\)']],
                            processEscapes: true,
                            tags: 'ams'
                        },
                        options: {
                            processHtmlClass: 'tex2jax_process|language-mathjax|lang-mathjax'
                        },
                        startup: { typeset: false },
                        svg: {
                            fontCache: 'local',
                            scale: window.vxOptions.mathJaxScale > 0 ? window.vxOptions.mathJaxScale : 1
                        }
                    };
                    const script = window.vxOptions.mathJaxScript
                        || 'https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-svg.js';
                    return new Promise((resolve, reject) => {
                        Utils.loadScript(script, () => {
                            if (!window.MathJax || !window.MathJax.startup
                                || !window.MathJax.startup.promise) {
                                reject(new Error('MathJax startup unavailable'));
                                return;
                            }
                            Promise.resolve(window.MathJax.startup.promise).then(() => {
                                if (typeof window.MathJax.typesetPromise !== 'function'
                                    || typeof window.MathJax.tex2svg !== 'function'
                                    || typeof window.MathJax.texReset !== 'function'
                                    || typeof window.MathJax.getMetricsFor !== 'function') {
                                    throw new Error('MathJax API unavailable');
                                }
                            }).then(resolve, reject);
                        });
                    });
                }

                const base = 'https://cdn.jsdelivr.net/npm/katex@0.16.22/dist/';
                const script = new Promise((resolve, reject) => {
                    Utils.loadScript(base + 'katex.min.js', () => {
                        if (window.katex && typeof window.katex.render === 'function') {
                            resolve();
                        } else {
                            reject(new Error('KaTeX API unavailable'));
                        }
                    });
                });
                const stylesheet = new Promise((resolve, reject) => {
                    const url = base + 'katex.min.css';
                    Utils.httpGet(url, 'text', (css) => {
                        try {
                            if (!css || !css.trim()) {
                                throw new Error('KaTeX stylesheet unavailable');
                            }
                            const style = document.createElement('style');
                            style.textContent = css.replace(/url\(\s*(?:"([^"]*)"|'([^']*)'|([^)]*?))\s*\)/gi,
                                (match, quoted, single, bare) => {
                                    const target = quoted !== undefined ? quoted
                                        : (single !== undefined ? single : bare.trim());
                                    return 'url("' + new URL(target, url).href + '")';
                                });
                            document.head.appendChild(style);
                            resolve();
                        } catch (error) {
                            reject(error);
                        }
                    });
                });
                return Promise.all([script, stylesheet]);
            }).catch((error) => {
                console.error('failed to initialize math renderer', this.renderer, error);
                throw error;
            });
        }
        return this.initialization;
    }

    // A reading pass owes one completion, independent of any simultaneous previews.
    render(p_node, p_className) {
        return Promise.resolve().then(() => {
            const extraNodes = this.vxcore.getWorker('markdownit').getCodeNodes(this.langs);
            this.transformExtraNodes(p_node, p_className, extraNodes);
            const nodes = Array.from(p_node.getElementsByClassName(p_className));
            if (!nodes.length) {
                return;
            }
            return this.initialize().then(() => {
                if (this.renderer === 'mathjax') {
                    window.MathJax.texReset();
                    return window.MathJax.typesetPromise(nodes);
                }
                const macros = {};
                nodes.forEach((node) => {
                    try {
                        const check = this.removeTextGuard(node.textContent);
                        if (check) {
                            this.renderKatex(node, check, macros);
                        }
                    } catch (error) {
                        console.error('failed to render KaTeX', error);
                    }
                });
                return document.fonts.ready;
            });
        }).catch((error) => {
            console.error('failed to render math', this.renderer, error);
        }).then(() => this.finishWork());
    }

    renderKatex(p_node, p_check, p_macros) {
        window.katex.render(p_check.text, p_node, {
            displayMode: p_check.display,
            output: 'htmlAndMathml',
            throwOnError: false,
            trust: false,
            macros: p_macros
        });
    }

    // Returns MathJax's SVG or an attached KaTeX wrapper owned by the preview caller.
    renderText(p_container, p_text, p_callback) {
        let wrapper = null;
        return this.initialize().then(() => {
            const check = this.removeTextGuard(p_text);
            if (!check) {
                return null;
            }
            if (this.renderer === 'mathjax') {
                const options = window.MathJax.getMetricsFor(p_container, check.display);
                window.MathJax.texReset();
                return window.MathJax.tex2svg(check.text, options).firstElementChild;
            }
            wrapper = document.createElement('span');
            const style = window.getComputedStyle(this.vxcore.contentContainer);
            wrapper.style.display = 'inline-block';
            wrapper.style.color = style.color;
            wrapper.style.font = style.font;
            wrapper.style.background = 'transparent';
            wrapper.style.padding = '2px';
            p_container.appendChild(wrapper);
            this.renderKatex(wrapper, check, {});
            const display = wrapper.querySelector('.katex-display');
            if (display) {
                display.style.margin = '0';
            }
            return wrapper;
        }).catch((error) => {
            if (wrapper && wrapper.parentNode) {
                wrapper.parentNode.removeChild(wrapper);
            }
            console.error('failed to preview math', this.renderer, error);
            return null;
        }).then(p_callback).catch((error) => {
            console.error('failed to deliver math preview', error);
        });
    }

    transformExtraNodes(p_node, p_className, p_extraNodes) {
        p_extraNodes.forEach((node) => {
            MathRenderer.transformNode(node, p_className);
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

    initializeRasterizer() {
        if (!this.rasterInitialization) {
            this.rasterInitialization = new Promise((resolve, reject) => {
                Utils.loadScript('https://cdn.jsdelivr.net/npm/html-to-image@1.11.13/dist/html-to-image.js', () => {
                    if (window.htmlToImage && typeof window.htmlToImage.toSvg === 'function'
                        && typeof window.htmlToImage.getFontEmbedCSS === 'function') {
                        resolve();
                    } else {
                        reject(new Error('HTML rasterizer unavailable'));
                    }
                });
            });
        }
        return this.rasterInitialization;
    }

    rasterizeHtml(p_node, p_pixelRatio) {
        let width, height;
        return this.initializeRasterizer().then(() => document.fonts.ready).then(() => {
            const rect = p_node.getBoundingClientRect();
            width = Math.ceil(Math.max(rect.width, p_node.scrollWidth));
            height = Math.ceil(Math.max(rect.height, p_node.scrollHeight));
            if (!width || !height) {
                throw new Error('Empty math raster bounds');
            }
            // In 1.11.13 preferredFontFormat's shared regex drops alternating font
            // sources. Keep all formats so every used face is embedded reliably.
            return window.htmlToImage.getFontEmbedCSS(p_node);
        }).then((fontCSS) => {
            // The dependency can resolve a failed fetch with url(""). Never emit a
            // successful PNG with missing fonts, or leave external URLs in the SVG.
            const urls = /url\(\s*(?:"([^"]*)"|'([^']*)'|([^)]*?))\s*\)/gi;
            let match;
            while ((match = urls.exec(fontCSS)) !== null) {
                const url = match[1] !== undefined ? match[1]
                    : (match[2] !== undefined ? match[2] : match[3].trim());
                if (!/^data:[^,]+,.+/i.test(url)) {
                    throw new Error('Unresolved math raster font');
                }
            }
            return window.htmlToImage.toSvg(p_node, {
                width: width,
                height: height,
                fontEmbedCSS: fontCSS,
                style: { display: 'inline-block', margin: '0' },
                filter: (node) => !node.classList || !node.classList.contains('katex-mathml')
            });
        }).then((uri) => new Promise((resolve, reject) => {
            // toPng/toCanvas wait for requestAnimationFrame, which is suspended in
            // hidden edit-only WebEngine pages. Load the self-contained SVG directly.
            SvgToImage.loadImage(uri, { crossOrigin: 'Anonymous' }, (error, image) => {
                if (error) {
                    reject(error);
                    return;
                }
                try {
                    const canvas = document.createElement('canvas');
                    canvas.width = Math.ceil(width * p_pixelRatio);
                    canvas.height = Math.ceil(height * p_pixelRatio);
                    const context = canvas.getContext('2d');
                    context.drawImage(image, 0, 0, canvas.width, canvas.height);
                    const dataUrl = canvas.toDataURL('image/png');
                    if (!dataUrl.startsWith('data:image/png;base64,')) {
                        throw new Error('Empty math raster image');
                    }
                    resolve({ dataUrl: dataUrl, width: width, height: height });
                } catch (error) {
                    reject(error);
                }
            });
        }));
    }

    // Renderer-neutral export hook; ordinary HTML retains accessible live math.
    prepareForExport(p_options) {
        if (!p_options || !p_options.rasterizeMath) {
            return null;
        }
        if (this.renderer === 'mathjax') {
            return new Promise((resolve) => this.convertAllSvgToPng(resolve));
        }
        const container = this.vxcore.contentContainer;
        const roots = container ? Array.from(container.querySelectorAll('.tex-to-render .katex')) : [];
        const scale = Math.max(2, window.devicePixelRatio || 1);
        return Promise.all(roots.filter((root) => !root.parentElement.closest('.katex')).map((root) => {
            let verticalAlign = null;
            return Promise.resolve(document.fonts.ready).then(() => {
                if (!root.closest('.katex-display')) {
                    const marker = document.createElement('span');
                    marker.style.cssText = 'display:inline-block;width:0;height:0;padding:0;margin:0;vertical-align:baseline';
                    root.parentNode.insertBefore(marker, root.nextSibling);
                    verticalAlign = marker.getBoundingClientRect().bottom - root.getBoundingClientRect().bottom;
                    marker.parentNode.removeChild(marker);
                }
                return this.rasterizeHtml(root, scale);
            }).then((raster) => {
                const image = document.createElement('img');
                image.src = raster.dataUrl;
                image.setAttribute('data-math-png', 'true');
                image.style.width = raster.width + 'px';
                image.style.height = raster.height + 'px';
                if (verticalAlign !== null) {
                    image.style.verticalAlign = verticalAlign + 'px';
                }
                if (root.parentNode) {
                    root.parentNode.replaceChild(image, root);
                }
            }).catch((error) => console.error('failed to rasterize KaTeX', error));
        }));
    }

    // Rasterize MathJax's SVG output to PNG before PDF export, then call p_done exactly once.
    convertAllSvgToPng(p_done) {
        let done = typeof p_done === 'function' ? p_done : function () {};
        let container = this.vxcore.contentContainer;
        // Only the root <svg> of each equation (the direct child of
        // mjx-container). A nested inner <svg> (e.g. MathJax's data-table for
        // matrices/aligned equations) must NOT be rasterized on its own: it is
        // captured as part of the root and, sized in relative units, scales to
        // fill the root's pinned viewport below.
        let svgs = container ? container.querySelectorAll('mjx-container > svg') : [];
        if (svgs.length == 0) {
            done();
            return;
        }

        let pending = svgs.length;
        // Runs exactly once per SVG so a failure on one equation can never
        // strand the counter and hang the export.
        let finalizeOne = function () {
            if (--pending == 0) {
                done();
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
                            let mjxContainer = svg.parentNode;
                            svg.parentNode.replaceChild(pngImg, svg);
                            // MathJax also emits a hidden <mjx-assistive-mml> (MathML) sibling.
                            // Tools that read MathML (e.g. Pandoc -> docx) would turn it into a
                            // second, native equation alongside our PNG, duplicating the formula.
                            // Drop it so only the rasterized image remains.
                            let assistiveMml = mjxContainer.querySelector('mjx-assistive-mml');
                            if (assistiveMml) {
                                assistiveMml.parentNode.removeChild(assistiveMml);
                            }
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

window.vxcore.registerWorker(new MathRenderer());
