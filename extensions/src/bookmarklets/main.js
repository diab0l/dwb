/*jslint eqeq:true,forin:true*/
/*global data, script,xprovide,xinclude,io,util,Signal,ButtonContext,bind,extensions*/

/*<INFO
creates a context menu entry for saving bookmarklets
INFO>*/

var defaultConfig = {
//<DEFAULT_CONFIG
// The location to save bookmarklets
file : data.configDir + "/bookmarklets.json",

// command to delete a bookmarklet
deleteCommand : "bm_delete",

// shortcut to delete a bookmarklet
deleteShortcut : null, 

// command to list all bookmarklet and execute the selected bookmarklet
listCommand : "bm_list", 

// shortcut to list all bookmarklet and execute the selected bookmarklet
listShortcut : null, 

// command to change a bookmarklet
changeCommand : "bm_change", 

// shortcut to change a bookmarklet
changeShortcut : null

//>DEFAULT_CONFIG
};

var shared;
var cmdDelete, cmdList, cmdChange;

function newBookmarklet(bmscript, confirmOverwrite) {
    var data, name, shortcut, command, bookmarklet;
    data = shared.getAll();
    name = io.prompt("Name:");

    if (!name || name.trim().length == 0) {
        io.error("Name is required");
        return;
    }

    if (confirmOverwrite && data[name] && !io.confirm(name + " already exists, overwrite (y/n)?")) {
        return;
    }

    shortcut = io.prompt("Shortcut:");
    command = io.prompt("Command:");

    bookmarklet = {
        name : name, 
        shortcut : shortcut, 
        command : command, 
        script : bmscript
    };
    data[name] = bookmarklet;
    shared.save(data);

    shared.addBind(name, bookmarklet);
}

// actions
function doList(data, name) {
    shared.inject(data[name].script);
}

function doDelete(data, name) {
    shared.delete(data, name);
    io.notify("bookmarklet " + name + " deleted");
}

function doChange(data, name) {
    var bmscript = data[name].script;
    shared.delete(data, name);
    newBookmarklet(bmscript, false);
}

function complete(label, action) {
    var data = shared.getAll();
    var name, list = [], b;
    for (name in data) {
        b = data[name];
        list.push({
            left : name, 
            right : (b.shortcut ? "shortcut: " + b.shortcut + " " : "") + 
                    (b.command ? "command: " + b.command : "")
        });
    }
    if (list.length == 0) {
        io.error("No bookmarklets found");
        return;
    }
    util.tabComplete(label + ":", list, action.bind(null, data), true);
}

function onContextMenu(bmscript, wv, menu) {
    menu.addItems([{
        label : "Save as _bookmarklet", 
        callback : newBookmarklet.bind(null, bmscript, true)
    }]);
}

function onButtonPress(wv, result, e) {
    var linkUri, signal, bmscript;
    if (e.button == 3 && (result.context & ButtonContext.link) ) {
        linkUri = result.linkUri;
        if (linkUri.substring(0, 11) == "javascript:") {
            bmscript = decodeURI(linkUri.substring(11));
            script.own(Signal.once("contextMenu", onContextMenu.bind(null, bmscript)));
        }
    }
}

cmdDelete   = complete.bind(null, "Delete bookmarklet", doDelete);
cmdList     = complete.bind(null, "Use bookmarklet",    doList);
cmdChange   = complete.bind(null, "Change bookmarklet", doChange);

function init(config) {
    config = config || defaultConfig;
    config.name = "bookmarklets";

    xprovide("config", config, true);
    shared = xinclude("~shared.js");

    this.exports.config = config;

    script.own(
        Signal.connect("buttonPress", onButtonPress),
        bind(config.deleteShortcut, cmdDelete, config.deleteCommand),
        bind(config.listShortcut, cmdList, config.listCommand), 
        bind(config.changeShortcut, cmdChange, config.changeCommand),
        extensions.registerChrome("bookmarklets", xinclude("~chrome.js"))
    );

    shared.addAllBinds();

    return true;
}
function end() {
    script.removeHandles();
    shared.end();
    return true;
}

var bookmarklets = {
    defaultConfig : defaultConfig, 
    exports : { 
        deleteBookmarklet : cmdDelete,
        listBookmarklets : cmdList,
        changeBookmarklet : cmdChange
    }, 
    init : init,
    end : end
};
return bookmarklets;
