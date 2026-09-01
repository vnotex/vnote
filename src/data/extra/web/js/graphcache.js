// Cache of rendered graph artifacts, with in-flight coalescing.
//
// A plain get-then-set LRU is close to useless in the read-mode render path: a
// document with 150 copies of 10 diagrams dispatches all 150 renders before the
// first result comes back, so every one of them misses and every one of them
// renders. The cache therefore stores the PENDING COMPUTATION, not just the
// finished artifact - the first request for a key installs a pending entry
// synchronously, every later request for that key subscribes to it, and the result
// fans out to all waiters when it resolves.
//
// What is cached is the PRE-SCALING artifact (the SVG/PNG payload), never a
// rasterized pixmap: the C++ side (PreviewHelper::m_codeBlockCache) already owns
// that level and handles zoom re-rasterization.
class GraphCache {
    // The default LruCache capacity of 100 is below one screenful of the large
    // perf fixtures, which would make the cache thrash exactly where it is needed.
    //
    // The entry count alone is not a useful bound, though: an entry holds a whole
    // rendered SVG plus a key containing the whole diagram source, and entries are
    // otherwise released only by clear(). 256 large diagrams would pin tens of
    // megabytes in the render process for the lifetime of the page, so cap the
    // total payload as well.
    constructor(p_capacity = 256, p_maxBytes = 8 * 1024 * 1024) {
        this.capacity = p_capacity;
        this.maxBytes = p_maxBytes;
        this.cache = this.createStore();

        // key -> Promise, for computations that have started but not resolved.
        this.pending = new Map();

        // The validation gate for T3 is this counter, not the clock.
        // - misses:      keys computed for the first time
        // - joins:       requests that subscribed to an already-running computation
        // - hits:        requests served from a completed entry
        // - invocations: actual renderer invocations (must equal misses)
        // - failures:    computations that rejected
        this.stats = { misses: 0, joins: 0, hits: 0, invocations: 0, failures: 0 };
    }

    // Weighed by the length of the key plus the artifact, which is what actually
    // occupies memory here. UTF-16 code units rather than bytes; the ratio is
    // close enough for a budget.
    createStore() {
        return new LruCache(this.capacity, this.maxBytes,
                            (p_key, p_val) => p_key.length
                                              + (typeof p_val === 'string' ? p_val.length : 0));
    }

    // Length-prefixed encoding, so that no two different component lists can
    // produce the same key. The previous bare concatenation collided:
    // ('ab', 'c', x) and ('a', 'bc', x) were the same string.
    //
    // The key must identify the OUTPUT, not just the source text. Every input that
    // varies the rendered artifact belongs in p_parts: the language, the output
    // format, the renderer backend (web vs local) and its server URL, the theme,
    // and the render target. Callers that cannot enumerate their inputs must not
    // use this cache.
    generateKey(p_parts) {
        let key = '';
        for (let i = 0; i < p_parts.length; ++i) {
            const part = (p_parts[i] === undefined || p_parts[i] === null)
                         ? '' : String(p_parts[i]);
            key += part.length + ':' + part + '|';
        }
        return key;
    }

    // Resolve @p_key, invoking @p_compute() at most once per key while it is
    // resident. p_compute() returns the artifact or a Promise of it.
    // Returns a Promise. On failure the entry is dropped - so a later request
    // retries rather than caching the failure - and every waiter rejects exactly once.
    request(p_key, p_compute) {
        const cached = this.cache.get(p_key);
        if (cached !== undefined) {
            ++this.stats.hits;
            return Promise.resolve(cached);
        }

        const inflight = this.pending.get(p_key);
        if (inflight) {
            ++this.stats.joins;
            return inflight;
        }

        ++this.stats.misses;
        ++this.stats.invocations;

        let computation = null;
        try {
            computation = Promise.resolve(p_compute());
        } catch (p_err) {
            ++this.stats.failures;
            return Promise.reject(p_err);
        }

        const tracked = computation.then(
            (p_value) => {
                this.pending.delete(p_key);
                // Never store undefined: it is indistinguishable from "absent".
                if (p_value !== undefined && p_value !== null) {
                    this.cache.set(p_key, p_value);
                }
                return p_value;
            },
            (p_err) => {
                ++this.stats.failures;
                this.pending.delete(p_key);
                throw p_err;
            });

        // Installed synchronously, before control can return to the dispatch loop:
        // this is what makes the 2nd..150th request for the same key a join.
        // (The handlers above run in a microtask, strictly after this line.)
        this.pending.set(p_key, tracked);
        return tracked;
    }

    // Drop everything. Inputs that are fixed for the lifetime of a page - the
    // Mermaid theme, the configured PlantUml server - are handled by clearing here
    // when they change instead of widening every key.
    clear() {
        this.cache = this.createStore();
        this.pending.clear();
    }

    statsString() {
        return 'invocations=' + this.stats.invocations
               + ' misses=' + this.stats.misses
               + ' joins=' + this.stats.joins
               + ' hits=' + this.stats.hits
               + ' failures=' + this.stats.failures;
    }
}
