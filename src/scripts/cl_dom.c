/*
 * Copyright (c) 2010-2014 Stefan Bolte <portix@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "private.h"

static void 
object_destroy_weak_cb(JSObjectRef jsobj, GObject *domobj) {
    JSContextRef ctx = scripts_get_global_context();
    if (ctx == NULL) 
        return;

    if (JSObjectGetPrivate(jsobj) != NULL) {
        JSObjectSetPrivate(jsobj, NULL);
        JSValueUnprotect(ctx, jsobj);
    }
    scripts_release_global_context();
}
static JSObjectRef
dom_make_node_array(JSContextRef ctx, GObject *list, JSValueRef *exc, gulong (*getlength)(GObject *item), 
        WebKitDOMNode * (*getitem)(GObject *, gulong)) {
    if (list == NULL) 
        return JSObjectMakeArray(ctx, 0, NULL, exc);

    g_return_val_if_fail(list, JSObjectMakeArray(ctx, 0, NULL, exc));

    JSObjectRef result = NULL;
    JSValueRef *argv;
    WebKitDOMNode *node;

    gulong n = getlength(list);

    if (n > 0) {
        argv = g_try_malloc_n(n, sizeof(JSValueRef));
        if (argv != NULL) {
            for (gulong i=0; i<n; i++) {
                node = getitem(list, i);
                argv[i] = make_dom_object(ctx, G_OBJECT(node));
            }
            result = JSObjectMakeArray(ctx, n, argv, exc);
            g_free(argv);
        }
    }
    if (result == NULL) {
        result = JSObjectMakeArray(ctx, 0, NULL, exc);
    }
    return result;
}

static JSObjectRef
dom_make_collection(JSContextRef ctx, WebKitDOMHTMLCollection *list, JSValueRef *exc) {
    return dom_make_node_array(ctx, G_OBJECT(list), exc, (gulong (*)(GObject *))webkit_dom_html_collection_get_length, 
            (WebKitDOMNode * (*)(GObject *, gulong)) webkit_dom_html_collection_item);
}
static JSObjectRef
dom_make_node_list(JSContextRef ctx, WebKitDOMNodeList *list, JSValueRef *exc) {
    return dom_make_node_array(ctx, G_OBJECT(list), exc, (gulong (*)(GObject *))webkit_dom_node_list_get_length, 
            (WebKitDOMNode * (*)(GObject *, gulong)) webkit_dom_node_list_item);
}
JSObjectRef 
make_dom_object(JSContextRef ctx, GObject *o) {
    if (WEBKIT_DOM_IS_HTML_COLLECTION(o)) {
        return dom_make_collection(ctx, WEBKIT_DOM_HTML_COLLECTION(o), NULL);
    }
    if (WEBKIT_DOM_IS_NODE_LIST(o)) {
        return dom_make_node_list(ctx, WEBKIT_DOM_NODE_LIST(o), NULL);
    }
    JSObjectRef result =  make_object_for_class(ctx, CLASS_DOM_OBJECT, o, true);
    g_object_weak_ref(o, (GWeakNotify)object_destroy_weak_cb, result);
    return result;
}
// dom functions
#define DOM_CAST__DOC_OR_ELEMENT GObject * (*)(GObject *, const char *, GError **)
static GObject *
selector_exec(JSContextRef ctx, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef *exc, 
        GObject * (*fel)(GObject *, const char *, GError **), GObject *(fdoc)(GObject *, const char *, GError **)) {
    if (argc == 0) 
        return NULL;

    GError *e = NULL;
    GObject *result = NULL;
    GObject *o = JSObjectGetPrivate(self);
    char *selector = NULL;
    if (o == NULL) 
        return NULL;

    gboolean is_element = WEBKIT_DOM_IS_ELEMENT(o);
    gboolean is_document = WEBKIT_DOM_IS_DOCUMENT(o);

    if (!(is_document || is_element)) 
        return NULL;
    selector = js_value_to_char(ctx, argv[0], -1, NULL);
    if (selector == NULL) 
        return NULL;

    if (is_element) {
        result = fel(o, selector, &e);
    }
    else {
        result = fdoc(o, selector, &e);
    }
    if (e != NULL) {
        js_make_exception(ctx, exc, EXCEPTION("%s"), e->message);
        result = NULL;
    }
    g_free(selector);
    return result;
}
/** 
 * Queries for elements, this method is the equivalent to
 * element.querySelectorAll. This method is only implemented by <span class="iltype">DOMDocument</span> and
 * <span class="iltype">Element</span> (ie. nodes with nodeType == 1 or nodeType == 9).
 *
 * @name querySelectorAll
 * @memberOf DOMObject.prototype
 * @since 1.12
 * @function
 * @example 
 * var doc = tabs.current.document;
 *
 * var nodeList = doc.evaluate("[href], [src]");
 * 
 * @param {String} selector 
 *      The DOM selector
 *
 * @returns {Array[DOMObject]}
 *      The result of the query. Unlike the result of element.querySelectorAll the result is converted to a real array.
 * */
static JSValueRef 
dom_query(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    GObject *list = selector_exec(ctx, self, argc, argv, exc, 
            (DOM_CAST__DOC_OR_ELEMENT)webkit_dom_element_query_selector_all, 
            (DOM_CAST__DOC_OR_ELEMENT)webkit_dom_document_query_selector_all);
    if (list != NULL) {
        return dom_make_node_list(ctx, WEBKIT_DOM_NODE_LIST(list), exc);
    }
    return NIL;

}
/** 
 * Gets the computed style of an element, this is only implemented by <span class="iltype">DOMWindow</span>. 
 * Unlike a CSSStyleDeclaration in the webcontext the properties of the style
 * declaration can only be get by parsing the cssText-property. 
 *
 * @name getComputedStyle
 * @memberOf DOMObject.prototype
 * @since 1.12
 * @function
 * 
 * @param {DOMElement} element 
 *      An element 
 *
 * @returns {CSSStyleDeclaration}
 * */
static JSValueRef 
dom_computed_style(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    if (argc == 0) 
        return NIL;
    GObject *o = JSObjectGetPrivate(self);
    GObject *e = js_get_private(ctx, argv[0], exc);
    if (o == NULL || e == NULL || !G_TYPE_CHECK_INSTANCE_TYPE(o, WEBKIT_TYPE_DOM_DOM_WINDOW) || !G_TYPE_CHECK_INSTANCE_TYPE(e, WEBKIT_TYPE_DOM_ELEMENT)) {
        js_make_exception(ctx, exc, EXCEPTION("Type error"));
        return NIL;
    }
    WebKitDOMCSSStyleDeclaration *style = webkit_dom_dom_window_get_computed_style(WEBKIT_DOM_DOM_WINDOW(o), WEBKIT_DOM_ELEMENT(e), NULL);
    if (style == NULL)
        return NIL;
    return make_dom_object(ctx, G_OBJECT(style));
}
/** 
 * Queries for an element, this method is the equivalent to
 * element.querySelector. This method is only implemented by <span class="iltype">DOMDocument</span> and
 * <span class="iltype">Element</span> (ie. nodes with nodeType == 1 or nodeType == 9).
 *
 * @name querySelector
 * @memberOf DOMObject.prototype
 * @since 1.12
 * @function
 * @example 
 * var doc = tabs.current.document;
 *
 * var nodeList = doc.evaluate("[href], [src]");
 * 
 * @param {String} selector 
 *      The DOM selector
 *
 * @returns {DOMObject}
 *      The result of the query. 
 * */
static JSValueRef 
dom_query_first(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    GObject *element = selector_exec(ctx, self, argc, argv, exc, 
            (DOM_CAST__DOC_OR_ELEMENT)webkit_dom_element_query_selector, 
            (DOM_CAST__DOC_OR_ELEMENT)webkit_dom_document_query_selector);
    if (element != NULL) {
        return make_dom_object(ctx, element);
    }
    return NIL;
}
/** 
 * Queries for nodes using XPath, this method is the equivalent to
 * document.evaluate. This method is only implemented by <span class="iltype">DOMDocument</span>.
 *
 * @name evaluate
 * @memberOf DOMObject.prototype
 * @since 1.12
 * @function
 * @example 
 * var doc = tabs.current.document;
 *
 * var nodeList = doc.evaluate(doc.body, "//div/input");
 * 
 * @param {Node}  refnode
 *      The reference node
 * @param {String} selector 
 *      The DOM selector
 *
 * @returns {Array[DOMObject]}
 *      The result of the query. Unlink the result of document.evaluate the
 *      result is converted to a real array.
 * */
static JSValueRef 
dom_evaluate(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    if (argc < 2) {
        return NIL;
    }
    JSValueRef result;
    GError *e = NULL;
    JSValueRef *items = NULL;
    GObject *o = JSObjectGetPrivate(self);
    GObject *refNode = js_get_private(ctx, argv[0], exc);


    if (o == NULL || !WEBKIT_DOM_IS_DOCUMENT(o) || refNode == NULL || !WEBKIT_DOM_IS_NODE(refNode)) 
        return NIL;

    char *selector = js_value_to_char(ctx, argv[1], -1, NULL);
    if (selector == NULL) 
        return NIL;

    WebKitDOMXPathResult *xpath = webkit_dom_document_evaluate(WEBKIT_DOM_DOCUMENT(o), selector, WEBKIT_DOM_NODE(refNode), NULL, 7, NULL, &e);
    if (e != NULL) {
        js_make_exception(ctx, exc, EXCEPTION("%s"), e->message);
        goto error_out;
    }

    gulong l = webkit_dom_xpath_result_get_snapshot_length(xpath, &e);
    items = g_try_malloc_n(l, sizeof(JSValueRef));
    if (items == NULL) {
        goto error_out;
    }
    for (gulong i = 0; i<l; i++) {
        WebKitDOMNode *node = webkit_dom_xpath_result_snapshot_item(xpath, i, &e);
        if (e != NULL) {
            js_make_exception(ctx, exc, EXCEPTION("%s"), e->message);
            g_free(items);
            items = NULL;
            goto error_out;
        }
        items[i] = make_dom_object(ctx, G_OBJECT(node));
    }
error_out:
    if (items == NULL) {
        result = JSObjectMakeArray(ctx, 0, NULL, exc);
    }
    else {
        result = JSObjectMakeArray(ctx, l, items, exc);
        g_free(items);
    }
    g_free(selector);
    return result;

}
/** 
 * Returns the real class name of a DOMObject, opposed to element.toString()
 * which always returns <i>[object DOMObject]</i>.
 * 
 *
 * @name asString
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 * @example 
 * var doc = tabs.current.document
 *
 * io.out(doc.body.asString()); // -> [object HTMLBodyElement]
 * io.out(doc.body.toString()); // -> [object DOMObject]
 *
 * @returns {String}
 *      The classname
 * */
static JSValueRef 
dom_as_string(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    const char *name; 
    char buffer[256];
    GObject *o = NULL;

    o = JSObjectGetPrivate(self); 
    if (o == NULL) {
        return NULL;
    }

    name = G_OBJECT_TYPE_NAME(o);
    g_return_val_if_fail(name != NULL, NULL);

    if (g_str_has_prefix(name, "WebKitDOM")) 
        name += 9;
    
    snprintf(buffer, 256, "[object %s]", name);
    return js_char_to_value(ctx, buffer);
}
/** 
 * Checks if the DOMObject is derived from some type
 * 
 *
 * @name checkType
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 * @example 
 * var doc = tabs.current.document
 *
 * io.out(doc.checkType("EventTarget"));  // -> true
 * io.out(doc.checkType("Element"));      // -> false
 *
 * @returns {boolean}
 *      true is the object is derived from the given type
 * */
static JSValueRef 
dom_check_type(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    gboolean is_type = false;
    if (argc == 0) {
        return JSValueMakeBoolean(ctx, false);
    }
    GObject *o = JSObjectGetPrivate(self);
    char *name = js_value_to_char(ctx, argv[0], 128, exc);
    char typename[256];
    if (o != NULL && typename != NULL) {
        snprintf(typename, 256, "WebKitDOM%s", name);
        GType type = g_type_from_name(typename);
        is_type = G_TYPE_CHECK_INSTANCE_TYPE(o, type);
    }
    g_free(name);
    return JSValueMakeBoolean(ctx, is_type);
    
}
#define DOM_CAST_VOID__SELF  void (*)(GObject *)
static JSValueRef
dom_void__self(JSContextRef ctx, JSObjectRef self, void (*func)(GObject *), size_t argc, const JSValueRef argv[], JSValueRef *exc, GType ts) {
    GObject *o = JSObjectGetPrivate(self);
    if (o == NULL || !G_TYPE_CHECK_INSTANCE_TYPE(o, ts)) {
        js_make_exception(ctx, exc, EXCEPTION("Type Error"));
    }
    else {
        func(o);
    }
    return NULL;
}
#define DOM_CAST_BOOL__CHAR_E  gboolean (*)(GObject *, const char *, GError **)
static JSValueRef
dom_bool__char_e(JSContextRef ctx, JSObjectRef self, gboolean (*func)(GObject *, const char *, GError **), 
        size_t argc, const JSValueRef argv[], JSValueRef* exc, GType ts) {
    GError *e = NULL;
    gboolean result = false;

    if (argc == 0) 
        return JSValueMakeBoolean(ctx, false);

    GObject *o = JSObjectGetPrivate(self);
    if (o == NULL || !G_TYPE_CHECK_INSTANCE_TYPE(o, ts)) {
        js_make_exception(ctx, exc, EXCEPTION("Type error"));
        return JSValueMakeBoolean(ctx, false);
    }
    char *val = js_value_to_char(ctx, argv[0], -1, exc);
    if (val != NULL) {
        result = func(o, val, &e);
        if (e != NULL) {
            result = false;
            js_make_exception(ctx, exc, EXCEPTION("%s"), e->message);
            g_error_free(e);
        }
        g_free(val);
    }
    return JSValueMakeBoolean(ctx, result);

}
#define DOM_CAST_OBJ__CHAR_E  GObject * (*)(GObject *, const char *, GError **)
JSValueRef
dom_obj__char_e(JSContextRef ctx, JSObjectRef self, GObject * (*func)(GObject *, const char *, GError **), 
        size_t argc, const JSValueRef argv[], JSValueRef* exc, GType ts) {
    GError *e = NULL;
    JSValueRef ret = NIL;
    if (argc == 0) 
        return NIL;
    GObject *o = JSObjectGetPrivate(self);
    if (o == NULL || !G_TYPE_CHECK_INSTANCE_TYPE(o, ts)) {
        js_make_exception(ctx, exc, EXCEPTION("Type error"));
        return NULL;
    }
    char *val = js_value_to_char(ctx, argv[0], -1, exc);
    if (val != NULL) {
        GObject *go = func(o, val, &e);
        if (e != NULL) {
            js_make_exception(ctx, exc, EXCEPTION("%s"), e->message);
            g_error_free(e);
        }
        else {
            ret = make_dom_object(ctx, go);
        }
        g_free(val);
    }
    return ret;

}

#define DOM_CAST_OBJ__OBJ_OBJ_E  GObject * (*)(GObject *, GObject *, GObject *, GError **)
JSValueRef
dom_obj__obj_obj_e(JSContextRef ctx, JSObjectRef self, GObject * (*func)(GObject *, GObject *, GObject *, GError **),
        size_t argc, const JSValueRef argv[], JSValueRef* exc, GType ts, GType t1, GType t2) {
    GError *e = NULL;
    JSValueRef ret = NIL;
    if (argc < 2) 
        return NIL;
    GObject *o = JSObjectGetPrivate(self);
    if (o == NULL || !G_TYPE_CHECK_INSTANCE_TYPE(o, ts))
        return NIL;
    GObject *a1 = js_get_private(ctx, argv[0], exc);
    GObject *a2 = js_get_private(ctx, argv[1], exc);
    if (o == NULL || a1 == NULL || a2 == NULL || !G_TYPE_CHECK_INSTANCE_TYPE(o, ts) || 
            !G_TYPE_CHECK_INSTANCE_TYPE(a1, t1) || !G_TYPE_CHECK_INSTANCE_TYPE(a2, t2)) {
        js_make_exception(ctx, exc, EXCEPTION("Type error"));
        return NULL;
    }
    GObject *go = func(o, a1, a2, &e);
    if (e != NULL) {
        js_make_exception(ctx, exc, EXCEPTION("%s"), e->message);
        g_error_free(e);
    }
    else {
        ret = make_dom_object(ctx, go);
    }
    return ret;
}
#define DOM_CAST_BOOL__OBJ gboolean (*)(GObject *, GObject *)
JSValueRef
dom_bool__obj(JSContextRef ctx, JSObjectRef self, gboolean (*func)(GObject *, GObject *),
        size_t argc, const JSValueRef argv[], JSValueRef* exc, GType ts, GType t1) {
    if (argc == 0) 
        return JSValueMakeBoolean(ctx, false);

    GObject *o = JSObjectGetPrivate(self);
    GObject *a1 = js_get_private(ctx, argv[0], exc);

    if (o == NULL || a1 == NULL || !G_TYPE_CHECK_INSTANCE_TYPE(o, ts) || 
            !G_TYPE_CHECK_INSTANCE_TYPE(a1, t1)) {
        js_make_exception(ctx, exc, EXCEPTION("Type error"));
        return NULL;
    }
    return JSValueMakeBoolean(ctx, func(o, a1));
}
#define DOM_CAST_OBJ__OBJ_E  GObject * (*)(GObject *, GObject *, GError **)
JSObjectRef 
dom_obj__obj_e(JSContextRef ctx, JSObjectRef self, GObject * (*func)(GObject *, GObject *, GError **),
        size_t argc, const JSValueRef argv[], JSValueRef* exc, GType ts, GType t1) {
    GError *e = NULL;
    JSObjectRef ret = NULL;
    if (argc < 1) 
        return NULL;
    GObject *o = JSObjectGetPrivate(self);
    GObject *a1 = js_get_private(ctx, argv[0], exc);
    if (o == NULL || a1 == NULL || !G_TYPE_CHECK_INSTANCE_TYPE(o, ts) || !G_TYPE_CHECK_INSTANCE_TYPE(a1, t1)) {
        js_make_exception(ctx, exc, EXCEPTION("Type error"));
        return NULL;
    }
    GObject *go = func(o, a1, &e);
    if (e != NULL) {
        js_make_exception(ctx, exc, EXCEPTION("%s"), e->message);
        g_error_free(e);
    }
    else {
        ret = make_dom_object(ctx, go);
    }
    return ret;
}
/** 
 * Creates a new Element. Only implemented by <span class="iltype">DOMDocument</span>
 * 
 *
 * @name createElement
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 * @param {String} tagname
 *      The element type
 * @example 
 * var doc = tabs.current.document
 *
 * var div = doc.createElement("div");
 *
 * @returns {Element}
 *      The new element
 * */
static JSValueRef 
dom_create_element(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_obj__char_e(ctx, self, (DOM_CAST_OBJ__CHAR_E)webkit_dom_document_create_element, argc, argv, exc, 
            WEBKIT_TYPE_DOM_DOCUMENT);
}
/** 
 * Creates a Textnode. Only implemented by <span class="iltype">DOMDocument</span>
 * 
 *
 * @name createTextNode
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 * @param {String} tagname
 *      The element type
 *
 * @returns {Node}
 *      The text node
 * */
static JSValueRef 
dom_create_text_node(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_obj__char_e(ctx, self, (DOM_CAST_OBJ__CHAR_E)webkit_dom_document_create_text_node, argc, argv, exc, 
            WEBKIT_TYPE_DOM_DOCUMENT);
}
/** 
 * Inserts a node as a child of a node. Implemented by <span class="iltype">Node</span>
 * 
 *
 * @name insertBefore
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 *
 * @param {Node} newNode
 *      The new element
 * @param {Node} refNode
 *      The new element
 *
 * @returns {Node}
 *      The inserted node
 * */
static JSValueRef 
dom_insert_before(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_obj__obj_obj_e(ctx, self, (DOM_CAST_OBJ__OBJ_OBJ_E)webkit_dom_node_insert_before, argc, argv, exc, 
            WEBKIT_TYPE_DOM_NODE, WEBKIT_TYPE_DOM_NODE, WEBKIT_TYPE_DOM_NODE);
}
/** 
 * Replaces a child of a node with a new node. Implemented by <span class="iltype">Node</span>
 * 
 *
 * @name replaceChild
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 *
 * @param {Node} newNode
 *      The new node
 * @param {Node} oldNode
 *      The old node
 *
 * @returns {Node}
 *      The replaced node
 * */
static JSValueRef 
dom_replace_child(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_obj__obj_obj_e(ctx, self, (DOM_CAST_OBJ__OBJ_OBJ_E)webkit_dom_node_replace_child, argc, argv, exc, 
            WEBKIT_TYPE_DOM_NODE, WEBKIT_TYPE_DOM_NODE, WEBKIT_TYPE_DOM_NODE);
}
/** 
 * Appends a node to end of the node. Implemented by <span class="iltype">Node</span>
 *
 * @name appendChild
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 *
 * @param {Node} newNode
 *      The new node
 *
 * @returns {Node}
 *      The child node
 * */
static JSValueRef 
dom_append_child(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_obj__obj_e(ctx, self, (DOM_CAST_OBJ__OBJ_E)webkit_dom_node_append_child, argc, argv, exc, 
            WEBKIT_TYPE_DOM_NODE, WEBKIT_TYPE_DOM_NODE);
}

/** 
 * Removes a node from the DOM. Implemented by <span class="iltype">Node</span>
 *
 * @name removeChild
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 * @example
 * var doc = tabs.current.document;
 *
 * doc.documentElement.removeChild(doc.body);
 *
 * @param {Node} node
 *      The node to remove
 *
 * @returns {Node}
 *      The removed node
 * */
static JSValueRef 
dom_remove_child(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_obj__obj_e(ctx, self, (DOM_CAST_OBJ__OBJ_E)webkit_dom_node_remove_child, argc, argv, exc, 
            WEBKIT_TYPE_DOM_NODE, WEBKIT_TYPE_DOM_NODE);
}
// XXX This is only a workaround, any nodes created will never be released by
// webkitgtk, so we release them manually
static JSValueRef 
dom_dispose(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    GObject *o = JSObjectGetPrivate(self);
    if (o != NULL) {
        g_object_run_dispose(o);
    }
    return NULL;
}
/** 
 * Tests if two nodes reference the same node. Implemented by <span class="iltype">Node</span>
 *
 * @name isSameNode
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 *
 * @param {Node} node
 *      The node to test
 *
 * @returns {Boolean}
 * */
static JSValueRef 
dom_is_same_node(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_bool__obj(ctx, self, (DOM_CAST_BOOL__OBJ)webkit_dom_node_is_same_node, argc, argv, exc, 
            WEBKIT_TYPE_DOM_NODE, WEBKIT_TYPE_DOM_NODE);
}
/** 
 * Tests two nodes are equal. Implemented by <span class="iltype">Node</span>
 *
 * @name isEqualNode
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 *
 * @param {Node} node
 *      The node to test
 *
 * @returns {Boolean}
 * */
static JSValueRef 
dom_is_equal_node(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_bool__obj(ctx, self, (DOM_CAST_BOOL__OBJ)webkit_dom_node_is_equal_node, argc, argv, exc, 
            WEBKIT_TYPE_DOM_NODE, WEBKIT_TYPE_DOM_NODE);
}
/** 
 * Tests whether a node is a descendant of a node. Implemented by <span class="iltype">Node</span>
 *
 * @name contains
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 *
 * @param {Node} node
 *      The node to test
 *
 * @returns {Boolean}
 * */
static JSValueRef 
dom_contains(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_bool__obj(ctx, self, (DOM_CAST_BOOL__OBJ)webkit_dom_node_contains, argc, argv, exc, 
            WEBKIT_TYPE_DOM_NODE, WEBKIT_TYPE_DOM_NODE);
}
/** 
 * Tests if a node matches a given selector, equivalent to webkitMatchesSelector. Implemented by <span class="iltype">Element</span>
 *
 * @name matches
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 * @example 
 * var doc = tabs.current.document;
 *
 * var isBody = doc.body.matches("body"); 
 * var matchesId = doc.body.matches("#someid"); // true if body has id 'someid'
 *
 * @param {String} selector
 *      A css selector
 *
 * @returns {Boolean}
 * */
static JSValueRef 
dom_matches(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_bool__char_e(ctx, self, (DOM_CAST_BOOL__CHAR_E)webkit_dom_element_webkit_matches_selector, argc, argv, exc, 
            WEBKIT_TYPE_DOM_ELEMENT);
}
/** 
 * Only implemented by <span class="iltype">Event</span>.
 * @name stopPropagation
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 */
static JSValueRef 
dom_stop_propagation(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_void__self(ctx, self, (DOM_CAST_VOID__SELF)webkit_dom_event_stop_propagation, argc, argv, exc, 
            WEBKIT_TYPE_DOM_EVENT);
}
/** 
 * Only implemented by <span class="iltype">Event</span>.
 * @name preventDefault
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 */
static JSValueRef 
dom_prevent_default(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_void__self(ctx, self, (DOM_CAST_VOID__SELF)webkit_dom_event_prevent_default, argc, argv, exc, 
            WEBKIT_TYPE_DOM_EVENT);
}
/** 
 * Only implemented by <span class="iltype">Element</span>.
 * @name focus
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 */
static JSValueRef 
dom_element_focus(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_void__self(ctx, self, (DOM_CAST_VOID__SELF)webkit_dom_element_focus, argc, argv, exc, 
            WEBKIT_TYPE_DOM_ELEMENT);
}
/** 
 * Only implemented by <span class="iltype">Element</span>.
 * @name blur
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 */
static JSValueRef 
dom_element_blur(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    return dom_void__self(ctx, self, (DOM_CAST_VOID__SELF)webkit_dom_element_blur, argc, argv, exc, 
            WEBKIT_TYPE_DOM_ELEMENT);
}

#if WEBKIT_CHECK_VERSION(2, 4, 0) 
/**
 * Callback called for DOM events. <span class="ilkw">this</span> refers to the element that connected to
 * the event.
 *
 * @callback DOMObject~onEvent
 *
 * @param {Event}  event 
 *      The event
 *
 * @returns {Boolean}
 *      If the callback returns true event.preventDefault() and
 *      event.stopPropagation() are called on event.
 * */
static gboolean 
dom_event_callback(WebKitDOMElement *element, WebKitDOMEvent *event, JSObjectRef cb) {
    gboolean result = false;

    JSContextRef ctx = scripts_get_global_context();
    if (ctx == NULL) 
        return false;

    JSObjectRef jselement = make_dom_object(ctx, G_OBJECT(element));
    JSObjectRef jsevent = make_dom_object(ctx, G_OBJECT(event));

    JSValueRef argv[] = { jsevent };
    JSValueRef ret = scripts_call_as_function(ctx, cb, jselement, 1, argv);
    if (JSValueIsBoolean(ctx, ret)) {
        result = JSValueToBoolean(ctx, ret);
        if (result) {
            webkit_dom_event_prevent_default(event);
            webkit_dom_event_stop_propagation(event);
        }
    }

    scripts_release_global_context();
    return result;
}
void 
dom_event_destroy_cb(JSObjectRef func, GClosure *closure) {
    JSContextRef ctx = scripts_get_global_context();
    if (ctx != NULL) {
        JSValueUnprotect(ctx, func);
        scripts_release_global_context();
    }
}
static JSValueRef 
dom_event_remove(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    GClosure *closure = JSObjectGetPrivate(self);
    if (closure != NULL) {
        g_closure_invalidate(closure);
        g_closure_unref(closure);
        JSObjectSetPrivate(self, NULL);

        JSStringRef propname = JSStringCreateWithUTF8CString("callback");

        JSValueRef cb = JSObjectGetProperty(ctx, self, propname, exc);
        JSObjectDeleteProperty(ctx, self, propname, exc);

        JSStringRelease(propname);
        JSValueUnprotect(ctx, cb);
    }
    return NULL;
}
/** 
 * Connects an element to a DOM-Event, equivalent to element.addEventListener().
 * Implemented by <span class="iltype">EventTarget</span>. Requires webkitgtk >= 2.6
 *
 * @name on
 * @memberOf DOMObject.prototype
 * @function
 * @since 1.12
 * @example
 * var doc = tabs.current.document;
 * var win = doc.defaultView;
 * 
 * var handle = win.on("click", function(e) {
 *     this.stop();
 *     io.out(e.target.nodeName + " was clicked");
 *     handle.remove();
 * });
 *
 * @param {String} event
 *      The DOM event
 * @param {DOMObject~onEvent} callback
 *      The callback function
 * @param {Boolean} [capture]
 *      Pass true to initiate capture, default false.
 *
 * @returns {Object}
 *      An object which implements a remove function to disconnect the
 *      event handler.
 * */
static JSValueRef 
dom_on(JSContextRef ctx, JSObjectRef func, JSObjectRef self, size_t argc, const JSValueRef argv[], JSValueRef* exc) {
    if (argc < 2) 
        return NULL;
    char *event = NULL;
    JSValueRef ret = NIL;
    gboolean capture = false;
    
    GObject *o = JSObjectGetPrivate(self);
    if (o == NULL || !WEBKIT_DOM_IS_EVENT_TARGET(o)) 
       goto error_out;

    JSObjectRef cb = js_value_to_function(ctx, argv[1], exc);
    if (cb == NULL) 
       goto error_out;

    JSValueProtect(ctx, cb);
    event = js_value_to_char(ctx, argv[0], 128, exc);
    if (event == NULL) 
       goto error_out;

    if (argc > 2) 
       capture = JSValueToBoolean(ctx, argv[2]);

    GClosure *closure = g_cclosure_new((GCallback)dom_event_callback, cb, (GClosureNotify)dom_event_destroy_cb);
    g_object_watch_closure(o, closure);

    if (webkit_dom_event_target_add_event_listener_with_closure(WEBKIT_DOM_EVENT_TARGET(o), event, closure, capture)) {
        ScriptContext *sctx = scripts_get_context();
        if (sctx != NULL) {
            JSObjectRef retobj = JSObjectMake(ctx, sctx->classes[CLASS_DOM_EVENT], closure);
            js_set_property(ctx, retobj, "callback", cb, kJSPropertyAttributeDontEnum, exc);
            ret = retobj;
            scripts_release_context();
        }

    }
    g_free(event);
error_out:
    return ret;
}
#endif

void
dom_initialize(ScriptContext *sctx) {
    /** 
     * @class
     * @classdesc
     * Classes that represent DOMElements. The implemented methods are only a
     * small subset of methods implemented in the DOM context of a site.
     * DOMObjects are mainly usefull for simple DOM manipulations and listening
     * to DOM events since event listeners in injected scripts cannot create a
     * callback in the scripting context. 
     * Not all methods listed below are implemented by all DOMObjects, see the method
     * descriptions for details.
     *
     * DOMObjects are destroyed every time a new document is loaded, so it must be
     * taken care not to hold references to DOMObjects that persist during a
     * pageload or frameload, otherwise those objects will leak memory. 
     *
     * Note that some element properties differ from the original property name.
     * Properties of DOMObjects don't have successive uppercase letters, e.g. 
     * <i>element.innerHTML</i> can be get or set with <i>element.innerHtml</i>.
     *    
     * @name DOMObject
     * @since 1.12
     * @example 
     * //!javascript
     *
     * Signal.connect("documentLoaded", function(wv, frame) {
     *    var doc = frame.document;
     *    var imageLinks = doc.query("img").reduce(function(last, img) {
     *        return last + img.src + "\n";
     *    }, "");
     *    io.out(imageLinks);
     * });
     *
     *
     * */
    JSStaticFunction dom_functions[] = {
        { "querySelectorAll",              dom_query,       kJSDefaultAttributes },
        { "getComputedStyle",              dom_computed_style,       kJSDefaultAttributes },
        { "querySelector",         dom_query_first,       kJSDefaultAttributes },
        { "evaluate",           dom_evaluate,       kJSDefaultAttributes },
        { "asString",           dom_as_string,       kJSDefaultAttributes },
        { "checkType",          dom_check_type,       kJSDefaultAttributes },
        { "createElement",      dom_create_element, kJSDefaultAttributes },
        { "createTextNode",      dom_create_text_node,      kJSDefaultAttributes },
        { "insertBefore",       dom_insert_before, kJSDefaultAttributes },
        { "replaceChild",       dom_replace_child, kJSDefaultAttributes },
        { "appendChild",        dom_append_child, kJSDefaultAttributes },
        { "removeChild",        dom_remove_child, kJSDefaultAttributes },
        { "_dispose",           dom_dispose, kJSDefaultAttributes },
        { "isSameNode",         dom_is_same_node, kJSDefaultAttributes },
        { "isEqualNode",      dom_is_equal_node, kJSDefaultAttributes },
        { "contains",           dom_contains, kJSDefaultAttributes },
        { "matches",           dom_matches, kJSDefaultAttributes },
        { "preventDefault",     dom_prevent_default, kJSDefaultAttributes },
        { "stopPropagation",     dom_stop_propagation, kJSDefaultAttributes },
        { "focus",              dom_element_focus, kJSDefaultAttributes },
        { "blur",               dom_element_blur, kJSDefaultAttributes },
#if WEBKIT_CHECK_VERSION(2, 4, 0) 
        { "on",                 dom_on,       kJSDefaultAttributes },
#endif
        { 0, 0, 0 }, 
    };
    JSClassDefinition cd = kJSClassDefinitionEmpty;
    cd.className = "DOMObject";
    cd.staticFunctions = dom_functions;
    cd.parentClass = sctx->classes[CLASS_GOBJECT];
    sctx->classes[CLASS_DOM_OBJECT] = JSClassCreate(&cd);


#if WEBKIT_CHECK_VERSION(2, 4, 0) 
    JSStaticFunction dom_event_functions[] = {
        { "remove",              dom_event_remove,       kJSDefaultAttributes },
        { 0, 0, 0 }, 
    };

    cd = kJSClassDefinitionEmpty;
    cd.className = "DWBDOMEvent";
    cd.staticFunctions = dom_event_functions;
    sctx->classes[CLASS_DOM_EVENT] = JSClassCreate(&cd);
#endif
}
