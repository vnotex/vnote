class GraphPreviewer {
    constructor(p_vnotex, p_container) {
        this.vnotex = p_vnotex;

        // Preview will take place here.
        this.container = p_container;

        this.flowchartJsIdx = 0;
        this.waveDromIdx = 0;
        this.mermaidIdx = 0;

        // Used to decide the width with 100% relative value.
        this.windowWidth = 800;

        window.addEventListener(
            'resize',
            () => {
                if (window.innerWidth > 0) {
                    this.windowWidth = window.innerWidth;
                }
            },
            { passive: true });
    }

    previewGraph(p_id, p_timeStamp, p_lang, p_text) {
        if (p_text.length == 0) {
            this.setGraphPreviewData(p_id, p_timeStamp);
            return;
        }

        if (p_lang === 'flow' || p_lang === 'flowchart') {
            this.vnotex.getWorker('flowchartjs').renderText(this.container,
                p_text,
                this.flowchartJsIdx++,
                (graphDiv) => {
                    this.processGraph(p_id, p_timeStamp, graphDiv);
                });
        } else if (p_lang === 'wavedrom') {
            this.vnotex.getWorker('wavedrom').renderText(this.container,
                p_text,
                this.waveDromIdx++,
                (graphDiv) => {
                    this.processGraph(p_id, p_timeStamp, graphDiv);
                });
        } else if (p_lang === 'mermaid') {
            this.vnotex.getWorker('mermaid').renderText(this.container,
                p_text,
                this.mermaidIdx++,
                (graphDiv) => {
                    this.fixSvgRelativeWidth(graphDiv.firstElementChild);
                    this.processGraph(p_id, p_timeStamp, graphDiv);
                });
        } else if (p_lang === 'puml' || p_lang === 'plantuml') {
            let func = function(p_previewer, p_id, p_timeStamp) {
                let previewer = p_previewer;
                let id = p_id;
                let timeStamp = p_timeStamp;
                return function(p_format, p_data) {
                    previewer.setGraphPreviewData(id, timeStamp, p_format, p_data, false, true);
                };
            };
            this.vnotex.getWorker('plantuml').renderText(p_text, func(this, p_id, p_timeStamp));
            return;
        } else if (p_lang === 'dot') {
            let func = function(p_previewer, p_id, p_timeStamp) {
                let previewer = p_previewer;
                let id = p_id;
                let timeStamp = p_timeStamp;
                return function(p_svgNode) {
                    previewer.setGraphPreviewData(id, timeStamp, 'svg', p_svgNode.outerHTML, false, true);
                };
            };
            this.vnotex.getWorker('graphviz').renderText(p_text, func(this, p_id, p_timeStamp));
            return;
        } else if (p_lang === 'mathjax') {
            this.renderMath(p_id, p_timeStamp, p_text, null);
            return;
        } else {
            this.setGraphPreviewData(p_id, p_timeStamp);
        }
    }

    renderMath(p_id, p_timeStamp, p_text, p_dataSetter) {
        let func = function(p_previewer, p_id, p_timeStamp) {
            let previewer = p_previewer;
            let id = p_id;
            let timeStamp = p_timeStamp;
            return function(p_svgNode) {
                previewer.fixSvgRelativeWidth(p_svgNode);
                previewer.processSvgAsPng(id, timeStamp, p_svgNode, p_dataSetter);
            };
        };
        this.vnotex.getWorker('mathjax').renderText(this.container,
                                                    p_text,
                                                    func(this, p_id, p_timeStamp));
    }

    processGraph(p_id, p_timeStamp, p_graphDiv) {
        if (!p_graphDiv) {
            console.error('failed to preview graph', p_id, p_timeStamp);
            this.setGraphPreviewData(p_id, p_timeStamp);
            return;
        }

        this.container.removeChild(p_graphDiv);

        this.processSvgAsPng(p_id, p_timeStamp, p_graphDiv.firstElementChild);
    }

    processSvgAsPng(p_id, p_timeStamp, p_svgNode, p_dataSetter = null) {
        if (!p_dataSetter) {
            p_dataSetter = this.setGraphPreviewData.bind(this);
        }
        if (!p_svgNode) {
            console.error('failed to preview graph', p_id, p_timeStamp);
            p_dataSetter(p_id, p_timeStamp);
            return;
        }

        this.scaleSvg(p_svgNode);

        SvgToImage.svgToImage(p_svgNode.outerHTML,
            { crossOrigin: 'Anonymous' },
            (p_err, p_image) => {
                if (p_err) {
                    p_dataSetter(p_id, p_timeStamp);
                    return;
                }

                let canvas = document.createElement('canvas');
                let ctx = canvas.getContext('2d');
                canvas.height = p_image.height;
                canvas.width = p_image.width;
                ctx.drawImage(p_image, 0, 0);
                let dataUrl = null;
                try {
                    dataUrl = canvas.toDataURL();
                } catch (err) {
                    // Tainted canvas may be caused by the <foreignObject> in SVG.
                    console.error('failed to draw image on canvas', err);

                    // Try simply using the SVG.
                    p_dataSetter(p_id, p_timeStamp, 'svg', p_svgNode.outerHTML, false, false);
                    return;
                }

                let png = dataUrl ? dataUrl.substring(dataUrl.indexOf(',') + 1) : '';
                p_dataSetter(p_id, p_timeStamp, 'png', png, true, false);
        });
    }

    previewMath(p_id, p_timeStamp, p_text) {
        // Do we need to go through TexMath plugin? I don't think so.
        this.renderMath(p_id, p_timeStamp, p_text, this.setMathPreviewData.bind(this));
    }

    // Fix SVG with width and height being '100%'.
    fixSvgRelativeWidth(p_svgNode) {
        if (p_svgNode.getAttribute('width').indexOf('%') != -1) {
            // Try maxWidth.
            if (p_svgNode.style.maxWidth && p_svgNode.style.maxWidth.endsWith('px')) {
                p_svgNode.setAttribute('width', p_svgNode.style.maxWidth);
            } else {
                // Set as window width.
                p_svgNode.setAttribute('width', Math.max(this.windowWidth - 100, 100) + 'px');
            }
        }
    }

    scaleSvg(p_svgNode) {
        let scaleFactor = window.devicePixelRatio;
        if (scaleFactor == 1) {
            return;
        }

        if (p_svgNode.getAttribute('width').indexOf('%') == -1) {
            p_svgNode.width.baseVal.valueInSpecifiedUnits *= scaleFactor;
        }
        if (p_svgNode.getAttribute('height').indexOf('%') == -1) {
            p_svgNode.height.baseVal.valueInSpecifiedUnits *= scaleFactor;
        }
    }

    setGraphPreviewData(p_id, p_timeStamp, p_format = '', p_data = '', p_base64 = false, p_needScale = false) {
        let previewData = {
            id: p_id,
            timeStamp: p_timeStamp,
            format: p_format,
            data: p_data,
            base64: p_base64,
            needScale: p_needScale
        };
        this.vnotex.setGraphPreviewData(previewData);
    }

    setMathPreviewData(p_id, p_timeStamp, p_format = '', p_data = '', p_base64 = false, p_needScale = false) {
        let previewData = {
            id: p_id,
            timeStamp: p_timeStamp,
            format: p_format,
            data: p_data,
            base64: p_base64,
            needScale: p_needScale
        };
        this.vnotex.setMathPreviewData(previewData);
    }
}
