(function () {
        var prefixMessage     = "\n==> DEBUG [MESSAGE]    : ";
        var prefixFile        = "\n==> DEBUG [IN FILE]    : ";
        var prefixError       = "\n==> DEBUG [ERROR]      : ";
        var prefixStack       = "\n==> DEBUG [STACK]      : ";
        var prefixArguments   = "\n==> DEBUG [ARGUMENTS]  : ";
        var prefixCaller      = "\n==> DEBUG [CALLER]";
        var prefixSource      = "\n==> DEBUG [SOURCE]     : \n";
        var prefixFunction = "\n------>";
        var regHasDwb = new RegExp("[^]*/\\*<dwb\\*/([^]*)/\\*dwb>\\*/[^]*");
        var formatLine = function(line, max) 
        {
                var size = max - Math.ceil(Math.log(line+1)/Math.log(10)) + 1; 
                return Array(size).join(" ") + line + " >  ";
        };

        Object.defineProperties(io, {
            "debug" : 
            {
                value : function (params) 
                {
                    var outMessage = new String();
                    params = params || {};
                    var offset = params.offset || 0;
                    var error, message;
                    var line = -1;
                    var showLine;
                    var caller, source;
                    var stack;

                    if (typeof params == "string")
                        message = params;
                    else if (params instanceof Error)
                        error = params;
                    else 
                    {
                        if (params.message) 
                            message = params.message;
                        if (params.error instanceof Error)
                            error = params.error;
                    }

                    if (params.path || this.path) 
                        outMessage += prefixFile + (params.path || this.path);
                    if (message)
                        outMessage += prefixMessage + message;

                    if (error)
                    {
                        if (error.line || error.line === 0)
                            line = showLine = error.line;
                        else 
                            showLine = "?";
                        if (!error.stack) 
                        {
                            try 
                            {
                                throw new Error(error.message);
                            }
                            catch(e) 
                            { 
                                error = e;
                            }
                            offset += 1;
                        }
                        outMessage += prefixError + "Exception in line " + showLine + ": " + error.message;
                        stack = "[" + error.stack.match(/[^\n]+/g).slice(offset).join("] [")+"]"; 
                    }
                    else 
                    {
                        try 
                        {
                            throw new Error();
                        }
                        catch(e) 
                        {
                            stack =  "[" + e.stack.match(/[^\n]+/g).slice(offset + 2).join("] [")+"]"; 
                        }
                    }
                    if (stack) 
                    {
                        outMessage += prefixStack + stack;
                    }

                    if ((params.callee || this.arguments) && line >= 0)
                    {
                        caller = (params.callee || String(this.arguments.callee)).replace(regHasDwb, "$1", "");
                        source = caller.split("\n");
                        var length = source.length;
                        var max = Math.ceil(Math.log(source.length+1)/Math.log(10));

                        outMessage += prefixSource;
                        if (length >= line-3 && line-3 >= 0)
                        {
                            if (length >= line-4)
                                outMessage += "...\n";
                            outMessage += formatLine(line-1, max) +  source[line-3] + "\n";
                        }
                        else 
                            outMessage += formatLine(line-1, max) + "#!javascript\n";
                        if (length > line-2)
                            outMessage += formatLine(line, max) + source[line-2] + "     <-----\n";
                        if (length > line-1 && length != line) 
                        {
                            outMessage += formatLine(line+1, max) + source[line-1];
                            if (length > line + 1)
                                outMessage += "\n...";
                            else 
                                outMessage += "\nEOF";
                        }
                        else 
                            outMessage += "EOF";
                    }
                    else if (params.arguments) 
                    {
                        outMessage += prefixArguments + JSON.stringify(params.arguments);
                        caller = String(params.arguments.callee.caller);
                        outMessage += prefixCaller;
                        outMessage += prefixFunction + "\n";
                        outMessage += caller.replace(regHasDwb, "$1").replace(/\n/gm, "\n  ");
                        outMessage += prefixFunction;
                    }
                    io.print(outMessage + "\n", "stderr");
                }

            }
        });
})();
//Object.freeze(io);
