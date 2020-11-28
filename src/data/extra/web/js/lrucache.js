class LruCache {
    constructor(p_capacity = 100) {
        this.capacity = p_capacity;
        this.cache = new Map();
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
            this.cache.delete(p_key);
        }
        else if (this.cache.size == this.capacity) {
            this.cache.delete(this.first());
        }
        this.cache.set(p_key, p_val);
    }

    first() {
        return this.cache.keys().next().value;
    }
}
