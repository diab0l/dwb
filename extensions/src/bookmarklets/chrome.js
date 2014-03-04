/*jslint */
/*global xrequire,tabs,io,script,provide,xgettext*/
var shared = xrequire("shared");
    
function chromeDelete(name) {
    shared.delete(null, name);
    tabs.current.reload();
}

function chromeSave(data) {
    var oldData = shared.getAll();
    var newData = {};
    var name;
    shared.removeAllBinds();
    data.forEach(function(d) {
        var orig = oldData[d.origName];
        newData[d.name] = {
            name : d.name, 
            shortcut : d.shortcut, 
            command : d.command, 
            script : orig.script
        };
    });
    shared.save(newData);
    shared.addAllBinds();
    io.notify("Bookmarklets saved");
}

return function(wv) {
    var data, name, b, list, id;

    data = shared.getAll();
    list = [];
    id = script.generateId();
    for (name in data) {
        b = data[name];
        list.push({
            name : b.name,
            shortcut : b.shortcut,
            command : b.command
        });
    }
    list = list.sort(function(a, b) {
        return a.name < b.name ? -1 : 1;
    });
    wv.onceDocumentLoaded = function() {
        provide(id, {
            delete : chromeDelete,
            save : chromeSave
        });
        wv.inject(xgettext("~dom.js"), { moduleId : id, data : list }, 1);

        // cleanup
        wv.onceNavigation = function() {
            provide(id, null, true);
        };

    };
    return xgettext("~chrome.html");
};
