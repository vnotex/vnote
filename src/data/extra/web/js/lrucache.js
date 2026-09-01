// Least-recently-used cache.
//
// Two independent bounds, both optional:
// - p_capacity: maximum number of entries.
// - p_maxWeight / p_weigh: maximum total weight, where p_weigh(key, value)
//   returns the weight of one entry. Needed when the entries are of wildly
//   different sizes - a count of 256 rendered SVGs says nothing about how many
//   megabytes are pinned. Left at 0 the weight bound is disabled entirely.
class LruCache {
    constructor(p_capacity = 100, p_maxWeight = 0, p_weigh = null) {
        this.capacity = p_capacity;
        this.maxWeight = p_maxWeight;
        this.weigh = p_weigh;

        this.cache = new Map();

        // Per-key weights, so eviction can subtract the right amount without
        // re-measuring a value that may have been mutated since.
        this.weights = new Map();
        this.weight = 0;
    }

    get(p_key) {
        let item = this.cache.get(p_key);
        if (item) {
            this.cache.delete(p_key);
            this.cache.set(p_key, item);
        }
        return item;
    }

    set(p_key, p_val) {
        if (this.cache.has(p_key)) {
            this.remove(p_key);
        }

        this.cache.set(p_key, p_val);
        if (this.maxWeight > 0) {
            const w = this.weigh ? this.weigh(p_key, p_val) : 0;
            this.weights.set(p_key, w);
            this.weight += w;
        }

        this.trim();
    }

    remove(p_key) {
        if (!this.cache.has(p_key)) {
            return;
        }

        this.cache.delete(p_key);
        if (this.weights.has(p_key)) {
            this.weight -= this.weights.get(p_key);
            this.weights.delete(p_key);
        }
    }

    // Evict from the least-recently-used end until both bounds hold. A loop, not
    // a single delete: an oversized entry can push the cache past its budget by
    // more than one entry's worth, and the old `size == capacity` equality test
    // could not recover if the size ever overshot.
    trim() {
        while (this.cache.size > this.capacity && this.cache.size > 0) {
            this.remove(this.first());
        }

        while (this.maxWeight > 0 && this.weight > this.maxWeight && this.cache.size > 1) {
            this.remove(this.first());
        }
    }

    first() {
        return this.cache.keys().next().value;
    }
}
