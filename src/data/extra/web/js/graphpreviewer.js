class GraphPreviewer {
    constructor(p_vxcore, p_container) {
        this.vxcore = p_vxcore;

        // Preview will take place here.
        this.container = p_container;

        this.flowchartJsIdx = 0;
        this.waveDromIdx = 0;
        this.mermaidIdx = 0;

        // Used to decide the width with 100% relative value.
        this.windowWidth = 800;

        this.firstPreview = true;

        this.currentColor = null;

        // Diagnostics only. See
        // .kilo/plans/1788310000000-trace-edit-mode-preview-perf.md.
        // Per-item values are kept in arrays and only one summary line is
        // printed, on a 2s idle timer: there is no "last node" to key off on the
        // edit path, unlike a read-mode GraphRenderer pass.
        this.perfStats = null;
        this.perfSummaryTimer = null;

        window.addEventListener(
            'resize',
            () => {
                if (window.innerWidth > 0) {
                    this.windowWidth = window.innerWidth;
                }
            },
            { passive: true });
    }

    // performance.now(), not Date.now(): Chromium clamps wall-clock timer
    // resolution in some configurations.
    perfNow() {
        if (typeof performance !== 'undefined' && performance.now) {
            return performance.now();
        }
        return Date.now();
    }

    // A generation is identified by its timestamp. A zoom or an edit starts a
    // new batch while the old one drains; keying on the timestamp keeps two
    // passes from being silently averaged together.
    perfStatsFor(p_timeStamp) {
        if (!this.perfStats || this.perfStats.timeStamp !== p_timeStamp) {
            this.perfStats = {
                timeStamp: p_timeStamp,
                start: this.perfNow(),
                startEpoch: Date.now(),
                requests: 0,
                renders: 0,
                rasters: 0,
                failures: 0,
                reported: false,
                incompleteReported: false,
                idleAttempts: 0,
                // Tracked separately: one combined counter cannot tell a render
                // backlog from a raster backlog.
                outstandingRequests: 0,
                outstandingRenders: 0,
                outstandingRasters: 0,
                maxOutstandingRequests: 0,
                maxOutstandingRenders: 0,
                maxOutstandingRasters: 0,
                renderMs: [],
                rasterizeMs: [],
                payloadChars: [],
                // id -> { renderStart, rasterStart }. A counter is only ever
                // moved for a request that was explicitly opened here, so a
                // path that bypasses one of the phases cannot leak or
                // double-count.
                pending: {},
                lastSend: 0
            };
        }
        return this.perfStats;
    }

    // Armed on COMPLETION only: arming it on request receipt would let a slow
    // first diagram print a summary of a batch that is still running.
    perfArmSummary() {
        if (this.perfSummaryTimer !== null) {
            clearTimeout(this.perfSummaryTimer);
        }
        this.perfSummaryTimer = setTimeout(() => {
            this.perfSummaryTimer = null;
            this.perfReport();
        }, 2000);
    }

    // @p_hasRenderPhase: false for PlantUml/Graphviz, which are served by an
    // external process and have no render callback to close.
    perfNoteRequest(p_timeStamp, p_id, p_hasRenderPhase) {
        try {
            let st = this.perfStatsFor(p_timeStamp);
            if (st.pending[p_id]) {
                // Same id re-issued within one generation; close the old record.
                delete st.pending[p_id];
                st.outstandingRequests = Math.max(0, st.outstandingRequests - 1);
            }
            st.pending[p_id] = {
                renderStart: p_hasRenderPhase ? this.perfNow() : 0,
                rasterStart: 0
            };
            st.requests++;
            st.outstandingRequests++;
            st.maxOutstandingRequests = Math.max(st.maxOutstandingRequests,
                                                 st.outstandingRequests);
            if (p_hasRenderPhase) {
                st.outstandingRenders++;
                st.maxOutstandingRenders = Math.max(st.maxOutstandingRenders,
                                                    st.outstandingRenders);
            }
        } catch (err) {
        }
    }

    perfPending(p_timeStamp, p_id) {
        if (!this.perfStats || this.perfStats.timeStamp !== p_timeStamp) {
            return null;
        }
        return this.perfStats.pending[p_id] || null;
    }

    perfNoteRender(p_timeStamp, p_id) {
        try {
            let rec = this.perfPending(p_timeStamp, p_id);
            if (!rec || !rec.renderStart) {
                return;
            }
            let st = this.perfStats;
            st.renders++;
            st.outstandingRenders = Math.max(0, st.outstandingRenders - 1);
            st.renderMs.push(this.perfNow() - rec.renderStart);
            rec.renderStart = 0;
        } catch (err) {
        }
    }

    perfNoteRasterStart(p_timeStamp, p_id) {
        try {
            let rec = this.perfPending(p_timeStamp, p_id);
            if (!rec || rec.rasterStart) {
                return;
            }
            let st = this.perfStats;
            st.outstandingRasters++;
            st.maxOutstandingRasters = Math.max(st.maxOutstandingRasters,
                                                st.outstandingRasters);
            rec.rasterStart = this.perfNow();
        } catch (err) {
        }
    }

    // Called from setGraphPreviewData, so every completion path of an opened
    // request is counted, including the failure paths that send empty data.
    perfNoteSend(p_timeStamp, p_id, p_chars) {
        try {
            let rec = this.perfPending(p_timeStamp, p_id);
            if (!rec) {
                return;
            }
            let st = this.perfStats;
            delete st.pending[p_id];
            st.outstandingRequests = Math.max(0, st.outstandingRequests - 1);
            if (rec.renderStart) {
                st.outstandingRenders = Math.max(0, st.outstandingRenders - 1);
            }
            if (rec.rasterStart) {
                st.outstandingRasters = Math.max(0, st.outstandingRasters - 1);
                st.rasters++;
                st.rasterizeMs.push(this.perfNow() - rec.rasterStart);
            }
            if (!p_chars) {
                st.failures++;
            }
            st.payloadChars.push(p_chars || 0);
            st.lastSend = this.perfNow();
            this.perfArmSummary();
        } catch (err) {
        }
    }

    // Nearest-rank on a sorted array: idx = min(n - 1, floor(p * n)).
    // The SAME rule is used by GraphRenderer.reportTiming() and by
    // PreviewHelper::perfQuartiles() in C++, because these three summaries are
    // meant to be read side by side - a p50 must mean one thing across all of
    // them. Change one, change all three.
    perfQuantiles(p_values) {
        if (!p_values || p_values.length === 0) {
            return 'n/a';
        }
        let vals = p_values.slice().sort((a, b) => a - b);
        let n = vals.length;
        let at = (p) => vals[Math.min(n - 1, Math.floor(p * n))];
        return 'min=' + vals[0].toFixed(1)
            + ' p50=' + at(0.5).toFixed(1)
            + ' p90=' + at(0.9).toFixed(1)
            + ' max=' + vals[n - 1].toFixed(1);
    }

    // Wrapped in try/catch for the reason spelled out in graphrenderer.js: a
    // half-updated %APPDATA%/web/js can leave an older class without a method
    // this calls, and diagnostics must never break the pipeline they measure.
    perfReport() {
        try {
            let st = this.perfStats;
            if (!st || st.reported || st.requests === 0) {
                return;
            }

            if (st.outstandingRequests > 0) {
                if (st.idleAttempts < 5) {
                    // Still draining (a slow PlantUml round trip, say). Wait for
                    // another idle interval rather than reporting a partial batch.
                    st.idleAttempts++;
                    this.perfArmSummary();
                    return;
                }
                // Something never came back. Report that as its own diagnostic,
                // and do NOT mark the generation reported: a late completion
                // still gets to produce the real summary.
                if (!st.incompleteReported) {
                    st.incompleteReported = true;
                    console.info('inplace graph timing INCOMPLETE ts=' + st.timeStamp
                        + ' requests=' + st.requests
                        + ' stillOutstanding=' + st.outstandingRequests);
                }
                return;
            }

            st.reported = true;
            let chars = st.payloadChars.reduce((a, b) => a + b, 0);
            // console.info, not console.log/warn/error: WebPage::javaScriptConsoleMessage
            // forwards only InfoMessageLevel into vnote.web.js.
            console.info('inplace graph timing ts=' + st.timeStamp
                + ' startEpochMs=' + st.startEpoch
                + ' requests=' + st.requests
                + ' renders=' + st.renders
                + ' rasters=' + st.rasters
                + ' failures=' + st.failures
                + ' stillOutstanding=' + st.outstandingRequests
                + ' spanMs=' + (st.lastSend - st.start).toFixed(1)
                + ' maxInFlight[requests=' + st.maxOutstandingRequests
                + ' renders=' + st.maxOutstandingRenders
                + ' rasters=' + st.maxOutstandingRasters + ']'
                + ' renderMs[' + this.perfQuantiles(st.renderMs) + ']'
                + ' rasterizeMs[' + this.perfQuantiles(st.rasterizeMs) + ']'
                + ' payloadChars=' + chars);
        } catch (err) {
        }
    }

    // Interface 1.
    // @p_scale: the editor zoom ratio, without any DPI factor.
    previewGraph(p_id, p_timeStamp, p_lang, p_text, p_scale = 1) {
        if (p_text.length == 0) {
            this.setGraphPreviewData(p_id, p_timeStamp);
            return;
        }

        this.initOnFirstPreview();

        if (p_lang === 'flow' || p_lang === 'flowchart') {
            this.perfNoteRequest(p_timeStamp, p_id, true);
            this.vxcore.getWorker('flowchartjs').renderText(this.container,
                p_text,
                this.flowchartJsIdx++,
                (graphDiv) => {
                    this.perfNoteRender(p_timeStamp, p_id);
                    this.processGraph(p_id, p_timeStamp, graphDiv, p_scale);
                });
        } else if (p_lang === 'wavedrom') {
            this.perfNoteRequest(p_timeStamp, p_id, true);
            this.vxcore.getWorker('wavedrom').renderText(this.container,
                p_text,
                this.waveDromIdx++,
                (graphDiv) => {
                    this.perfNoteRender(p_timeStamp, p_id);
                    this.processGraph(p_id, p_timeStamp, graphDiv, p_scale);
                });
        } else if (p_lang === 'mermaid') {
            this.perfNoteRequest(p_timeStamp, p_id, true);
            this.vxcore.getWorker('mermaid').renderText(this.container,
                p_text,
                this.mermaidIdx++,
                (graphDiv) => {
                    // Measured at this boundary rather than inside mermaid.js:
                    // Mermaid.renderText() has no cache, no concurrency limit
                    // and no yield, so the callback boundary IS the whole
                    // mermaid.render() cost for this request (H2).
                    this.perfNoteRender(p_timeStamp, p_id);
                    if (graphDiv) {
                        this.fixSvgRelativeWidth(graphDiv.firstElementChild);
                    }
                    this.processGraph(p_id, p_timeStamp, graphDiv, p_scale);
                });
        } else if (p_lang === 'puml' || p_lang === 'plantuml') {
            // No render phase: served by an external process, so there is no
            // in-page render callback to close.
            this.perfNoteRequest(p_timeStamp, p_id, false);
            let func = function(p_previewer, p_id, p_timeStamp) {
                let previewer = p_previewer;
                let id = p_id;
                let timeStamp = p_timeStamp;
                return function(p_format, p_data) {
                    // PlantUml preview is rendered to PNG (raster) rather than
                    // SVG: the popup renders SVG via Qt's QSvgRenderer, which
                    // does not support <foreignObject>/embedded HTML that
                    // PlantUml emits for some labels, yielding a blank preview.
                    // PNG from the server sidesteps that limitation.
                    previewer.setGraphPreviewData(id, timeStamp, p_format, p_data,
                                                  p_format === 'png', true);
                };
            };
            this.vxcore.getWorker('plantuml').renderText(p_text, func(this, p_id, p_timeStamp));
            return;
        } else if (p_lang === 'dot' || p_lang === 'graphviz') {
            this.perfNoteRequest(p_timeStamp, p_id, false);
            let func = function(p_previewer, p_id, p_timeStamp) {
                let previewer = p_previewer;
                let id = p_id;
                let timeStamp = p_timeStamp;
                return function(p_svgNode) {
                    if (!p_svgNode) {
                        console.warn('failed to preview graph', id, timeStamp);
                        previewer.setGraphPreviewData(id, timeStamp);
                        return;
                    }
                    previewer.setGraphPreviewData(id, timeStamp, 'svg', p_svgNode.outerHTML, false, true);
                };
            };
            this.vxcore.getWorker('graphviz').renderText(p_text, func(this, p_id, p_timeStamp));
            return;
        } else if (p_lang === 'mathjax') {
            // Completes through setGraphPreviewData (processSvgAsPng's default
            // setter), so it is counted as a request with a raster phase only.
            this.perfNoteRequest(p_timeStamp, p_id, false);
            this.renderMath(p_id, p_timeStamp, p_text, null, p_scale);
            return;
        } else {
            this.setGraphPreviewData(p_id, p_timeStamp);
        }
    }

    // Interface 2.
    // @p_scale: the editor zoom ratio, without any DPI factor.
    previewMath(p_id, p_timeStamp, p_text, p_scale = 1) {
        if (p_text.length == 0) {
            this.setMathPreviewData(p_id, p_timeStamp);
            return;
        }

        this.initOnFirstPreview();

        // Do we need to go through TexMath plugin? I don't think so.
        this.renderMath(p_id, p_timeStamp, p_text, this.setMathPreviewData.bind(this), p_scale);
    }

    initOnFirstPreview() {
        if (this.firstPreview) {
            this.firstPreview = false;

            let contentStyle = window.getComputedStyle(this.vxcore.contentContainer);
            this.currentColor = contentStyle.getPropertyValue('color');
            console.log('currentColor', this.currentColor);
        }
    }

    renderMath(p_id, p_timeStamp, p_text, p_dataSetter, p_scale = 1) {
        let func = function(p_previewer, p_id, p_timeStamp, p_scale) {
            let previewer = p_previewer;
            let id = p_id;
            let timeStamp = p_timeStamp;
            let scale = p_scale;
            return function(p_svgNode) {
                previewer.fixSvgCurrentColor(p_svgNode);
                previewer.fixSvgRelativeWidth(p_svgNode);
                previewer.processSvgAsPng(id, timeStamp, p_svgNode, p_dataSetter, scale);
            };
        };
        this.vxcore.getWorker('mathjax').renderText(this.container,
                                                    p_text,
                                                    func(this, p_id, p_timeStamp, p_scale));
    }

    processGraph(p_id, p_timeStamp, p_graphDiv, p_scale = 1) {
        if (!p_graphDiv) {
            console.error('failed to preview graph', p_id, p_timeStamp);
            this.setGraphPreviewData(p_id, p_timeStamp);
            return;
        }

        this.container.removeChild(p_graphDiv);

        this.processSvgAsPng(p_id, p_timeStamp, p_graphDiv.firstElementChild, null, p_scale);
    }

    processSvgAsPng(p_id, p_timeStamp, p_svgNode, p_dataSetter = null, p_scale = 1) {
        // Math previews carry their own id/timestamp namespace and complete via
        // setMathPreviewData, which is not instrumented; only the graph path
        // (no explicit setter) participates in the raster accounting.
        const isGraphPath = !p_dataSetter;
        if (!p_dataSetter) {
            p_dataSetter = this.setGraphPreviewData.bind(this);
        }
        if (!p_svgNode) {
            console.warn('failed to preview graph', p_id, p_timeStamp);
            p_dataSetter(p_id, p_timeStamp);
            return;
        }

        this.scaleSvg(p_svgNode, p_scale);

        if (isGraphPath) {
            this.perfNoteRasterStart(p_timeStamp, p_id);
        }

        // Serialize as well-formed XML rather than using outerHTML. In an HTML
        // document, outerHTML emits void/empty elements without a self-closing
        // slash (e.g. Mermaid's <br> inside a <foreignObject> label), which is
        // not valid XML and makes the browser fail to load the SVG as an image,
        // leaving the popup preview blank. XMLSerializer self-closes them.
        let svgString = new XMLSerializer().serializeToString(p_svgNode);
        SvgToImage.svgToImage(svgString,
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

    // Fix SVG with width and height being '100%'.
    fixSvgRelativeWidth(p_svgNode) {
        if (!p_svgNode) {
            return;
        }

        let width = p_svgNode.getAttribute('width');
        if (width === null) {
            return;
        }
        if (width.indexOf('%') != -1) {
            // Try maxWidth.
            if (p_svgNode.style.maxWidth && p_svgNode.style.maxWidth.endsWith('px') && p_svgNode.style.maxWidth != "0px") {
                p_svgNode.setAttribute('width', p_svgNode.style.maxWidth);
            } else {
                // Set as window width.
                p_svgNode.setAttribute('width', Math.max(this.windowWidth - 100, 100) + 'px');
            }
        }
    }

    // Fix SVG with stroke="currentColor" and fill="currentColor".
    fixSvgCurrentColor(p_svgNode) {
        let currentColor = this.currentColor;
        if (currentColor && p_svgNode) {
            let nodes = p_svgNode.querySelectorAll("g[fill='currentColor']");
            for (let i = 0; i < nodes.length; ++i) {
                let node = nodes[i];
                if (node.getAttribute('stroke') === 'currentColor') {
                    node.setAttribute('stroke', currentColor);
                }
                if (node.getAttribute('fill') === 'currentColor') {
                    node.setAttribute('fill', currentColor);
                }
            }
        }
    }

    scaleSvg(p_svgNode, p_scale = 1) {
        let scaleFactor = window.devicePixelRatio * (p_scale || 1);
        if (scaleFactor == 1 || !p_svgNode) {
            return;
        }

        let width = p_svgNode.getAttribute('width')
        if (width && width.indexOf('%') == -1) {
            p_svgNode.width.baseVal.valueInSpecifiedUnits *= scaleFactor;
        }
        let height = p_svgNode.getAttribute('height')
        if (height && height.indexOf('%') == -1) {
            p_svgNode.height.baseVal.valueInSpecifiedUnits *= scaleFactor;
        }
    }

    setGraphPreviewData(p_id, p_timeStamp, p_format = '', p_data = '', p_base64 = false, p_needScale = false) {
        this.perfNoteSend(p_timeStamp, p_id, p_data ? p_data.length : 0);
        let previewData = {
            id: p_id,
            timeStamp: p_timeStamp,
            format: p_format,
            data: p_data,
            base64: p_base64,
            needScale: p_needScale
        };
        this.vxcore.setGraphPreviewData(previewData);
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
        this.vxcore.setMathPreviewData(previewData);
    }
}
