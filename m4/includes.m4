define(`html_input_inc', `0')dnl 
define(`html_select_inc', `0')dnl 
dnl
dnl Header
dnl
define(`html_header', 
<tr class="dwb_table_row"><th class="dwb_table_headline" colspan="3"> $1 </th></tr>)dnl
dnl
dnl Input
dnl
define(`html_input_element', 
`'    <td class="dwb_table_cell_left">
        $1 
    </td>
    <td class="dwb_table_cell_middle"> 
        $3 
    </td>
    <td class="dwb_table_cell_right"> 
        <input id="$1" type="$2">
    </td>
</tr>)dnl
dnl
define(`html_input', 
`ifelse(eval($0_inc`%2'), 0, dnl 
<tr class="dwb_table_row_even">
html_input_element($@), 
<tr class="dwb_table_row_odd">
`html_input_element($@)')`
'define(`$0_inc',incr($0_inc))')dnl 
dnl
dnl Select 
dnl
define(`html_select_element', `'
    <td class="dwb_table_cell_left">
        $1 
    </td>
    <td class="dwb_table_cell_middle"> 
        $3 
    </td>
    <td class="dwb_table_cell_right"> 
        <select id="$1">
            `$2'
        </select>
    </td>
</tr>)dnl
dnl
define(`html_select', `ifelse(eval($0_inc`%2'), 0, dnl 
<tr class="dwb_table_row_even">html_select_element($@), 
<tr class="dwb_table_row_odd">html_select_element($@))`
'define(`$0_inc',incr($0_inc))')dnl 
dnl
dnl Options 
dnl
define(`html_options', `ifelse($#, 0, , $#, 1,dnl
<option>$1</option>,
           <option>$1</option>
            `html_options(shift($@))')')dnl
