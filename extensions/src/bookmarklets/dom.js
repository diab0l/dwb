var exports = this.exports;
DOMUtil = { 
    delete : function(name) {
        dwb(function() {
            require(this.exports.moduleId).delete(this.exports.bookmarklet);
        }, { moduleId : exports.moduleId, bookmarklet : name });
    }, 
    save : function() {
        var elements = document.querySelectorAll("[data-id]");
        var data = Array.prototype.map.call(elements, function(e) {
            var origName = e.dataset.id;
            var nameNode = e.firstElementChild;
            var name = nameNode.firstElementChild.value;
            var shortcutNode = nameNode.nextElementSibling;
            var shortcut = shortcutNode.firstElementChild.value;
            var command = shortcutNode.nextElementSibling.firstElementChild.value;
            return {
                origName : origName,
                name : name, 
                shortcut : shortcut, 
                command : command
            };
        });
        dwb(function() {
            require(this.exports.moduleId).save(this.exports.data);
        }, { moduleId : exports.moduleId, data : data });
    }
};
(function() { 
    var content;
    function createElement(parent, type, props) {
        var prop;
        var e = document.createElement(type);
        for (prop in props) {
            e[prop] = props[prop];
        }
        parent.appendChild(e);

        return e;
    }

    function createWithParent(parent, type, props) {
        var e = createElement(parent, "td");
        var ret = createElement(e, type, props);
        return ret;
    }

    content = document.getElementById("content");
    exports.data.forEach(function(b) {
        var button, row;

        row = createElement(content, "tr", { role : "presentation" });
        row.dataset.id = b.name;
        createWithParent(row, "input", { value : b.name });
        createWithParent(row, "input", { value : b.shortcut });
        createWithParent(row, "input", { value : b.command });
        button = createWithParent(row, "button", { className : "button" });
        button.setAttribute("onclick", "DOMUtil.delete('" + b.name + "')");

        content.appendChild(row);
    });
    document.getElementById("saveButton").setAttribute("onclick", "DOMUtil.save()");
}());
