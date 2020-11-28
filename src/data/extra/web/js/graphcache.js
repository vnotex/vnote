// Cache all rendered graph.
// {type, format, text} -> data.
class GraphCache {
    constructor() {
        this.cache = new LruCache();
    }

    generateKey(p_type, p_format, p_text) {
        return p_type + p_format + p_text;
    }

    set(p_type, p_format, p_text, p_graph) {
        this.cache.set(generateKey(p_type, p_format, p_text), p_graph);
    }

    get(p_type, p_format, p_text, p_graph) {
        return this.cache.get(generateKey(p_type, p_format, p_text));
    }
}
