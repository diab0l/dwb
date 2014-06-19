var util    = namespace("util");
var gui     = namespace("gui");
var timer   = namespace("timer");

function htmlContent() {
/*HEREDOC
<head>
<style type="text/css">
body {
    margin:0; 
    padding:0; 
    border-top:1px solid #000;
}
.line {
    width : 100%;
}
.lineSelected {
    width : 100%;
}
.label {
    display:inline-block;
    white-space:nowrap;
    overflow:hidden;
    text-overflow:ellipsis;
    margin:0; 
    padding:0; 
}
.label.left {
    width : 50%;
    padding-right:5px;
}
.label.right {
    width:45%;
    float:right;
    text-align:right;
    padding-left:5px;
}
#content {
    width : 100%; 
    height : 100%; 
    margin:0; 
    padding:0;
    overflow-x:none;
}
* {
    -webkit-user-select:none;
}
</style>
</head>
<body>
<div id="content"></div>
</body>
HEREDOC*/
}

function injectable() {
    var mSelectedIdx = -1; 
    var mHeaderOffset = 0;
    var mElements;
    function clear() {
        document.getElementById("content").innerHTML = "";
        mElements = null;
        mSelectedIdx = -1;
        mHeaderOffset = 0;
    }
    function getSelected() {
        return mElements[mSelectedIdx].dataset.id;
    }
    function select(dir) {
        var l = mElements.length, element;
        if (l == 0) {
            return;
        }
        else if (mSelectedIdx == -1) {
            mSelectedIdx = 0;
        }
        else if (l > 1) {
            mElements[mSelectedIdx].className = "line";
            mSelectedIdx += dir;
            mSelectedIdx = mSelectedIdx < 0 ? (l-1) : (mSelectedIdx > l-1 ? 0 : mSelectedIdx);
        }
        else {
            return; 
        }
        element = mElements[mSelectedIdx];
        element.className = "lineSelected";
        element.scrollIntoView();
    }
    function createElement(tag, refElement, props) {
        var element, key;
        var element = document.createElement(tag);
        for (key in props) {
            element[key] = props[key];
        }
        refElement.appendChild(element);
        return element;
    }
    function update(data) {
        clear();
        var content = document.createDocumentFragment();
        mElements = data.map(function(item, i) {
            var className = "line ";
            var element = createElement("div", content, {
                className : "line",
            });
            createElement("code", element, {
                textContent : item.leftLabel || "",
                className : "label left"
            });
            createElement("code", element, {
                textContent : item.rightLabel || "", 
                className : "label right"
            });
            element.dataset.id = i;
            return element;
        });
        document.getElementById("content").appendChild(content);
        select();
    }
    function applyStyle(styles) {
        var selector, style, i, sheet, item;
        var styleSheets = document.styleSheets[0].cssRules;
        for (i=styleSheets.length - 1; i>=0; --i) {
            sheet = styleSheets[i];
            if ((style = styles[sheet.selectorText])) {
                for (selector in style) {
                    sheet.style[selector] = style[selector];
                }
            }
        }
    }
}

function getWidget() {
    var widget, wp, is, iw;
    widget = new HiddenWebView();

    widget.canFocus = false;
    widget.transparent = true;

    gui.mainBox.packStart(widget, false, false, 0);
    wp = settings.widgetPacking;
    is = wp.search(/[sS]/);
    iw = wp.indexOf("w");
    if (is > iw) {
        gui.mainBox.reorderChild(widget, is);
    }
    else {
        gui.mainBox.reorderChild(widget, is + 1);
    }
    widget.loadString(util.hereDoc(htmlContent));
    widget.inject(injectable, null, 64, true);
    return widget;
}

function Completion(args) {
    if (!args.shortcut) {
        throw new Error("Completion no shortcut defined!");
    }
    if (!args.onSelected) {
        throw new Error("Completion: onSelected not defined!");
    }

    util.mixin(this, args);

    this._startup();
}

Object.defineProperties(Completion.prototype, {
    // private
    _sigKeyPress    : { value : null,   writable : true },
    _sigKeyRelease  : { value : null,   writable : true },
    _idLabelNotify  : { value : -1,     writable : true },
    _lastText       : { value : "",     writable : true },
    _data           : { value : null,   writable : true },
    _height         : { value : 0,      writable : true }, 
    _initialData    : { value : null,   writable : true },
    _position       : { value : 0,      writable : true },
    _timer          : { value : null,   writable : true },
    _lastKeyRelease : { value : -1,     writable : true },
    _dataPosistion  : { value : 0,      writable : true },
    _forwardCache   : { value : null,     writable : true },
    _backwardCache  : { value : null,     writable : true },

    cleanup : {
        value : function() {
            var widget = Completion.widget;
            this._sigKeyRelease.disconnect();
            this._sigKeyPress.disconnect();
            gui.messageLabel.disconnect(this._idLabelNotify);
            this._idLabelNotify = -1;
            gui.messageLabel.label = "";

            widget.inject("clear()");
            widget.visible = false;
            this.onHide();

            this._height = 0;
            this._lastText = "";
            this._data = null;
            this._forwardCache = null;
            this._backwardCache = null;
            this._initialData = null;
            this._position = 0;
            this._lastKeyRelease = -1;
            if (this._timer) {
                this._timer.remove();
                this._timer = null;
            }
            util.normalMode();
        }
    }, 
    getSelected : { 
        value : function() {
            var id = JSON.parse(Completion.widget.inject("return getSelected()"));
            return this._data[id]; 
        }
    },
    _triggerSelectedAction : {
        value : function() {
            var item = this.getSelected();
            if (item) {
                this.onSelected(item);
            }
            else {
                this.onHide();
            }
        }
    },
    _prepareMatchBoth : {
        value : function() {
            this._initialData.forEach(function(item, i) {
                item.matchAttr = item.leftLabel + " " + item.rightLabel;
            });
        }
    },
    _prepareMatchLeft : {
        value : function() {
            this._initialData.forEach(function(item, i) {
                item.matchAttr = item.leftLabel;
            });
        }
    },
    _prepareMatchRight : {
        value : function(data) {
            this._initialData.forEach(function(item, i) {
                item.matchAttr = item.rightLabel;
            });
        }
    },
    _prepareData : {
        value : function() {
            this._initialData.forEach(function(item, i) {
                if (this.ignoreCase) {
                    item.matchAttr = item.matchAttr.toLowerCase();
                }
                item.id = i;
            }, this);
        }
    },
    setData : { 
        value : function(data, refresh) {
            this._position = 0;
            this._data = data;
            if (refresh) {
                this._doUpdate();
            }
        }
    },
    _doUpdate : { 
        value : function() {
            var widget = Completion.widget;
            if (this._data && this._data.length > 0) {
                widget.inject("update(" + JSON.stringify(this._data) + ")");
            }
            else {
                widget.inject("clear()");
            }
            if (this.fixedHeight) {
                return;
            }
            var oldHeight = this._height;
            this._height = Math.min(this._data.length, this.visibleItems) * (this.fontSize + this.lineSpacing);
            if (this._height == 0)  {
                widget.visible = false;
            }
            else {
                if (oldHeight != this._height) {
                    widget.heightRequest = this._height;
                }
                if (oldHeight == 0) {
                    widget.visible = true;
                }
            }
        }
    },
    forward : {
        value : function() {
            this._position++;
            Completion.widget.inject("select(1)");
        }
    },
    backward : {
        value : function() {
            this._position--;
            Completion.widget.inject("select(-1)");
        }
    },

    _onKeyPress : {
        value : function(w, e) {
            switch(e.name) {
                case "Return" : 
                    if (this.onReturn) {
                        this.onReturn();
                    }
                    else {
                        this._triggerSelectedAction();
                        this.cleanup();
                    }
                    return true;
                case "Escape" : 
                    if (this.onEscape) {
                        this.onEscape();
                    }
                    else {
                        this.cleanup();
                    }
                    return true;
                case "Down" :
                case "Tab" : 
                    if (this.onForward) {
                        this.onForward();
                    }
                    else {
                        this.forward();
                    }
                    return true;
                case "Up" :
                case "ISO_Left_Tab" : 
                    if (this.onBackward) {
                        this.onBackward();
                    }
                    else {
                        this.backward();
                    }
                    return true;
                default : 
                    return false;
            }
        }
    },
    _onKeyRelease : { 
        value : function(w, e) {
            var text, data;

            if (e.isModifier) {
                return;
            }
            text = gui.entry.text.trim();
            if (this.ignoreCase) {
                text = text.toLowerCase();
            }
            if (text == this._lastText) {
                return;
            }
            if (this._timer) {
                this._timer.remove();
            }
            data = text.length > this._lastText.length ? this._data : this._initialData;
            if (this.updateDelay > 0) {
                var self = this;
                this._timer = timer.start(this.updateDelay, function() {
                    this._position = 0;
                    self.setData(self.onFilter(text, data), true);
                    return false;
                });
            }
            else {
                this._position = 0;
                this.setData(this.onFilter(text, data), true);
            }
            this._lastText = text;
        }
    },
    _onUpdateLabel : {
        value : function() {
            if (gui.messageLabel.label != this.label) {
                gui.messageLabel.label = this.label;
            }
            return true;
        },
    },
    _bindCallback : {
        value : function() {
            var widget = Completion.widget;
            if (Completion._lastStyle != this._styleId) {
                widget.inject("applyStyle(" + Completion._styles[this._styleId] + ")");
                Completion._lastStyle = this._styleId;
            }

            gui.messageLabel.label = this.label;
            gui.entry.visible = true;
            gui.entry.hasFocus = true;
            this._idLabelNotify = gui.messageLabel.notify("label", this._onUpdateLabel.bind(this));

            this._sigKeyPress.connect();
            this._sigKeyRelease.connect();

            this.refresh();
            if (this.fixedHeight) {
                widget.heightRequest = this.visibleItems * (this.fontSize + this.lineSpacing);
                widget.visible = true;
            }
        }
    }, 
    _startup : {
        value : function() {
            var styleId;
            var style = JSON.stringify({
                body : { 
                    "background-color" : this.bgColor || settings.normalCompletionBgColor,
                    "color" : this.fgColor || settings.normalCompletionFgColor, 
                    "font-size" : this.fontSize,
                    "font-family" : this.fontFamily,
                },
                ".lineselected" : {
                    "background-color" : this.selectedBgColor || settings.activeCompletionBgColor,   
                    "color" : this.selectedFgColor || settings.activeCompletionFgColor,
                },
                ".label.right" : {
                    "margin-right" : this.margin || "3", 
                },
                ".label.left" : {
                    "margin-left" : this.margin || "3", 
                }, 
                "#content" : {
                    "overflow-y" : this.overflow || "auto" 
                }, 

            });
            if (!Completion._styles.some(function(s, i) {
                if (s == style) {
                    styleId = i;
                    return true;
                }
            }, this)) {
                styleId = Completion._styles.push(style) - 1;
            }
            Object.defineProperty(this, "_styleId", { value : styleId });

            this._sigKeyPress   = Signal("keyPress", this._onKeyPress.bind(this));
            this._sigKeyRelease = Signal("keyRelease", this._onKeyRelease.bind(this));

            bind(this.shortcut, this._bindCallback.bind(this));
            if (this.renderPageSize == 0) {
                this.renderPageSize = this.visibleItems * 2;
            }

            if (!this.onFilter) {
                switch (this.matchStyle) {
                    case "lazy" : 
                        this.onFilter = this._onFilterLazy; break;
                    case "exact" : 
                        this.onFilter = this._onFilterExact; break;
                    default :
                        this.onFilter = this._onFilterWordMatch; break;
                }
            }
            this.onShow = this.onShow || this.onFilter;
        }
    }, 
    _onFilterExact : {
        value : function(text, data) {
            return data.filter(function(item) {
                return item.matchAttr.indexOf(text) != -1;
            });
        }
    },
    _onFilterLazy : { 
        value : function(text, data) {
            return data.filter(function(item) {
                var matchAttr = item.matchAttr;
                var length = matchAttr.length;
                var tmp = text;
                for (var i=0; i<length; i++) {
                    if (tmp[0] == matchAttr[i]) {
                        tmp = tmp.substring(1);
                    }
                    if (!tmp) {
                        return true;
                    }
                }
                return false;
            });
        } 
    },
    _onFilterWordMatch : {
        value : function(text, data) {
            var words = text.split(/\s+/);
            return data.filter(function(item) {
                return words.every(function(word) {
                    return item.matchAttr.indexOf(word) != -1;
                });
            });
        }
    },
    _filterForward : {
        value : function() {

        }
    },
    refresh : {
        value : function() {
            this._initialData = this.onShow();
            var text = gui.entry.text.trim();
            var filter = Boolean(text);

            switch (this.match) {
                case "both" : this._prepareMatchBoth(); break;
                case "left" : this._prepareMatchLeft(); break;
                case "right" : this._prepareMatchRight(); break;
                default : break;
            }
            this._prepareData();
            if (filter) {
                this.setData(this.onFilter(text, this._initialData), true);
            }
            else {
                this.setData(this._initialData, true);
            }

        }
    },
    // public overridable properties
    onFilter        : { value : null,           writable : true },
    onSelected      : { value : null,           writable : true },
    shortcut        : { value : null,           writable : true }, 
    label           : { value : ":",            writable : true }, 
    visibleItems    : { value : 11,             writable : true },
    fontSize        : { value : 11,             writable : true },
    fontFamily      : { value : "monospace",    writable : true },
    lineSpacing     : { value : 2,              writable : true },
    onHide          : { value : function(){},   writable : true }, 
    ignoreCase      : { value : true,           writable : true }, 
    updateDelay     : { value : 0,              writable : true }, 
    renderItems     : { value : 20,             writable : true },
    fixedHeight     : { value : false,          writable : true },
    match           : { value : "both",         writable : true }, 
    onReturn        : { value : null,           writable : true }, 
    onEscape        : { value : null,           writable : true }, 
    onForward       : { value : null,           writable : true }, 
    onBackward      : { value : null,           writable : true }, 
    renderPageSize  : { value : 0,              writable : true }
});

Object.defineProperties(Completion, {
    _lastStyle : {
        value : -1, writable : true
    },
    _styles : {
        value : []
    },
    widget : { 
        value : getWidget() 
    } 
});

exports = Completion;
/* vim: set ft=javascript: */
