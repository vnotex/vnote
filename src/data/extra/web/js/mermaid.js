class Mermaid extends GraphRenderer {
    constructor() {
        super();

        this.name = 'mermaid';

        this.graphDivClass = 'vx-mermaid-graph';

        this.extraScripts = [this.scriptFolderPath + '/mermaid/mermaid.min.js'];

        // default/dark/forest/neutral.
        this.theme = 'default';

        this.langs = ['mermaid'];
    }

    initialize(p_callback) {
        return super.initialize(() => {
           mermaid.initialize({
               startOnLoad: false,
               theme: this.theme
           });
            p_callback();
        });
    }

    // Render @p_node as Mermaid graph.
    // Return true on success.
    async renderOne(p_node, p_idx) {
        let graphSvg = null;
        try {
            const { svg } = await mermaid.render('vx-mermaid-graph-' + p_idx,
                                                 p_node.textContent);
            graphSvg = svg;
        } catch (p_err) {
            console.error('failed to render Mermaid', p_err);
            // Clean the container element, or Mermaid won't render the graph with
            // the same id.
            let graphNode = document.getElementById('vx-mermaid-graph-' + p_idx);
            if (graphNode) {
                let parentNode = graphNode.parentElement;
                parentNode.outerHTML = '';
                delete graphNode.parentElement;
            }
            this.finishRenderingOne();
            return false;
        }

        if (!graphSvg) {
            this.finishRenderingOne();
            return false;
        }

        let graphDiv = document.createElement('div');
        graphDiv.classList.add(this.graphDivClass);
        // Keep the graph source: the PDF export re-renders the diagram with SVG (instead of HTML)
        // labels before rasterizing it. See rasterizeAllForExport().
        graphDiv.setAttribute('data-mermaid-src', p_node.textContent);
        try {
            graphDiv.innerHTML = graphSvg;
            window.vxImageViewer.setupSVGToView(graphDiv.children[0], true);
        } catch (p_err) {
            console.error('incorrect graph SVG definition', p_err);
            this.finishRenderingOne();
            return false;
        }

        Utils.checkSourceLine(p_node, graphDiv);

        Utils.replaceNodeWithPreCheck(p_node, graphDiv);

        this.finishRenderingOne();
        return true;
    }

    // vxcore.prepareForExport() hook. Returns a Promise, or null when there is nothing to do.
    prepareForExport(p_options) {
        if (!p_options || !p_options.rasterizeDiagrams) {
            return null;
        }

        return this.rasterizeAllForExport(p_options);
    }

    // Replace every rendered diagram with a PNG raster of itself.
    //
    // Only used by the wkhtmltopdf export route. wkhtmltopdf's QtWebKit re-shapes the SVG text
    // with a substituted font (it has no per-glyph fallback, and cannot load .ttc fonts at all),
    // while the geometry around it - box sizes, tspan positions - was computed here with the real
    // font. Labels then overflow their boxes and get clipped. It also drops some long/complex
    // paths outright. Rasterizing here makes the diagram immune to both: wkhtmltopdf only ever
    // sees pixels.
    //
    // An SVG loaded as an <img> does not render <foreignObject>, and Mermaid's default HTML labels
    // are exactly that, so each diagram is first re-rendered from its source with htmlLabels off.
    // A diagram without a stored source is left untouched (vector), which is strictly no worse
    // than before.
    //
    // p_options.maxWidthPx / .maxHeightPx are the PRINTED box (0 = unconstrained) and
    // p_options.rasterScale the device pixels per CSS pixel to render at. Rasterizing straight
    // into the printed box matters: sizing the raster from the on-screen box instead leaves the
    // CSS clamp to resample it a second time, which is what makes diagrams look soft.
    async rasterizeAllForExport(p_options) {
        let divs = document.querySelectorAll('div.' + this.graphDivClass + '[data-mermaid-src]');
        if (divs.length == 0) {
            return;
        }

        let options = p_options || {};
        let scale = options.rasterScale > 0 ? options.rasterScale
                                            : Math.max(2, window.devicePixelRatio || 1);
        for (let i = 0; i < divs.length; ++i) {
            try {
                await this.rasterizeOneForExport(divs[i], i, scale, options);
            } catch (p_err) {
                console.error('failed to rasterize Mermaid graph', p_err);
            }
        }
    }

    async rasterizeOneForExport(p_div, p_idx, p_scale, p_options) {
        const src = p_div.getAttribute('data-mermaid-src');
        if (!src) {
            return;
        }

        // Force SVG labels for this render only, via an init directive, so the live viewer
        // configuration is left alone.
        const directive = '%%{init: {"flowchart": {"htmlLabels": false}, '
                          + '"class": {"htmlLabels": false}, "htmlLabels": false} }%%\n';
        const { svg } = await mermaid.render('vx-mermaid-graph-export-' + p_idx, directive + src);
        if (!svg) {
            return;
        }

        let holder = document.createElement('div');
        holder.innerHTML = svg;
        let svgNode = holder.children[0];
        if (!svgNode) {
            return;
        }

        // The on-page size of the diagram being replaced, as the starting point.
        let oldSvg = p_div.querySelector('svg');
        let box = oldSvg ? oldSvg.getBoundingClientRect() : p_div.getBoundingClientRect();
        let vb = (svgNode.getAttribute('viewBox') || '').split(/[\s,]+/);
        let vbW = vb.length >= 4 ? parseFloat(vb[2]) : 0;
        let vbH = vb.length >= 4 ? parseFloat(vb[3]) : 0;
        let width = Math.max(1, Math.ceil(box.width > 0 ? box.width : vbW));
        let height = Math.max(1, Math.ceil(box.height > 0 ? box.height
                                                          : (vbW > 0 ? vbH * width / vbW : vbH)));
        if (vbW > 0 && vbH > 0) {
            // Keep the aspect ratio of the freshly rendered diagram: SVG labels are not laid out
            // exactly like the HTML ones, so its natural height may differ from the old one's.
            height = Math.max(1, Math.round(width * vbH / vbW));
        }

        // Shrink to the printed box BEFORE rasterizing, so the raster is produced at exactly the
        // size it will be printed at (same clamp as the injected size-fix script, which then has
        // nothing left to do).
        let options = p_options || {};
        if (options.maxWidthPx > 0 && width > options.maxWidthPx) {
            height = Math.max(1, Math.round(height * options.maxWidthPx / width));
            width = options.maxWidthPx;
        }
        if (options.maxHeightPx > 0 && height > options.maxHeightPx) {
            width = Math.max(1, Math.round(width * options.maxHeightPx / height));
            height = options.maxHeightPx;
        }

        // Keep the canvas within what the browser will actually allocate. Chromium refuses very
        // large canvases silently (toDataURL then throws or returns a blank image), so cap the
        // longest side and the total area, preserving the aspect ratio.
        const c_maxSide = 8192;
        const c_maxArea = 40000000;
        let scale = p_scale > 0 ? p_scale : 2;
        scale = Math.min(scale, c_maxSide / Math.max(width, height));
        scale = Math.min(scale, Math.sqrt(c_maxArea / (width * height)));
        scale = Math.max(1, scale);

        const devWidth = Math.max(1, Math.round(width * scale));
        const devHeight = Math.max(1, Math.round(height * scale));

        // An SVG loaded as an image has no containing block, so `width="100%"` collapses.
        svgNode.setAttribute('width', devWidth + 'px');
        svgNode.setAttribute('height', devHeight + 'px');
        svgNode.style.maxWidth = 'none';
        const svgStr = new XMLSerializer().serializeToString(svgNode);

        const dataUrl = await new Promise((resolve) => {
            let url = URL.createObjectURL(new Blob([svgStr],
                                                   { type: 'image/svg+xml;charset=utf-8' }));
            let img = new Image();
            img.onload = function () {
                let png = null;
                try {
                    let canvas = document.createElement('canvas');
                    canvas.width = devWidth;
                    canvas.height = devHeight;
                    let ctx = canvas.getContext('2d');
                    // The diagram is drawn on transparent background otherwise, which turns into
                    // black in some PDF viewers.
                    ctx.fillStyle = '#ffffff';
                    ctx.fillRect(0, 0, canvas.width, canvas.height);
                    ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
                    png = canvas.toDataURL('image/png');
                } catch (p_err) {
                    console.error('failed to rasterize Mermaid SVG', p_err);
                } finally {
                    URL.revokeObjectURL(url);
                }
                resolve(png);
            };
            img.onerror = function () {
                URL.revokeObjectURL(url);
                resolve(null);
            };
            img.src = url;
        });

        if (!dataUrl) {
            return;
        }

        let pngImg = document.createElement('img');
        pngImg.src = dataUrl;
        pngImg.style.width = width + 'px';
        pngImg.style.height = height + 'px';
        pngImg.style.maxWidth = 'none';
        pngImg.setAttribute('data-mermaid-png', 'true');
        // Consumed by the size-fix script injected for wkhtmltopdf, which cannot measure the DOM.
        pngImg.setAttribute('data-mermaid-width', width);
        pngImg.setAttribute('data-mermaid-height', height);
        p_div.innerHTML = '';
        p_div.appendChild(pngImg);
    }

    // Render a graph from @p_text.
    // Will append a div to @p_container and return the div.
    async renderTextInternal(p_container, p_text, p_idx) {
        let graphSvg = null;
        try {
            const { svg } = await mermaid.render('vx-mermaid-graph-stand-alone-' + p_idx,
                                                 p_text);
            graphSvg = svg;
        } catch (p_err) {
            console.error('failed to render Mermaid', p_err);
            // Clean the container element, or Mermaid won't render the graph with
            // the same id.
            let graphNode = document.getElementById('vx-mermaid-graph-stand-alone-' + p_idx);
            if (graphNode) {
                let parentNode = graphNode.parentElement;
                parentNode.outerHTML = '';
                delete graphNode.parentElement;
            }
            return null;
        }

        if (!graphSvg) {
            return null;
        }

        let graphDiv = document.createElement('div');
        try {
            graphDiv.innerHTML = graphSvg;
        } catch (p_err) {
            console.error('incorrect graph SVG definition', p_err);
            return null;
        }

        p_container.appendChild(graphDiv);
        console.log(graphDiv);
        return graphDiv;
    }

    // p_callback(graphDiv).
    async renderText(p_container, p_text, p_idx, p_callback) {
        if (!this.initialize(async () => {
                let graphDiv = await this.renderTextInternal(p_container, p_text, p_idx);
                p_callback(graphDiv);
            })) {
            return;
        }

        let graphDiv = await this.renderTextInternal(p_container, p_text, p_idx);
        console.log(graphDiv);
        p_callback(graphDiv);
    }
}

window.vxcore.registerWorker(new Mermaid());
