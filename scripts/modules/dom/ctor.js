var domutil         = require("dom$util"); 
var DOMStaticMixin  = require("dom$static");

function DOMCtor(document, collection, selector, window) {
    Object.defineProperties(this, {
        document : { value :  document, writable : true },
        $_collection : { value : collection, writable : true },
        $_selector : { value : selector, writable : true }, 
        window : { value : window, writable : true }
    });
}
Object.defineProperties(DOMCtor.prototype, DOMStaticMixin);
/**
 * Objects that represents a collection DOM nodes.
 * @name Collection
 * @class
 *
 * @mixes Factory
 * */
Object.defineProperties(DOMCtor.prototype, {
    $_events : { value : {}, writable : true }, 
    /** 
     * Destroys a collection, i.e. disconnects all signal handlers, and drops
     * all references to DOMObjects.
     *
     * @name destroy 
     * @function
     * @memberOf Collection.prototype
     *
     * */
    destroy : {
        value : function() {
            this.off();
            this.$_collection = null;
            this.document = null;
            this.$_selector = null;
            this.$_events = null;
            this.window = null;
        }
    },
    /** 
     * Calls a callback for each node in a collection
     *
     * @name each 
     * @function
     * @memberOf Collection.prototype
     *
     * @param {Function} callback
     * @param {Object}   [scope]
     *      The scope in which the callback is called, if omitted <span class="ilkw">this</span>
     *      refers to the element
     *
     * @returns {Collection}
     *      self
     * */
    each : {
        value : function(cb, scope) {
            this.$_collection.forEach(function(e, idx) {
                return cb.call(scope || e, idx, e);
            });
            return this;
        }
    }, 
    /** 
     * Filters a collection
     *
     * @name filter 
     * @function
     * @memberOf Collection.prototype
     *
     * @param {String} css 
     *      A css selector used as a filter
     *
     * @returns {Collection}
     *      A new Collection containing the filtered nodes
     * */
    filter : {
        value : function(selector) {
            var col = this.$_collection.filter(function(e) {
                return e.matches(selector);
            });
            return new DOMCtor(this.document, col, selector, this.window);
        }
    }, 
    /** 
     * Disconnects all signal handlers from the given event
     *
     * @name off 
     * @function
     * @memberOf Collection.prototype
     *
     * @param {String} [event]
     *      The event name, if omitted all event handlers are disconnected
     *
     * @returns {Collection}
     *      self
     *
     * */
    off : {
        value : function(event) {
            var key;
            if (this.$_events[event]) {
                domutil.off(this.$_events[event]);
                delete this.$_events[event];
            }
            else {
                for (event in this.$_events) {
                    domutil.off(this.$_events[event]);
                }
                this.$_events = {};
            }
            return this;
        }
    }, 
    /** 
     * Connects all elements in the collection to the event
     *
     * @name on 
     * @function
     * @memberOf Collection.prototype
     *
     * @param {String} event 
     *      The event name
     * @param {Function}  callback
     *      The callback for the event
     *
     * @returns {Collection}
     *      self
     *
     * */
    on : {
        value : function(event, callback) {
            if (this.$_events[event]) {
                this.off(event);
            }
            var events = [];
            this.each(function() {
                events.push(this.on(event, callback));
            });
            this.$_events[event] = events;
            return this;
        }
    }, 
    /** 
     * Constructs a new Collection from the first element
     *
     * @name first 
     * @function
     * @memberOf Collection.prototype
     *
     * @return {Collection}
     *      A new Collection
     *
     * */
    first : {
        value : function() { return new DOMCtor(this.document, this.$_collection[0] ? [this.$_collection[0]] : [], this.selector, this.window); }
    }, 
    /** 
     * Applies a style hash to all elements of a collection, see {@link DOM.css}
     * for details
     *
     * @name style 
     * @function
     * @memberOf Collection.prototype
     *
     * @param {Object} css
     *      Hash of css properties to set
     *
     * @returns {Collection}
     *      self
     *
     * */
    style : {
        value : function(props) {
            this.$_collection.forEach(function(e) {
                this.css(e, props);
            }, this);
            return this;
        }
    }, 
    /** 
     * Tests if an element in the collection is a descendant of the given node.
     *
     * @name contains 
     * @function
     * @memberOf Collection.prototype
     *
     * @returns {Boolean}
     *
     * */
    contains : {
        value : function(node) {
            return this.$_collection.some(function(e) {
                return e.contains(node);
            });
        }
    }, 
    /** 
     * The dom node collection
     *
     * @name _ 
     * @type Array[Node]
     * @readonly
     * @memberOf Collection.prototype
     *
     * */
    _ : {
        get : function() { return this.$_collection; }
    }, 
    /** 
     * Gets an element from the collection
     *
     * @name get 
     * @memberOf Collection.prototype
     * @function
     *
     * @param {Number} idx
     *      The index of the element
     *      
     * @returns {Node}
     *      The element or null
     * */
    get : {
        value : function(idx) {
            return this.$_collection[idx] || null;
        }
    }, 
    /** 
     * Sets an attribute or property of all elements or gets an attribute or
     * property of the first element in a collection
     *
     * @name attr 
     * @memberOf Collection.prototype
     * @function
     *
     * @param {String} attribute
     *      The the attribute or property name
     * @param {String} [value]
     *      The value to set, optional
     *
     * @returns {Node}
     *      The collection if used as a setter, the attribute or property if
     *      used as a getter
     * */
    attr: {
        value: function(prop, value) {
            if (value) {
                return this.each(function() {
                    this[prop] = value;
                });
            }
            else {
                return this.$_collection[0][prop];
            }
        }
    }
    

});
provide("dom$ctor", DOMCtor);
exports = DOMCtor;
