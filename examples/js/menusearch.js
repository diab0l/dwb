//!javascript
//
// Adds new menus to the right click context menu if text is selected on a
// website. Adds a menu entry for each configured searchengine that can be used
// to search for the selected text.

// The maximum length of the preview string
var previewLength = 50;


var engines, menuItems;

engines = data.parse("searchEngines").sort(function(a, b) {
    return a.keyword > b.keyword ? 1 : -1;
});

__assert__(engines.length !== 0);

menuItems = [ { 
        label : "_Search", 
        items : [{}, null].concat(engines.map(engine2item.bind(null, "open"))) 
    }, { 
        label : "Search in new _tab", 
        items : [{}, null].concat(engines.map(engine2item.bind(null, "tabopen"))) 
    }, { 
        label : "Search in _background tab", 
        items : [{}, null].concat(engines.map(engine2item.bind(null, "backopen"))) 
    }
];

function engine2item(command, engine) {
    return {
        label : "_" + engine.keyword + "   " + engine.host,
        command : command, 
        keyword : engine.keyword
    };
}

function getSubmenu(label, selection, subitem) {
    var submenu = new GtkWidget("GtkMenu");

    subitem.items[0].label = label;
    subitem.items.slice(2).forEach(function(item) {
        item.callback = execute.bind(null, item.command + " " +  item.keyword + " " + selection);
    });
    submenu.addItems(subitem.items);

    return { label : subitem.label, menu : submenu };
}

Signal.connect("contextMenu", function (wv, menu) {
    var preview, label, selection;

    selection = util.getSelection();
    if (!selection) {
        return;
    }

    preview = selection;
    if (previewLength > 0 && preview.length > previewLength) {
        preview = preview.replace(/\s+/g, " ").substring(0, previewLength - 3) + "...";
    }
    label = "Search for: " + preview;

    menu.addItems(menuItems.map(getSubmenu.bind(this, label, selection)));
});
