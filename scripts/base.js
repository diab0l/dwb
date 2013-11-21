Object.freeze((function () {
    String.prototype.isInt = function () 
    { 
        return !isNaN(parseInt(this, 10)); 
    };
    String.prototype.isLower = function () 
    { 
        return this == this.toLowerCase(); 
    };
    var globals = {
        actionElement : null,
        active : null,
        matchHint : -1,
        escapeChar : "\\",
        activeArr : [],
        activeInput : null,
        elements : [],
        positions : [],
        lastInput : null,
        lastPosition : 0,
        newTab : false,
        notify : null,
        hintTypes :  [ "a, textarea, select, input:not([type=hidden]), button,  frame, iframe, [onclick], [onmousedown]," + 
            "[role=link], [role=option], [role=button], [role=option], img",  // HINT_T_ALL
        //[ "iframe", 
        "a",  // HINT_T_LINKS
        "img",  // HINT_T_IMAGES
        "input:not([type=hidden]), input[type=text], input[type=password], input[type=search], textarea",  // HINT_T_EDITABLE
        "[src], [href]"  // HINT_T_URL
                 ]
    };
    var HintTypes = {
        HINT_T_ALL : 0,
        HINT_T_LINKS : 1,
        HINT_T_IMAGES : 2,
        HINT_T_EDITABLE : 3,
        HINT_T_URL : 4
    };
    var OpenMode = {
        OPEN_NORMAL      : 1<<0, 
        OPEN_NEW_VIEW    : 1<<1, 
        OPEN_NEW_WINDOW  : 1<<2
    };
    var p_getTextHints = function(arr) {
        var length = arr.length;
        var i, j, e, text, cur, start, l, max, r;
        if (length === 0)
            return;
        if (arr[0] instanceof p_numberHint) 
        {
            for (i=0; i<length; i++) 
            {
                e = arr[i];
                start = e.getStart(length);
                e.hint.textContent = start+i;
            }
        }
        else if (arr[0] instanceof p_letterHint) 
        {
            l = globals.letterSeq.length;
            max = Math.ceil(Math.log(length)/Math.log(l));
            r = Math.ceil(Math.pow(length, 1/max));
            for (i=0; i<length; i++) 
            {
                e = arr[i];
                text = new String();
                cur = i;
                for (j=0; j<max; j++) 
                {
                    text += globals.letterSeq[(cur%r)];
                    cur = Math.floor(cur/r);
                }
                e.hint.textContent = text;
            }
        }
    };

    var p_newHint = function(element, win, rect, oe) 
    {
        this.element = element;
        this.overlay = null;
        this.win = win;
        var hint = p_createElement("div");
        var toppos = rect.top + oe.offY;
        var leftpos = rect.left + oe.offX;
        var t = Math.max(toppos, 0);
        var l = Math.max(leftpos, 0);
        for (var i=0; i<globals.positions.length; i++) 
        {
            var p = globals.positions[i];
            if ((p.top -globals.fontSize <= t) && ( t <= p.top + globals.fontSize) && (l<=p.left + globals.fontSize) && (p.left-globals.fontSize <= l) ) 
            {
                l+=Math.ceil(globals.fontSize*2.5);
                break;
            }
        }
        hint.style.top = (t + globals.hintOffsetTop) + "px";
        hint.style.left = (l + globals.hintOffsetLeft) + "px";
        // 37000 is the z-index of the clickble element
        hint.style.zIndex = 37002;
        globals.positions.push({top : t, left : l});

        hint.className =  "dwb_hint";
        this.createOverlay = function() 
        {
            if (this.element instanceof HTMLAreaElement) 
                return;
            var comptop = toppos;
            var compleft = leftpos;
            var height = rect.height;
            var width = rect.width;
            var h = height + Math.max(0, comptop);
            var overlay = p_createElement("div");
            overlay.className = "dwb_overlay_normal";
            overlay.style.width = (compleft > 0 ? width : width + compleft) + "px";
            overlay.style.height = (comptop > 0 ? height : height + comptop) + "px";
            overlay.style.top = t + "px";
            overlay.style.left = l + "px";
            overlay.style.display = "block";
            overlay.style.cursor = "pointer";
            overlay.style.zIndex = 37001;
            this.overlay = overlay;
        };
        this.hint = hint;
    };
    var p_createElement = function(tagname) 
    {
        var element = document.createElement(tagname);
        if (!element.style) 
        {
            var namespace = document.getElementsByTagName('html')[0].getAttribute('xmlns') || "http://www.w3.org/1999/xhtml";
            element = document.createElementNS(namespace, tagname);
        }
        return element;
    };
    var p_numberHint = function (element, win, rect, offsetElement) 
    {
        this.varructor = p_newHint;
        this.varructor(element, win, rect, offsetElement);

        this.getStart = function(n) 
        {
            var start = parseInt(Math.log(n) / Math.log(10), 10)*10;
            if (n > 10*start-start) 
                start*=10;
            
            return Math.max(start, 1);
        };
        this.betterMatch = function(input) 
        {
            var length = globals.activeArr.length;
            var i, cl;
            if (input.isInt()) 
                return 0;

            var bestposition = 37;
            var ret = 0;
            for (i=0; i<length; i++) 
            {
                var e = globals.activeArr[i];
                if (input && bestposition !== 0) 
                {
                    var content = e.element.textContent.toLowerCase().split(" ");
                    for (cl=0; cl<content.length; cl++) 
                    {
                        if (content[cl].toLowerCase().indexOf(input) === 0) 
                        {
                            if (cl < bestposition) 
                            {
                                ret = i;
                                bestposition = cl;
                                break;
                            }
                        }
                    }
                }
            }
            p_getTextHints(globals.activeArr);
            return ret;
        };
        this.matchText = function(input, matchHint) 
        {
            var i;
            if (matchHint) 
            {
                var regEx = new RegExp('^' + input);
                return regEx.test(this.hint.textContent);
            }
            else 
            {
                var inArr = input.split(" ");
                for (i=0; i<inArr.length; i++) 
                {
                    if (!this.element.textContent.toLowerCase().match(inArr[i].toLowerCase())) 
                        return false;
                }
                return true;
            }
        };
    };
    var p_letterHint = function (element, win, rect, offsetElement) 
    {
        this.varructor = p_newHint;
        this.varructor(element, win, rect, offsetElement);

        this.betterMatch = function(input) {
            return 0;
        };

        this.matchText = function(input, matchHint) 
        {
            var i;
            if (matchHint) 
            {
                return (this.hint.textContent.toLowerCase().indexOf(input.toLowerCase()) === 0);
            }
            else 
            {
                var inArr = input.split(" ");
                for (i=0; i<inArr.length; i++) 
                {
                    if (!this.element.textContent.toUpperCase().match(inArr[i].toUpperCase())) 
                        return false;
                }
                return true;
            }
        };
    };

    var p_mouseEvent = function (e, ev) 
    {
        if (e.ownerDocument != document) 
            e.focus();

        var mouseEvent = e.ownerDocument.createEvent("MouseEvents");
        mouseEvent.initMouseEvent(ev, true, true, e.ownerDocument.defaultView, 0, 0, 0, 0, 0, false, false, false, false, 
            globals.newTab & OpenMode.OPEN_NEW_VIEW ? 1 : 0, null);
        e.dispatchEvent(mouseEvent);
    };
    var p_clickElement = function (element, ev) 
    {
        if (arguments.length == 2) 
            p_mouseEvent(element, ev);
        else 
        {
            p_mouseEvent(element, "mousedown");
            p_mouseEvent(element, "mouseover");
            p_mouseEvent(element, "click");
            p_mouseEvent(element, "mouseup");
        }
    };
    var p_setActive = function (element) 
    {
        var active = globals.active;
        if (active) 
        {
            if (globals.markHints) 
                active.overlay.style.background = globals.normalColor;
            else if (active.overlay) 
                active.overlay.parentNode.removeChild(active.overlay);

            active.hint.style.font = globals.font;
        }
        globals.active = element;
        if (!globals.active.overlay) 
            globals.active.createOverlay();

        if (!globals.markHints) 
            globals.active.hint.parentNode.appendChild(globals.active.overlay);

        var e = element.element;
        if (globals.notify) 
        {
            if (e.href || e.src) 
                globals.notify.innerText = encodeURI(e.href || e.ret);
            else if (e.name) 
                globals.notify.innerText = e.tagName.toLowerCase() + ", name=" + e.name;
            else if (e.innerText && e.innerText.trim().length > 0) 
                globals.notify.innerText = e.tagName.toLowerCase() + ": " + e.innerText.replace("\n\r", "").trim();
            else if (e.hasAttribute("onclick"))
                globals.notify.innerText = e.tagName.toLowerCase() + ": onclick";
            else if (e.hasAttribute("onmousedown"))
                globals.notify.innerText = e.tagName.toLowerCase() + ": onmousedown";
            else 
                globals.notify.innerText = e.tagName.toLowerCase();
        }

        globals.active.overlay.style.background = globals.activeColor;
        globals.active.hint.style.fontSize = globals.bigFont;
    };
    var p_hexToRgb = function (color) 
    {
        var rgb, i;
        if (color[0] !== '#') 
            return color;

        if (color.length == 4) 
        {
            rgb = /#([0-9a-f])([0-9a-f])([0-9a-f])/i.exec(color);
            for (i=1; i<=3; i++) 
            {
                var v = parseInt("0x" + rgb[i], 10)+1;
                rgb[i] = v*v-1;
            }
        }
        else 
        {
            rgb  = /#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})/i.exec(color);
            for (i=1; i<=3; i++) 
            {
                rgb[i] = parseInt("0x" + rgb[i], 16);
            }
        }
        return "rgba(" + rgb.slice(1) + "," +  globals.hintOpacity/2 + ")";
    };
    var p_createStyleSheet = function(doc) 
    {
        if (doc.hasStyleSheet) 
            return;

        var styleSheet = p_createElement("style");
        styleSheet.innerHTML += ".dwb_hint { " +
            "position:absolute; z-index:20000;" +
            "background:" + globals.bgColor  + ";" + 
            "color:" + globals.fgColor + ";" + 
            "border:" + globals.hintBorder + ";" + 
            "font:" + globals.font + ";" + 
            "display:inline;" +
            "width:auto;" +
            "height:auto;" +
            "padding:0px;" +
            "margin:0px;" +
            "opacity: " + globals.hintOpacity + "; }" + 
            ".dwb_overlay_normal { position:absolute!important;display:block!important; z-index:19999;background:" + globals.normalColor + ";}";
        doc.head.appendChild(styleSheet);
        doc.hasStyleSheet = true;
    };
    var p_getOffsets = function(doc) 
    {
        var oe = new Object();
        var win = doc.defaultView;
        var body = doc.body || doc.documentElement;
        var bs = win.getComputedStyle(body, null);
        var br = body.getBoundingClientRect();
        if (bs && br && (/^(relative|fixed|absolute)$/.test(bs.position)) ) 
        {
            oe.offX = -br.left; 
            oe.offY = -br.top;
        }
        else 
        {
            oe.offX = win.pageXOffset;
            oe.offY = win.pageYOffset;
        }
        return oe;
    };
    var p_appendHint = function (hints, varructor, e, win, r, oe) 
    {
        var element = new varructor(e, win, r, oe);
        globals.elements.push(element);
        hints.appendChild(element.hint);
        if (globals.markHints) 
        {
            element.createOverlay();
            hints.appendChild(element.overlay);
        }
    };
    var p_createMap = function (hints, varructor, e, win, r, oe) 
    {
        var map = null, a, i, coords, offsets;
        var mapid = e.getAttribute("usemap");
        var maps = win.document.getElementsByName(mapid.substring(1));
        for (i = 0; i<maps.length; i++) {
            if (maps[i] instanceof HTMLMapElement)  {
                map = maps[i]; 
                break;
            }
        }
        if (map === null)
            return;

        var areas = map.querySelectorAll("area");
        for (i=0; i<areas.length; i++) 
        {
            a = areas[i];
            if (!a.hasAttribute("href"))
                continue;

            coords = a.coords.split(",", 2);
            offsets = { 
                offX : oe.offX + parseInt(coords[0], 10), 
                offY : oe.offY + parseInt(coords[1], 10)
            };
            p_appendHint(hints, varructor, a, win, r, offsets);
        }
    };
    var p_createHints = function(win, varructor, type) 
    {
        var i;
        try 
        {
            var doc = win.document;
            var res = doc.body.querySelectorAll(globals.hintTypes[type]); 
            var e, r;
            p_createStyleSheet(doc);
            var hints = doc.createDocumentFragment();
            var oe = p_getOffsets(doc);
            for (i=0;i < res.length; i++) 
            {
                e = res[i];
                if ((r = p_getVisibility(e, win)) === null) {
                    continue;
                }
                if ( (e instanceof HTMLFrameElement || e instanceof HTMLIFrameElement)) {
                    p_createHints(e.contentWindow, varructor, type);
                    continue;
                }
                else if (e instanceof HTMLImageElement && type != HintTypes.HINT_T_IMAGES) {
                    if (e.hasAttribute("usemap")) 
                        p_createMap(hints, varructor, e, win, r, oe);
                }
                else {
                    p_appendHint(hints, varructor, e, win, r, oe);
                }
            }
            doc.body.appendChild(hints);
        }
        catch(exc) 
        {
            console.error(exc);
        }
    };
    var p_showHints = function (type, newTab) 
    {
        var i;
        if (document.activeElement) 
        {
            document.activeElement.blur();
        }
        globals.newTab = newTab;
        p_createHints(window, globals.style == "letter" ? p_letterHint : p_numberHint, type);
        var l = globals.elements.length;

        if (l === 0) 
        {
            return "_dwb_no_hints_";
        }
        else if (l == 1)  
        {
            return  p_evaluate(globals.elements[0].element, type);
        }

        globals.notify = document.createElement("div");
        globals.notify.style.cssText = 
            "bottom:0px;left:0px;position:fixed;z-index:1000;" + 
            "text-overflow:ellipsis;white-space:nowrap;overflow:hidden;max-width:100%;" + 
            "border-right:1px solid #555;" + 
            "border-top:1px solid #555;" + 
            "padding-right:2px;" + 
            "border-radius:0px 5px 0px 0px;letter-spacing:0px;background:" + globals.bgColor + ";" + 
            "color:" + globals.fgColor + ";font:" + globals.font + ";";
        globals.notify.id = "dwb_hint_notifier";
        document.body.appendChild(globals.notify);

        p_getTextHints(globals.elements);
        globals.activeArr = globals.elements;
        p_setActive(globals.elements[0]);
        return null;
    };
    var p_updateHints = function(input, type) 
    {
        var i;
        var array = [];
        var matchHint = false;
        if (!globals.activeArr.length) 
        {
            p_clear();
            p_showHints(type, globals.newTab);
        }
        if (globals.lastInput && (globals.lastInput.length > input.length)) 
        {
            p_clear();
            globals.lastInput = input;
            p_showHints(type, globals.newTab);
            return p_updateHints(input, type);
        }
        globals.lastInput = input;
        if (input) 
        {
            if (input[input.length-1] == globals.escapeChar && globals.matchHint == -1)
            {
                globals.matchHint = input.indexOf(globals.escapeChar) + 1;
                return null;
            }
            if (globals.matchHint != -1)
            {
                input = input.substring(globals.matchHint);
            }
            else if (globals.style == "number") 
            {
                if (input[input.length-1].isInt()) 
                {
                    input = input.match(/[0-9]+/g).join("");
                    matchHint = true;
                }
                else 
                    input = input.match(/[^0-9]+/g).join("");
            }
            else if (globals.style == "letter") 
            {
                var lowerSeq = globals.letterSeq.toLowerCase();
                if (input[input.length-1].isLower()) 
                {
                    if (lowerSeq.indexOf(input.charAt(input.length-1)) == -1) 
                        return "_dwb_no_hints_";

                    input = input.match(new RegExp("[" + lowerSeq + "]", "g")).join("");
                    matchHint = true;
                }
                else  
                    input = input.match(new RegExp("[^" + lowerSeq + "]", "g")).join("");
            }
        }
        for (i=0; i<globals.activeArr.length; i++) 
        {
            var e = globals.activeArr[i];
            if (e.matchText(input, matchHint)) 
                array.push(e);
            else
                e.hint.style.visibility = 'hidden';
        }
        globals.activeArr = array;
        if (array.length === 0) 
        {
            p_clear();
            return "_dwb_no_hints_";
        }
        else if (array.length == 1 && globals.autoFollow) 
        {
            return p_evaluate(array[0].element, type);
        }
        else 
        {
            globals.lastPosition = array[0].betterMatch(input);
            p_setActive(array[globals.lastPosition]);
        }
        return null;
    };
    var p_getVisibility = function (e, win) 
    {
        var style = win.getComputedStyle(e, null);
        if ((style.getPropertyValue("visibility") == "hidden" || style.getPropertyValue("display") == "none" ) ) 
            return null;

        var r = e.getClientRects()[0] || e.getBoundingClientRect();

        var height = win.innerHeight || document.body.offsetHeight;
        var width = win.innerWidth || document.body.offsetWidth;

        if (!r || r.top > height || r.bottom < 0 || r.left > width ||  r.right < 0 || !e.getClientRects()[0]) 
            return null;

        return r;
    };
    var p_clear = function() 
    {
        var p, i;
        try 
        {
            if (globals.elements) 
            {
                for (i=0; i<globals.elements.length; i++) 
                {
                    var e = globals.elements[i];
                    if ( (p = e.hint.parentNode) ) 
                        p.removeChild(e.hint);

                    if (e.overlay && (p = e.overlay.parentNode)) 
                        p.removeChild(e.overlay);
                }
            }
            if(! globals.markHints && globals.active) 
                globals.active.element.removeAttribute("dwb_highlight");
        }
        catch (exc) 
        { 
            console.error(exc); 
        }
        globals.elements = [];
        globals.activeArr = [];
        globals.active = null;
        globals.lastPosition = 0;
        globals.lastInput = null;
        globals.positions = [];
        globals.matchHint = -1;
        globals.actionElement = null;
        if (globals.notify && globals.notify.parentNode)
        {
            globals.notify.parentNode.removeChild(globals.notify);
        }
    };
    var p_action = function(action)
    {
        var e = globals.actionElement;
        if (!e)
            return;
        switch (action)
        {
            case "clickFocus" : 
                e.focus();
                p_clickElement(e, "click");
                break;
            case "focus" : 
                e.focus();
                break;
            case "all" : 
                p_clickElement(e);
                break;
            case "none" : 
                break;
            default : 
                p_clickElement(e, action);
            break;
                
        }
        p_clear();
    }
    var p_evaluate = function (e, type) 
    {
        globals.actionElement = e;
        var ret = null;
        var elementType = null;
        var resource = "unknown";
        if (e.type) 
        {
            elementType = e.type.toLowerCase();
        }
        var tagname = e.tagName.toLowerCase();
        if (globals.newTab && e.target == "_blank") 
        {
            e.target = null;
        }
        if (e.hasAttribute("src"))
        {
            resource = e.src;
        }
        else if (e.hasAttribute("href"))
        {
            resource = e.href;
        }
        if (type == HintTypes.HINT_T_IMAGES)
        {
            ret = "none|none";
        }
        else if (type == HintTypes.HINT_T_URL)
        {
            ret = "none|none"
        }
        else if ((tagname && (tagname == "input" || tagname == "textarea"))) 
        {
            if (elementType == "radio" || elementType == "checkbox") 
            {
                ret = "_dwb_check_|clickFocus|";
                resource = "@" + elementType;
            }
            else if (elementType && (elementType == "submit" || elementType == "reset" || elementType  == "button")) 
            {
                p_clickElement(e, "click");
                ret = "_dwb_click_|click";
                resource = "@" + elementType;
            }
            else 
            {
                ret = "_dwb_input_|focus";
                resource = "@" + tagname;
            }
        }
        else if (e.hasAttribute("role")) 
        {
            ret = "_dwb_click_|all";
            resource = "@role";
        }
        else 
        {
            ret = "_dwb_click_";
            if (tagname == "a" || e.hasAttribute("onclick"))
                ret += "|click";
            else if (e.hasAttribute("onmousedown")) 
                ret += "|mousedown";
            else if (e.hasAttribute("onmouseover")) 
                ret += "|mouseover";
            else {
                ret += "|all";
                p_clickElement(e);
            }
        }
        ret += "|" + resource;
        return ret;
    };
    var p_focusNext = function()  
    {
        var newpos = globals.lastPosition === globals.activeArr.length-1 ? 0 : globals.lastPosition + 1;
        p_setActive(globals.activeArr[newpos]);
        globals.lastPosition = newpos;
    };
    var p_focusPrev = function()  
    {
        var newpos = globals.lastPosition === 0 ? globals.activeArr.length-1 : globals.lastPosition - 1;
        p_setActive(globals.activeArr[newpos]);
        globals.lastPosition = newpos;
    };
    var p_addSearchEngine = function() 
    {
        var i, j;
        try 
        {
            p_createStyleSheet(document);
            var hints = document.createDocumentFragment();
            var res = document.body.querySelectorAll("form");
            var r, e;
            var oe = p_getOffsets(document);

            for (i=0; i<res.length; i++) 
            {
                var els = res[i].elements;
                for (j=0; j<els.length; j++) 
                {
                    if (((r = p_getVisibility(els[j], window)) !== null) && (els[j].type === "text" || els[j].type === "search")) 
                    {
                        e = new p_letterHint(els[j], window, r, oe);
                        hints.appendChild(e.hint);
                        e.hint.style.visibility = "hidden";
                        globals.elements.push(e);
                    }
                }
            }
            if (globals.elements.length === 0) 
                return "_dwb_no_hints_";

            if (globals.markHints) 
            {
                for (i=0; i<globals.elements.length; i++) 
                {
                    e = globals.elements[i];
                    e.createOverlay();
                    hints.appendChild(e.overlay);
                }
            }
            else 
            {
                e = globals.elements[0];
                e.createOverlay();
                hints.appendChild(e.overlay);
            }
            document.body.appendChild(hints); 
            p_setActive(globals.elements[0]);
            globals.activeArr = globals.elements;
        }
        catch (exc) 
        {
            console.error(exc);
        }
        return null;
    };
    var p_submitSearchEngine = function (string) 
    {
        var e = globals.active.element;
        e.value = string;
        if (e.form.submit instanceof Function) 
        {
            if (e.form.getAttribute('action') == '#')
                e.form.setAttribute('action', '');
            e.form.submit();
        }
        else 
        {
            var button = e.form.querySelector("input[type='submit'], button[type='submit']");
            p_clickElement(button, "click");
        }
        e.value = "";
        p_clear();
        if (e.form.method.toLowerCase() == 'post') 
            return e.name;

        return null;
    };
    var p_focusInput = function() 
    {
        var i;
        var res = document.body.querySelectorAll('input[type=text], input[type=password], textarea');
        if (res.length === 0) 
        {
            return "_dwb_no_input_";
        }
        var styles = document.styleSheets[0];
        styles.insertRule('input:focus { outline: 2px solid #1793d1; }', 0);
        if (!globals.activeInput) 
        {
            globals.activeInput = res[0];
        }
        else 
        {
            for (i=0; i<res.length; i++) 
            {
                if (res[i] == globals.activeInput) 
                {
                    globals.activeInput = res[i+1] || res[0];
                    break;
                }
            }
        }
        globals.activeInput.focus();
        return null;
    };
    var p_init = function (letter_seq, font, style,
        fg_color, bg_color, active_color, normal_color, border,  hintOffsetTop, hintOffsetLeft, opacity, markHints, autoFollow) 
    {
        globals.hintOpacity = opacity;
        globals.letterSeq  = letter_seq;
        globals.font = font;
        globals.style =  style.toLowerCase();
        globals.fgColor    = fg_color;
        globals.bgColor    = bg_color;
        globals.activeColor = p_hexToRgb(active_color);
        globals.normalColor = p_hexToRgb(normal_color);
        globals.hintBorder = border;
        globals.hintOffsetLeft = hintOffsetLeft;
        globals.hintOffsetTop = hintOffsetTop;
        globals.markHints = markHints;
        globals.autoFollow = autoFollow;
        globals.bigFont = Math.ceil(font.replace(/\D/g, "") * 1.25) + "px";
        globals.fontSize = Math.ceil(font.replace(/\D/g, ""))/2;
    };
    var p_pastePrimary = function(primary) 
    {
        var a = document.activeElement;
        if (a instanceof HTMLInputElement || a instanceof HTMLTextAreaElement)  
        {
            var start = a.selectionStart;
            a.value = a.value.substring(0, start) + primary + a.value.substring(a.selectionEnd);
            a.selectionStart = a.selectionEnd = start + primary.length;
        }
        else if (a.isContentEditable) 
        {
            var selection = window.getSelection();
            var range = selection.getRangeAt(0);
            selection.removeAllRanges();
            range.insertNode(document.createTextNode(primary));
            range.collapse(false);
            selection.addRange(range);
        }
    };
    return {
        createStyleSheet : function() 
        {
            p_createStyleSheet(document);
        },
        showHints : function(obj) 
        {
            return p_showHints(obj.type, obj.newTab);
        },
        updateHints : function (obj) 
        {
            return p_updateHints(obj.input, obj.type);
        },
        clear : function () 
        {
            p_clear();
        },
        followActive : function (obj) 
        {
            return p_evaluate(globals.active.element, obj.type);
        },
        focusNext : function () 
        {
            p_focusNext();
        },
        focusPrev : function () 
        {
            p_focusPrev();
        },
        addSearchEngine : function () 
        {
            return p_addSearchEngine();
        },
        submitSearchEngine : function (obj) 
        {
            return p_submitSearchEngine(obj.searchString);
        },
        focusInput : function () 
        {
            p_focusInput();
        },
        pastePrimary : function(selection) 
        {
            p_pastePrimary(selection);
        },
        insertAdblockRule : function(rule) 
        {
            var st=document.createElement('style');
            document.head.appendChild(st);
            document.styleSheets[document.styleSheets.length-1].insertRule(rule, 0);
        },
        follow : function(action)
        {
            p_action(action);
        },
        init : function (obj) 
        {
            p_init(obj.hintLetterSeq, obj.hintFont, obj.hintStyle, obj.hintFgColor,
                obj.hintBgColor, obj.hintActiveColor, obj.hintNormalColor,
                obj.hintBorder, obj.hintOffsetTop, obj.hintOffsetLeft, obj.hintOpacity, obj.hintHighlighLinks, obj.hintAutoFollow);
        }
    };
})());
