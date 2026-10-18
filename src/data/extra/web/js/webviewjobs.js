// Generic jobs share the page's existing QWebChannel handshake. Providers own
// dependency readiness and register handlers before calling setReady().
(function () {
    'use strict';

    const handlers = new Map();
    const running = new Map();
    let bridge = null;
    let generation = null;
    let runtimeReady = false;
    let announced = false;

    function announceReady() {
        if (bridge && runtimeReady && !announced) {
            announced = true;
            bridge.setReady(generation);
        }
    }

    function errorMessage(error) {
        return error && error.message ? String(error.message) : String(error);
    }

    async function dispatch(id, operation, args) {
        if (running.has(id)) {
            return;
        }
        const context = { id: id, cancelled: false };
        running.set(id, context);
        let success = false;
        let value = null;
        let error = '';
        try {
            const handler = handlers.get(operation);
            if (!handler) {
                throw new Error('Unknown web job operation: ' + operation);
            }
            const result = await handler(args, context);
            // Check serializability here so a rejected transport conversion also
            // settles the native serial slot. Undefined is represented by null.
            value = result === undefined ? null : JSON.parse(JSON.stringify(result));
            success = true;
        } catch (failure) {
            error = errorMessage(failure);
        } finally {
            running.delete(id);
        }
        // Awaiting the handler includes its finally/cleanup. Cancellation only
        // marks the context; it must never pretend unfinished DOM work is done.
        bridge.complete(id, success, value, error);
    }

    window.vxWebJobs = {
        registerHandler: function (name, handler) {
            if (typeof name !== 'string' || !name || typeof handler !== 'function') {
                throw new TypeError('A web job requires a name and a handler');
            }
            if (handlers.has(name)) {
                throw new Error('Web job handler already registered: ' + name);
            }
            handlers.set(name, handler);
        },

        bind: function (jobBridge) {
            if (bridge === jobBridge) {
                return;
            }
            if (bridge) {
                throw new Error('Web jobs already bound for this page');
            }
            bridge = jobBridge;
            // Never adopt generationChanged on the old document. A new document
            // captures its epoch through its own single channel handshake.
            generation = bridge.generation;
            bridge.jobRequested.connect(dispatch);
            bridge.jobCancelled.connect(function (id) {
                const context = running.get(id);
                if (context) {
                    context.cancelled = true;
                }
            });
            announceReady();
        },

        setReady: function () {
            runtimeReady = true;
            announceReady();
        }
    };
})();
