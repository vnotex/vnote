class GraphPreviewer {
    constructor(p_vxcore, p_container) {
        this.vxcore = p_vxcore;
        this.container = p_container;
        this.perfStats = null;
        this.perfSummaryTimer = null;
    }

    // performance.now(), not Date.now(): Chromium clamps wall-clock timer
    // resolution in some configurations.
    perfNow() {
        if (typeof performance !== 'undefined' && performance.now) {
            return performance.now();
        }
        return Date.now();
    }

    // Jobs in this immutable runtime share one diagnostic batch.
    perfStatsFor(p_timeStamp) {
        if (!this.perfStats || this.perfStats.timeStamp !== p_timeStamp || this.perfStats.reported) {
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

    // Count completion only after request-local cleanup, including errors.
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

    // The session serializes jobs through raster completion. Every asynchronous
    // callback captures this request, never a mutable current client or window geometry.
    async runPreview(p_args, p_context, p_graph, p_render) {
        const container = document.createElement('div');
        const request = {
            id: p_context.id,
            scope: p_args.scope,
            domId: 'job-' + Array.from(p_context.id, c => c.codePointAt(0).toString(16)).join('-'),
            scale: p_args.zoom > 0 ? p_args.zoom : 1,
            pixelRatio: p_args.devicePixelRatio > 0 ? p_args.devicePixelRatio : 1,
            width: p_args.width > 0 ? p_args.width : 800,
            color: window.getComputedStyle(this.vxcore.contentContainer).color,
            container,
            graph: p_graph
        };
        container.style.width = request.width + 'px';
        this.container.appendChild(container);
        let result;
        if (p_graph) {
            this.perfNoteRequest('webjobs', request.id, true);
        }
        try {
            if (p_context.cancelled) {
                throw new Error('Preview cancelled');
            }
            result = await p_render(request);
            return result;
        } finally {
            // Cancellation cannot release the shared renderer before its callback
            // and rasterizer drain. Only this job's DOM is removed, never the reader.
            container.remove();
            if (p_graph) {
                this.perfNoteSend('webjobs', request.id, result ? result.data.length : 0);
            }
        }
    }

    async initializeGraphWorker(p_worker) {
        if (!p_worker) {
            throw new Error('Graph renderer unavailable');
        }
        await new Promise((resolve, reject) => {
            if (p_worker.initialize(resolve, () => reject(new Error('Graph initialization failed')))) {
                resolve();
            }
        });
    }

    // Some workers are callback-based, others also return a Promise. Bridge both
    // failure forms, including Flowchart's synchronous draw failure without callback.
    renderCallback(p_render, p_synchronous = false) {
        return new Promise((resolve, reject) => {
            let called = false;
            const returned = p_render((...values) => {
                called = true;
                resolve(values);
            });
            if (returned && typeof returned.then === 'function') {
                returned.catch(reject);
            } else if (p_synchronous && !called) {
                reject(new Error('Graph rendering failed'));
            }
        });
    }

    previewGraph(p_args, p_context) {
        return this.runPreview(p_args, p_context, true, async (request) => {
            const lang = p_args.language;
            if (window.vxOptions.protectedView
                && (lang === 'puml' || lang === 'plantuml'
                    || (lang === 'mathjax' && this.vxcore.getWorker('math')?.renderer !== 'typst'))) {
                return this.protectedBlockedPreview(request, lang);
            }
            if (!p_args.source.length) {
                throw new Error('Empty graph source');
            }
            if (lang === 'mathjax') {
                return this.renderMath(request, p_args.source);
            }
            const name = lang === 'flow' || lang === 'flowchart' ? 'flowchartjs'
                : lang === 'mermaid' || lang === 'wavedrom' ? lang : null;
            if (name) {
                const worker = this.vxcore.getWorker(name);
                await this.initializeGraphWorker(worker);
                const [graph] = await this.renderCallback(callback =>
                    worker.renderText(request.container, p_args.source, request.domId, callback),
                    name === 'flowchartjs');
                this.perfNoteRender('webjobs', request.id);
                if (!graph || !graph.firstElementChild) {
                    throw new Error('Graph rendering failed');
                }
                const svg = graph.firstElementChild;
                if (name === 'mermaid') {
                    this.fixSvgRelativeWidth(svg, request);
                }
                graph.remove();
                return this.processSvgAsPng(svg, request);
            }
            if (lang === 'puml' || lang === 'plantuml') {
                // Preserve native/server rendering policy and PNG for PlantUML's
                // foreignObject labels, which the native SVG reader cannot paint.
                const worker = this.vxcore.getWorker('plantuml');
                const [format, data] = await this.renderCallback(callback =>
                    worker.renderText(p_args.source, callback));
                return this.previewData(format, data, format === 'png', true);
            }
            const graphviz = this.vxcore.getWorker('graphviz');
            const engine = graphviz && graphviz.getEngineForLanguage(lang);
            if (!engine) {
                throw new Error('Unsupported graph language');
            }
            await this.initializeGraphWorker(graphviz);
            const [svg] = await this.renderCallback(callback =>
                graphviz.renderText(p_args.source, engine, callback));
            if (!svg) {
                throw new Error('Graphviz rendering failed');
            }
            if (window.vxOptions.protectedView) {
                return this.processSvgAsPng(svg, request);
            }
            return this.previewData('svg', svg.outerHTML, false, true);
        });
    }

    previewMath(p_args, p_context) {
        return this.runPreview(p_args, p_context, false, request => {
            if (window.vxOptions.protectedView && this.vxcore.getWorker('math')?.renderer !== 'typst') {
                return this.protectedBlockedPreview(request, 'Math');
            }
            if (!p_args.source.length) {
                throw new Error('Empty math source');
            }
            return this.renderMath(request, p_args.source);
        });
    }

    protectedBlockedPreview(p_request, p_name) {
        const canvas = document.createElement('canvas');
        canvas.width = 560;
        canvas.height = 32;
        const context = canvas.getContext('2d');
        context.fillStyle = p_request.color;
        context.font = '14px sans-serif';
        context.fillText(p_name + ' preview blocked in protected notes', 4, 21);
        return this.previewData('png', canvas.toDataURL('image/png').split(',')[1], true, false,
            Math.max(1, Math.round(560 * p_request.scale)),
            Math.max(1, Math.round(32 * p_request.scale)));
    }

    async renderMath(p_request, p_text) {
        const worker = this.vxcore.getWorker('math');
        const [node] = await this.renderCallback(callback =>
            worker.renderText(p_request.container, p_text, callback, p_request.scope));
        if (!node) {
            throw new Error('Math rendering failed');
        }
        try {
            if (node.namespaceURI === 'http://www.w3.org/2000/svg') {
                this.fixSvgCurrentColor(node, p_request.color);
                this.fixSvgRelativeWidth(node, p_request);
                return await this.processSvgAsPng(node, p_request, worker.renderer === 'typst');
            }
            if (p_request.graph) {
                this.perfNoteRasterStart('webjobs', p_request.id);
            }
            const raster = await worker.rasterizeHtml(node, p_request.pixelRatio * p_request.scale);
            return this.previewData('png', raster.dataUrl.split(',')[1], true, false,
                Math.max(1, Math.round(raster.width * p_request.scale)),
                Math.max(1, Math.round(raster.height * p_request.scale)));
        } finally {
            node.remove();
        }
    }

    async processSvgAsPng(p_svgNode, p_request, p_typstMath = false) {
        if (p_request.graph) {
            this.perfNoteRasterStart('webjobs', p_request.id);
        }
        if (window.vxOptions.protectedView) {
            if (p_typstMath) {
                // The reader keeps source accessibility text. Native preview payloads
                // contain only painted math, including for a mathjax fenced graph job.
                p_svgNode.querySelectorAll('title, desc').forEach(node => node.remove());
            }
            // Also strips source-only aria metadata. Other protected graphs still
            // need their live computed styles copied before CSS is removed.
            DocumentRenderer.sanitizeProtectedSvg(p_svgNode, !p_typstMath);
            const viewBox = p_svgNode.viewBox.baseVal;
            const width = p_svgNode.width.baseVal.value || viewBox.width;
            const height = p_svgNode.height.baseVal.value
                || (viewBox.width > 0 ? width * viewBox.height / viewBox.width : viewBox.height);
            if (!(width > 0 && height > 0)) {
                throw new Error('Empty graph bounds');
            }
            if (!(viewBox.width > 0 && viewBox.height > 0)) {
                p_svgNode.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
            }
            const deviceScale = p_request.scale * p_request.pixelRatio;
            p_svgNode.setAttribute('width', Math.ceil(width * deviceScale));
            p_svgNode.setAttribute('height', Math.ceil(height * deviceScale));
            // Native validation/rasterization, never load an unchecked SVG data URI.
            return this.previewData('svg', new XMLSerializer().serializeToString(p_svgNode),
                false, false, Math.max(1, Math.round(width * p_request.scale)),
                Math.max(1, Math.round(height * p_request.scale)));
        }

        // XMLSerializer preserves well-formed foreignObject labels (including br).
        const svgString = new XMLSerializer().serializeToString(p_svgNode);
        const image = await new Promise((resolve, reject) => {
            SvgToImage.svgToImage(svgString, { crossOrigin: 'Anonymous' }, (error, raster) => {
                if (error) { reject(error); } else { resolve(raster); }
            });
        });
        const logicalWidth = Math.max(1, Math.round(image.width * p_request.scale));
        const logicalHeight = Math.max(1, Math.round(image.height * p_request.scale));
        const deviceScale = p_request.scale * p_request.pixelRatio;
        const canvas = document.createElement('canvas');
        canvas.width = Math.ceil(image.width * deviceScale) * 2;
        canvas.height = Math.ceil(image.height * deviceScale) * 2;
        canvas.getContext('2d').drawImage(image, 0, 0, canvas.width, canvas.height);
        let data;
        try {
            data = canvas.toDataURL();
        } catch (error) {
            // Preserve the vector fallback for a tainted foreignObject canvas.
            return this.previewData('svg', svgString, false, true, logicalWidth, logicalHeight);
        }
        return this.previewData('png', data.split(',')[1], true, false, logicalWidth, logicalHeight);
    }

    // Resolve relative dimensions against the requesting editor, not the helper window.
    fixSvgRelativeWidth(p_svgNode, p_request) {
        const width = p_svgNode.getAttribute('width');
        if (width && width.includes('%')) {
            if (p_svgNode.style.maxWidth && p_svgNode.style.maxWidth.endsWith('px')
                && p_svgNode.style.maxWidth !== '0px') {
                p_svgNode.setAttribute('width', p_svgNode.style.maxWidth);
            } else {
                p_svgNode.setAttribute('width', Math.max(p_request.width - 100, 100) + 'px');
            }
        }
    }

    fixSvgCurrentColor(p_svgNode, p_color) {
        for (const node of p_svgNode.querySelectorAll("g[fill='currentColor']")) {
            if (node.getAttribute('stroke') === 'currentColor') {
                node.setAttribute('stroke', p_color);
            }
            node.setAttribute('fill', p_color);
        }
    }

    previewData(p_format, p_data, p_base64, p_needScale, p_width = 0, p_height = 0) {
        if (!p_format || !p_data) {
            throw new Error('Preview rendering failed');
        }
        return { format: p_format, data: p_data, base64: p_base64, needScale: p_needScale,
                 logicalWidth: p_width, logicalHeight: p_height };
    }
}
