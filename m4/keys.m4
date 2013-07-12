include(includes.m4)
html_header(General)
dnl
html_input(open, text, Open url)
html_input(open_url, text, ``Open url, edit current url in the address bar'')
html_input(tabopen, text, Open in a new tab)
html_input(tabopen_url, text, ``Open url in a new tab, edit current url in the address bar'')
html_input(backopen, text, Open in a new tab in background)
html_input(backopen_url, text, ``Open url in a new background tab, edit current url in the address bar'')
html_input(winopen, text, Open url in a new window)
html_input(winopen_url, text, ``Open url in a new window, edit current url in the address bar'')
html_input(find_backward, text, Find backward)
html_input(find_backward_ic, text, Find backward case sensitive)
html_input(find_forward, text, Find forward)
html_input(find_forward_ic, text, Find forward case sensitive)
html_input(find_next, text, Find next)
html_input(find_previous, text, Find previous)
html_input(history_back, text, Go back)
html_input(history_forward, text, Go forward)
html_input(tab_hist_back, text, Go back in a new tab)
html_input(tab_hist_forward, text, Go forward in a new tab)
html_input(win_hist_back, text, Go back in a new window)
html_input(win_hist_forward, text, Go forward in a new window)
html_input(reload, text, Reload current page)
html_input(reload_bypass_cache, text, Reload without using any cached data)
html_input(stop_loading, text, Stop loading current page)
html_input(insert_mode, text, Insert mode)
html_input(normal_mode, text, Normal mode)
html_input(view_source, text, View page source)
html_input(zoom_in, text, Zoom in)
html_input(zoom_out, text, Zoom out)
html_input(zoom, text, Zoom)
html_input(quit, text, Quit dwb)

html_header(Hints)
dnl 
html_input(hints, text, Follow hints)
html_input(hints_tab, text, ``Follow hints, open link in a new tab'')
html_input(hints_background, text, ``Follow hints, open link in a new background tab'')
html_input(hints_win, text, ``Follow hints, open link in a new window'')
html_input(hints_links, text, Follow links)
html_input(hints_images, text, Follow images)
html_input(hints_images_tab, text, Follow images in a new tab)
html_input(hints_images_background, text, Follow images in a background tab)
html_input(hints_editable, text, Focus editable elements)
html_input(hints_url, text, Set hints url in commandline)
html_input(hints_url_tab, text, ``Set hints url in commandline, open in a new tab'')
html_input(hints_url_background, text, ``Set hints url in commandline, open in a background tab'')
html_input(hints_download, text, Download via hints)
html_input(hints_clipboard, text, Save link location to clipboard)
html_input(hints_primary, text, Save link location to primary selection)
html_input(hints_rapid, text, Open new tabs in background rapidly)
html_input(hints_rapid_win, text, Open new windows rapidly)

html_header(Bookmarks &amp Quickmarks)
dnl 
html_input(bookmark, text, Bookmark current page)
html_input(bookmarks, text, Show bookmarks in a pop up menu)
html_input(tab_bookmarks, text, Show bookmarks in a pop up menu and open in new tab)
html_input(win_bookmarks, text, Show bookmarks in a pop up menu and open in new window)
html_input(save_quickmark, text, Save a quickmark for this page)
html_input(quickmark, text, Open a quickmark)
html_input(tab_quickmark, text, Open quickmark in a new tab)
html_input(win_quickmark, text, Open quickmark in a new window)
html_input(start_page, text, Open the default homepage)

html_header(Commandline)
dnl
html_input(command_mode, text, Enter command mode)
html_input(entry_confirm, text, Alternative confirm shortcut)
html_input(entry_escape, text, Alternative escape shortcut)
html_input(entry_history_back, text, Command history back)
html_input(entry_history_forward, text, Command history forward)
html_input(entry_delete_letter, text, Delete a single letter)
html_input(entry_delete_line, text, Delete to the beginning of the line)
html_input(entry_delete_line_end, text, Delete to the end of the line)
html_input(entry_delete_word, text, Delete word)
html_input(entry_delete_word_forward, text, Delete word forward)
html_input(entry_word_back, text, Move cursor back on word)
html_input(entry_word_forward, text, Move cursor forward on word)
html_input(download_set_execute, text, Toggle between spawning an application and download path )
html_input(repeat, text, Repeat last commandline command)

html_header(Scrolling)
dnl
html_input(scroll_up, text, Scroll up)
html_input(scroll_down, text, Scroll down)
html_input(scroll_right, text, Scroll right)
html_input(scroll_left, text, Scroll left)
html_input(scroll_page_down, text, Scroll one page down)
html_input(scroll_page_up, text, Scroll one page up)
html_input(scroll_halfpage_down, text, Scroll one-half page down)
html_input(scroll_halfpage_up, text, Scroll one-half page up)
html_input(scroll_bottom, text, Scroll to  bottom of the page)
html_input(scroll_top, text, Scroll to the top of the page)

html_header(Tabs &amp UI)
dnl
html_input(tab_new, text, Open a new blank tab)
html_input(clear_tab, text, Clear history of a tab and load about:blank)
html_input(close_tab, text, Close tab)
html_input(only, text, Close all tabs except for the current one)
html_input(undo, text, Undo closing last tab)
html_input(focus_next, text, Focus next tab)
html_input(focus_tab, text, Focus nth tab)
html_input(focus_prev, text, Focus previous tab)
html_input(lock_domain, text, Lock tab to current domain)
html_input(lock_uri, text, Lock tab to current uri)
html_input(fullscreen, text, Toggle fullscreen)
html_input(tab_move, text, Move tab )
html_input(tab_move_left, text, Move tab left)
html_input(tab_move_right, text, Move tab right)
html_input(toggle_bars, text, Toggle tabbar and statusbar)
html_input(toggle_statusbar, text, Toggle statusbar)
html_input(toggle_tab, text, Toggle between current and last tab)
html_input(toggle_tabbar, text, Toggle tabbar)
html_input(protect, text, Protect/unprotect tab )
html_input(presentation_mode, text, Toggle presentation mode)
html_input(visible, text, Toggle visibility of a tab)

html_header(Completion)
dnl
html_input(buffers, text, Show all open tabs)
html_input(complete_bookmarks, text, Complete bookmarks)
html_input(complete_history, text, Complete browsing history)
html_input(complete_path, text, Complete local file path)
html_input(complete_searchengines, text, Complete searchengines)
html_input(complete_userscript, text, Complete userscripts)

html_header(Clipboard)
dnl
html_input(paste, text, Open url from clipboard)
html_input(tab_paste, text, Open url from clipboard in a new tab)
html_input(win_paste, text, Open url from clipboard in a new window)
html_input(paste_primary, text, Open url from primary selection)
html_input(tab_paste_primary, text, Open url from primary selection in a new tab)
html_input(win_paste_primary, text, Open url from primary selection in a new window)
html_input(yank, text, Yank current url to clipboard)
html_input(yank_primary, text, Yank current url to primary selection)
html_input(yank_title, text, Yank current title to clipboard)
html_input(yank_title_primary, text, Yank current title to primary selection)

html_header(Settings)
dnl
html_input(set_setting, text, Set a setting)
html_input(set_local_setting, text, Set a setting only for this session)
html_input(show_settings, text, Show and modify global properties)
html_input(set_key, text, Set keybinding)
html_input(show_keys, text, Show and modify keyboard configuration)
html_input(toggle_plugins_host, text, Toggle plugin blocker for current host)
html_input(toggle_plugins_uri, text, Toggle plugin blocker for current uri)
html_input(toggle_plugins_host_tmp, text, Toggle plugin blocker for current domain for this session)
html_input(toggle_plugins_uri_tmp, text, Toggle plugin blocker for current uri for this session)
html_input(toggle_scripts_host, text, Toggle block content for current domain)
html_input(toggle_scripts_uri, text, Toggle block content for current uri)
html_input(toggle_scripts_host_tmp, text, Toggle block content for current domain for this session)
html_input(toggle_scripts_uri_tmp, text, Toggle block content for current uri for this session)
html_input(proxy, text, Toggle proxy)

html_header(Miscellaneous)
dnl
html_input(adblock_reload_rules, text, Reload adblock rules)
html_input(allow_cookie, text, Allow persistent cookies for current site)
html_input(allow_session_cookie, text, Allow session cookies for current site)
html_input(allow_session_cookie_tmp, text, Allow session cookies for current site temporarily)
html_input(cancel_download, text, Cancel a running download)
html_input(download, text, Download current website)
html_input(execute_userscript, text, Execute userscript)
html_input(focus_input, text, Focus the next input)
html_input(new_tab, text, New tab for next navigation)
html_input(new_win, text, New window for next navigation)
html_input(web_inspector, text, Open the webinspector)
html_input(open_editor, text, Open an external editor for current input/textarea.)
html_input(print, text, Print current page)
html_input(print_preview, text, Show a print preview)
html_input(save, text, Save all configuration files)
html_input(save_session, text, Save current session)
html_input(save_named_session, text, Save current session with name)
html_input(toggle_hidden_files, text, Toggle hidden files in directory listing)
html_input(save_search_field, text, Add a new searchengine )
html_input(reload_bookmarks, text, Reload bookmark file)
html_input(reload_scripts, text, Reload all javascript userscripts )
html_input(reload_quickmarks, text, Reload quickmark file)
html_input(reload_userscripts, text, Reload userscripts )
html_input(show_bookmarks, text, Show bookmarks page)
html_input(show_downloads, text, Show download page)
html_input(show_history, text, Show history page)
html_input(show_quickmarks, text, Show quickmark page)

<tr class="dwb_table_row_even"><th class="dwb_table_headline" colspan="3">Custom commands</th></tr>
    <td colspan='3'>
        <div class='desc'>
            Custom commands can be defined in the following form:
        </div>
        <div class='commandLineContainer'>
            <div class='commandline'>[shortcut]:[command];;[command];;...
            </div>
        </div>
	</td>
</tr>
<tr class='dwb_table_row'>
    <td colspan='3'>
        <textarea rows='10' id='dwb_custom_keys_area'>
        </textarea>
    <td>
</tr>
<tr class='dwb_table_row'>
    <td class='dwb_table_cell_right' colspan='3'>
        <input id='dwb_custom_keys_submit' type='button' value='save custom'></input>
    </td>
</tr>
