//!javascript
// Creates hints for video sites supported by quvi.

var videoplayer = "mplayer";

hint(";v", function(wv, resource) {
    if (resource[0] !== "@")
    {
        system.spawn("quvi dump -s best --exec '" + videoplayer + " %u' " + resource);
        return true;
    }
});


