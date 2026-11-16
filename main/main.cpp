#include "vmsys.h"
#include "vmio.h"
#include "vmgraph.h"
#include "vmchset.h"
#include "vmstdlib.h"
//wq#include "vminput.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define NOTES_DIR       "E:\\Notes"
#define MAX_NOTES       128
#define MAX_FILENAME    96
#define MAX_NOTE_CHARS  4096
#define MAX_NOTE_BYTES  (64 * 1024)

#define C_BG            VM_COLOR_BLACK
#define C_HEADER        VM_COLOR_DARK_GREY
#define C_TEXT          VM_COLOR_WHITE
#define C_SELECTED      VM_COLOR_BLUE
#define C_ACCENT        VM_COLOR_GREEN
#define C_POPUP         VM_COLOR_DARK_GREY

/* Browser UI layout intentionally follows the list/selection/softkey pattern
 * from abook.cpp, while text input and UTF-8 conversion follow mre_editor. */
typedef enum { UI_BROWSER, UI_MENU, UI_DELETE_CONFIRM } UiMode;
typedef enum { INPUT_NONE, INPUT_NEW_NAME, INPUT_RENAME } InputAction;

typedef struct {
    VMWCHAR name[MAX_FILENAME];
} NoteEntry;

static VMINT g_layer = -1;
static VMINT g_screen_w;
static VMINT g_screen_h;
static VMINT g_font_h;
static VMINT g_header_h;
static VMINT g_footer_h;
static VMINT g_selected;
static VMINT g_scroll;
static VMINT g_note_count;
static VMINT g_visible_rows;
static UiMode g_ui = UI_BROWSER;
static InputAction g_input_action = INPUT_NONE;
static NoteEntry g_notes[MAX_NOTES];
static VMWCHAR g_current_path[260];
static VMWCHAR g_folder[128];
static VMWCHAR g_status[96];
static VMBOOL g_startup_empty = VM_FALSE;

static void refresh_notes(void);
static void open_selected_note(void);
static void start_new_note(void);
static void start_editor(void);
static void render_screen(void);
static void draw_ascii(VMINT x, VMINT y, const char *text, vm_graphic_color color);
static void filename_input_cb(VMINT state, VMWSTR text);
static void editor_input_cb(VMINT state, VMWSTR text);
static void rename_input_cb(VMINT state, VMWSTR text);
static void delete_selected_note(void);
static void handle_key(VMINT event, VMINT keycode);
static void handle_pen(VMINT event, VMINT x, VMINT y);
static void handle_sysevt(VMINT message, VMINT param);

//static VMINT ascii_to_ucs2_const(VMWSTR dst, VMINT size, const char *src)
//{
//    return vm_ascii_to_ucs2(
//        dst,
//        size,
//        const_cast(VMSTR)(src ? src : ""));
//}

static VMINT ascii_to_ucs2_const(VMWSTR dst, VMINT size, const char *src)
{
    if (dst == NULL || size <= 0)
        return -1;

    return vm_ascii_to_ucs2(
        dst,
        size,
        (VMSTR)(src ? src : "")
    );
}

static vm_graphic_color rgb(VMUINT8 r, VMUINT8 g, VMUINT8 b)
{
    vm_graphic_color c;
    c.vm_color_565 = VM_COLOR_888_TO_565(r, g, b);
    c.vm_color_888 = ((VMUINT32)b << 16) | ((VMUINT32)g << 8) | r;
    return c;
}

static void set_status(const char *message)
{
//    vm_ascii_to_ucs2(g_status, sizeof(g_status), message ? message : "");
    ascii_to_ucs2_const(g_status, sizeof(g_status), message);
}

static void build_search_pattern(VMWSTR out)
{
    //vm_ascii_to_ucs2(out, 260 * sizeof(VMWCHAR), NOTES_DIR "\\*.*");
    ascii_to_ucs2_const(out, 260 * sizeof(VMWCHAR), NOTES_DIR "\\*.*");
}

static void build_note_path(VMWSTR out, const VMWCHAR *name)
{
//    vm_ascii_to_ucs2(out, 260 * sizeof(VMWCHAR), NOTES_DIR "\\");
    ascii_to_ucs2_const(out, 260 * sizeof(VMWCHAR), NOTES_DIR "\\");
    vm_wstrcat(out, (VMWSTR)name);
}

static VMBOOL is_txt_name(const VMWCHAR *name)
{
    VMINT n = vm_wstrlen((VMWSTR)name);
    VMWCHAR a, b, c, d;
    if (n < 4) return VM_FALSE;
    a = name[n - 4]; b = name[n - 3]; c = name[n - 2]; d = name[n - 1];
    if (a != '.') return VM_FALSE;
    if (b >= 'A' && b <= 'Z') b += ('a' - 'A');
    if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    if (d >= 'A' && d <= 'Z') d += ('a' - 'A');
    return (b == 't' && c == 'x' && d == 't') ? VM_TRUE : VM_FALSE;
}

static void refresh_notes(void)
{
    VMWCHAR pattern[260];
    vm_fileinfo_ext info;
    VMINT find_handle;
    g_note_count = 0;
    g_selected = 0;
    g_scroll = 0;
    build_search_pattern(pattern);
    find_handle = vm_find_first_ext(pattern, &info);
    if (find_handle < 0) return;

    do {
        if ((info.attributes & VM_FS_ATTR_DIR) == 0 && is_txt_name(info.filefullname)) {
            if (g_note_count < MAX_NOTES) {
                vm_wstrncpy(g_notes[g_note_count].name, info.filefullname, MAX_FILENAME - 1);
                g_notes[g_note_count].name[MAX_FILENAME - 1] = 0;
                g_note_count++;
            }
        }
    } while (vm_find_next_ext(find_handle, &info) == 0);
    vm_find_close_ext(find_handle);
}

static void draw_text(VMINT x, VMINT y, VMWSTR text, vm_graphic_color color)
{
    vm_graphic_setcolor(&color);
    vm_graphic_textout_to_layer(g_layer, x, y, text, vm_wstrlen(text));
}

static void draw_ascii(VMINT x, VMINT y, const char *text, vm_graphic_color color)
{
    VMWCHAR wide[128];
//    vm_ascii_to_ucs2(wide, sizeof(wide), (VMSTR)text);
    ascii_to_ucs2_const(wide, sizeof(wide), text);
    draw_text(x, y, wide, color);
}

static void draw_truncated(VMINT x, VMINT y, VMWSTR text, VMINT max_width, vm_graphic_color color)
{
    VMWCHAR tmp[MAX_FILENAME];
    VMINT len = vm_wstrlen(text);
    VMINT width;
    if (len >= MAX_FILENAME) len = MAX_FILENAME - 1;
    if (len <= 0 || max_width <= 0) return;
    vm_wstrncpy(tmp, text, len);
    tmp[len] = 0;
    width = vm_graphic_get_string_width(tmp);
    while (len > 1 && width > max_width) {
        tmp[--len] = 0;
        width = vm_graphic_get_string_width(tmp);
    }
    draw_text(x, y, tmp, color);
}

static void render_screen(void)
{
    VMINT i, row_h, body_h, footer_y;
    vm_graphic_color bg = rgb(18, 20, 26);
    vm_graphic_color header = rgb(34, 39, 50);
    vm_graphic_color text = rgb(225, 229, 238);
    vm_graphic_color selected = rgb(36, 90, 150);
    vm_graphic_color accent = rgb(80, 220, 150);
    vm_graphic_color popup = rgb(30, 34, 44);
    vm_graphic_color muted = rgb(150, 160, 176);
    char title[40];

    if (g_layer < 0) return;
    vm_graphic_active_layer(g_layer);
    vm_graphic_set_font(VM_SMALL_FONT);
    vm_graphic_setcolor(&bg);
    vm_graphic_fill_rect_ex(g_layer, 0, 0, g_screen_w, g_screen_h);
    vm_graphic_setcolor(&header);
    vm_graphic_fill_rect_ex(g_layer, 0, 0, g_screen_w, g_header_h);
    sprintf(title, "Notes (%d/%d)", g_note_count ? g_selected + 1 : 0, g_note_count);
    draw_ascii(6, 3, title, text);

    footer_y = g_screen_h - g_footer_h;
    row_h = g_font_h + 5;
    body_h = footer_y - g_header_h;
    g_visible_rows = body_h > 0 ? body_h / row_h : 1;
    if (g_visible_rows < 1) g_visible_rows = 1;

    if (g_note_count == 0) {
        draw_ascii(8, g_header_h + 12, "No notes found.", muted);
        draw_ascii(8, g_header_h + 12 + row_h, "Choose Options > Create new.", muted);
    } else {
        for (i = 0; i < g_visible_rows; i++) {
            VMINT idx = g_scroll + i;
            VMINT y = g_header_h + i * row_h;
            if (idx >= g_note_count) break;
            if (idx == g_selected) {
                vm_graphic_setcolor(&selected);
                vm_graphic_fill_rect_ex(g_layer, 3, y, g_screen_w - 8, row_h);
            }
            draw_truncated(8, y + 2, g_notes[idx].name, g_screen_w - 22,
                           idx == g_selected ? text : muted);
        }
    }

    vm_graphic_setcolor(&header);
    vm_graphic_fill_rect_ex(g_layer, 0, footer_y, g_screen_w, g_footer_h);
    draw_ascii(8, footer_y + 3, g_ui == UI_BROWSER ? "Options" : "Select", text);
    {
        VMWCHAR w_exit[16];
//        vm_ascii_to_ucs2(w_exit, sizeof(w_exit), g_ui == UI_BROWSER ? "Exit" : "Back");
        ascii_to_ucs2_const(w_exit, sizeof(w_exit), g_ui == UI_BROWSER ? "Exit" : "Back");
        draw_text(g_screen_w - vm_graphic_get_string_width(w_exit) - 8, footer_y + 3, w_exit, text);
    }
    if (g_status[0]) draw_text(6, g_header_h - 2, g_status, accent);

    if (g_ui == UI_MENU || g_ui == UI_DELETE_CONFIRM) {
        VMINT pw = g_screen_w - 28;
        VMINT ph = g_ui == UI_MENU ? (g_font_h + 8) * 3 + 12 : g_font_h * 3 + 24;
        VMINT px = 14;
        VMINT py = (g_screen_h - ph) / 2;
        vm_graphic_setcolor(&popup);
        vm_graphic_fill_rect_ex(g_layer, px, py, pw, ph);
        vm_graphic_setcolor(&accent);
        vm_graphic_rect_ex(g_layer, px, py, pw, ph);
        if (g_ui == UI_MENU) {
            const char *items[] = { "Create new", "Rename", "Delete" };
            for (i = 0; i < 3; i++) {
                VMINT y = py + 6 + i * (g_font_h + 8);
                if (i == g_selected) {
                    vm_graphic_setcolor(&selected);
                    vm_graphic_fill_rect_ex(g_layer, px + 4, y - 2, pw - 8, g_font_h + 6);
                }
                draw_ascii(px + 10, y, items[i], text);
            }
        } else {
            draw_ascii(px + 10, py + 8, "Delete selected note?", text);
            draw_ascii(px + 10, py + 8 + g_font_h + 4, "Yes: OK / No: Back", muted);
        }
    }
    {
        VMINT layer = g_layer;
        vm_graphic_flush_layer(&layer, 1);
    }
}

static void update_scroll(void)
{
    if (g_selected < 0) g_selected = 0;
    if (g_note_count > 0 && g_selected >= g_note_count) g_selected = g_note_count - 1;
    if (g_selected < g_scroll) g_scroll = g_selected;
    if (g_selected >= g_scroll + g_visible_rows) g_scroll = g_selected - g_visible_rows + 1;
    if (g_scroll < 0) g_scroll = 0;
}

static VMINT read_note_as_ucs2(VMWSTR path, VMWSTR *out_text)
{
    VMFILE file;
    VMUINT size = 0, nread = 0;
    VMCHAR *utf8;
    VMWCHAR *wide;
    VMINT wide_bytes;
    file = vm_file_open(path, MODE_READ, VM_FALSE);
    if (file < 0) return -1;
    if (vm_file_getfilesize(file, &size) != 0 || size > MAX_NOTE_BYTES) {
        vm_file_close(file);
        return -2;
    }
    utf8 = (VMCHAR *)vm_malloc(size + 1);
    wide_bytes = (VMINT)((size + 1) * sizeof(VMWCHAR));
    wide = (VMWCHAR *)vm_malloc(wide_bytes);
    if (!utf8 || !wide) {
        if (utf8) vm_free(utf8);
        if (wide) vm_free(wide);
        vm_file_close(file);
        return -3;
    }
    if (size > 0 && vm_file_read(file, utf8, size, &nread) < 0) {
        vm_free(utf8); vm_free(wide); vm_file_close(file); return -4;
    }
    vm_file_close(file);
    utf8[nread] = 0;
    memset(wide, 0, wide_bytes);
    if (nread > 0) vm_chset_convert(VM_CHSET_UTF8, VM_CHSET_UCS2, utf8, (VMCHAR *)wide, wide_bytes);
    wide[MAX_NOTE_CHARS] = 0;
    vm_free(utf8);
    *out_text = wide;
    return 0;
}

static VMINT save_note(VMWSTR path, VMWSTR text)
{
    VMFILE file;
    VMINT chars = vm_wstrlen(text);
    VMINT out_bytes = (chars * 3) + 8;
    VMCHAR *utf8;
    VMUINT written = 0;
    if (chars > MAX_NOTE_CHARS) chars = MAX_NOTE_CHARS;
    utf8 = (VMCHAR *)vm_malloc(out_bytes);
    if (!utf8) return -1;
    memset(utf8, 0, out_bytes);
    if (chars > 0 && vm_chset_convert(VM_CHSET_UCS2, VM_CHSET_UTF8, (VMCHAR *)text, utf8, out_bytes) != 0) {
        vm_free(utf8);
        return -2;
    }
    file = vm_file_open(path, MODE_CREATE_ALWAYS_WRITE, VM_FALSE);
    if (file < 0) { vm_free(utf8); return -3; }
    if (strlen(utf8) > 0 && vm_file_write(file, utf8, strlen(utf8), &written) < 0) {
        vm_file_close(file); vm_free(utf8); return -4;
    }
    vm_file_close(file);
    vm_free(utf8);
    return 0;
}

static void start_editor(void)
{
    VMWCHAR *text = NULL;
    VMINT rc;
    if (g_current_path[0] == 0) return;
    rc = read_note_as_ucs2(g_current_path, &text);
    if (rc < 0) {
        text = (VMWCHAR *)vm_malloc(sizeof(VMWCHAR));
        if (!text) { set_status("Not enough memory"); render_screen(); return; }
        text[0] = 0;
    }
    g_input_action = INPUT_NONE;
    vm_input_set_editor_title(g_notes[g_selected].name);
    rc = vm_input_text3(text, MAX_NOTE_CHARS, VM_INPUT_METHOD_ALPHABETIC, editor_input_cb);
    vm_free(text);
    if (rc < 0) { set_status("Could not open editor"); render_screen(); }
}

static void open_selected_note(void)
{
    if (g_note_count <= 0) return;
    build_note_path(g_current_path, g_notes[g_selected].name);
    start_editor();
}

static VMBOOL valid_note_name(VMWSTR name)
{
    VMINT i, len = vm_wstrlen(name);
    if (len <= 0 || len >= MAX_FILENAME - 5) return VM_FALSE;
    if ((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.')) return VM_FALSE;
    for (i = 0; i < len; i++) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == ':' || name[i] == '*' ||
            name[i] == '?' || name[i] == '"' || name[i] == '<' || name[i] == '>' || name[i] == '|') return VM_FALSE;
    }
    return VM_TRUE;
}

static void append_txt_extension(VMWSTR out, VMWSTR input)
{
    vm_wstrncpy(out, input, MAX_FILENAME - 5);
    out[MAX_FILENAME - 5] = 0;
    if (!is_txt_name(out)) {
        VMWCHAR ext[5];
//        vm_ascii_to_ucs2(ext, sizeof(ext), ".txt");
        ascii_to_ucs2_const(ext, sizeof(ext), ".txt");
        vm_wstrcat(out, ext);
    }
}

static void start_new_note(void)
{
    VMWCHAR empty[1] = { 0 };
    g_input_action = INPUT_NEW_NAME;
    vm_input_set_editor_title((VMWSTR)empty);
    if (vm_input_text3(empty, MAX_FILENAME - 1, VM_INPUT_METHOD_ALPHABETIC, filename_input_cb) < 0) {
        g_input_action = INPUT_NONE;
        set_status("Could not open name entry");
        render_screen();
    }
}

static void filename_input_cb(VMINT state, VMWSTR text)
{
    VMWCHAR name[MAX_FILENAME];
    VMWCHAR path[260];
    VMFILE existing;
    if (state != VM_INPUT_OK || !text || !valid_note_name(text)) {
        g_input_action = INPUT_NONE;
        render_screen();
        return;
    }
    append_txt_extension(name, text);
    build_note_path(path, name);
    existing = vm_file_open(path, MODE_READ, VM_FALSE);
    if (existing >= 0) {
        vm_file_close(existing);
        set_status("That note name already exists");
        g_input_action = INPUT_NONE;
        render_screen();
        return;
    }
    vm_wstrcpy(g_current_path, path);
    g_input_action = INPUT_NONE;
    start_editor();
}

static void editor_input_cb(VMINT state, VMWSTR text)
{
    if (state == VM_INPUT_OK && text) {
        if (save_note(g_current_path, text) < 0) set_status("Could not save note");
        else set_status("Saved");
        refresh_notes();
    }
    g_current_path[0] = 0;
    render_screen();
}

static void rename_input_cb(VMINT state, VMWSTR text)
{
    VMWCHAR name[MAX_FILENAME];
    VMWCHAR new_path[260];
    if (state == VM_INPUT_OK && text && valid_note_name(text)) {
        append_txt_extension(name, text);
        build_note_path(new_path, name);
        if (vm_file_rename(g_current_path, new_path) == 0) set_status("Renamed");
        else set_status("Rename failed");
        refresh_notes();
    }
    g_current_path[0] = 0;
    g_ui = UI_BROWSER;
    render_screen();
}

static void begin_rename(void)
{
    if (g_note_count <= 0) { set_status("No note selected"); g_ui = UI_BROWSER; render_screen(); return; }
    build_note_path(g_current_path, g_notes[g_selected].name);
    vm_input_set_editor_title(g_notes[g_selected].name);
    if (vm_input_text3(g_notes[g_selected].name, MAX_FILENAME - 1,
                       VM_INPUT_METHOD_ALPHABETIC, rename_input_cb) < 0) {
        g_current_path[0] = 0;
        set_status("Could not open rename dialog");
        g_ui = UI_BROWSER;
        render_screen();
    }
}

static void delete_selected_note(void)
{
    if (g_note_count > 0) {
        build_note_path(g_current_path, g_notes[g_selected].name);
        if (vm_file_delete(g_current_path) == 0) set_status("Deleted");
        else set_status("Delete failed");
        g_current_path[0] = 0;
        refresh_notes();
    }
    g_ui = UI_BROWSER;
    render_screen();
}

static void activate_menu_item(void)
{
    g_ui = UI_BROWSER;
    switch (g_selected) {
    case 0: start_new_note(); return;
    case 1: begin_rename(); return;
    case 2:
        if (g_note_count > 0) g_ui = UI_DELETE_CONFIRM;
        else set_status("No note selected");
        break;
    }
    render_screen();
}

static void handle_key(VMINT event, VMINT keycode)
{
    if (event != VM_KEY_EVENT_DOWN && event != VM_KEY_EVENT_REPEAT) return;
    if (g_ui == UI_DELETE_CONFIRM) {
        if (keycode == VM_KEY_OK || keycode == VM_KEY_LEFT_SOFTKEY) delete_selected_note();
        else if (keycode == VM_KEY_RIGHT_SOFTKEY || keycode == VM_KEY_CLEAR || keycode == VM_KEY_BACK) {
            g_ui = UI_BROWSER; render_screen();
        }
        return;
    }
    if (g_ui == UI_MENU) {
        if (keycode == VM_KEY_UP) g_selected = (g_selected + 2) % 3;
        else if (keycode == VM_KEY_DOWN) g_selected = (g_selected + 1) % 3;
        else if (keycode == VM_KEY_OK || keycode == VM_KEY_LEFT_SOFTKEY) activate_menu_item();
        else if (keycode == VM_KEY_RIGHT_SOFTKEY || keycode == VM_KEY_CLEAR || keycode == VM_KEY_BACK) {
            g_ui = UI_BROWSER;
        }
        render_screen();
        return;
    }
    if (keycode == VM_KEY_LEFT_SOFTKEY) {
        g_selected = 0; g_ui = UI_MENU; render_screen(); return;
    }
    if (keycode == VM_KEY_RIGHT_SOFTKEY || keycode == VM_KEY_BACK || keycode == VM_KEY_CLEAR) {
        vm_exit_app(); return;
    }
    if (keycode == VM_KEY_UP) {
        if (g_note_count > 0 && g_selected > 0) g_selected--;
    } else if (keycode == VM_KEY_DOWN) {
        if (g_note_count > 0 && g_selected < g_note_count - 1) g_selected++;
    } else if (keycode == VM_KEY_OK) {
        open_selected_note(); return;
    }
    update_scroll();
    render_screen();
}

static void handle_pen(VMINT event, VMINT x, VMINT y)
{
    VMINT row_h = g_font_h + 5;
    if (event != VM_PEN_EVENT_TAP) return;
    if (y >= g_screen_h - g_footer_h) {
        handle_key(VM_KEY_EVENT_DOWN, x < g_screen_w / 2 ? VM_KEY_LEFT_SOFTKEY : VM_KEY_RIGHT_SOFTKEY);
        return;
    }
    if (g_ui == UI_MENU) {
        VMINT py = (g_screen_h - ((g_font_h + 8) * 3 + 12)) / 2;
        VMINT row = (y - py - 6) / (g_font_h + 8);
        if (row >= 0 && row < 3) { g_selected = row; activate_menu_item(); }
        return;
    }
    if (g_ui == UI_DELETE_CONFIRM) {
        if (x < g_screen_w / 2) delete_selected_note();
        else { g_ui = UI_BROWSER; render_screen(); }
        return;
    }
    if (y >= g_header_h && y < g_screen_h - g_footer_h && g_note_count > 0) {
        VMINT idx = g_scroll + (y - g_header_h) / row_h;
        if (idx >= 0 && idx < g_note_count) {
            if (idx == g_selected) open_selected_note();
            else { g_selected = idx; update_scroll(); render_screen(); }
        }
    }
}

static void handle_sysevt(VMINT message, VMINT param)
{
    switch (message) {
    case VM_MSG_CREATE:
        g_screen_w = vm_graphic_get_screen_width();
        g_screen_h = vm_graphic_get_screen_height();
        vm_graphic_set_font(VM_SMALL_FONT);
        g_font_h = vm_graphic_get_character_height();
        if (g_font_h <= 0) g_font_h = 14;
        g_header_h = g_font_h + 8;
        g_footer_h = g_font_h + 8;
//        vm_ascii_to_ucs2(g_folder, sizeof(g_folder), NOTES_DIR);
        ascii_to_ucs2_const(g_folder, sizeof(g_folder), NOTES_DIR);
        vm_file_mkdir(g_folder); /* succeeds when absent; existing folder is harmless */
        g_layer = vm_graphic_create_layer(0, 0, g_screen_w, g_screen_h, VM_NO_TRANS_COLOR);
        vm_graphic_active_layer(g_layer);
        refresh_notes();
        render_screen();
        if (g_note_count == 0) start_new_note();
        break;
    case VM_MSG_ACTIVE:
    case VM_MSG_PAINT:
        if (g_layer >= 0) { vm_graphic_active_layer(g_layer); render_screen(); }
        break;
    case VM_MSG_QUIT:
        if (g_layer >= 0) vm_graphic_delete_layer(g_layer);
        g_layer = -1;
        break;
    }
}

void vm_main(void)
{
    vm_reg_sysevt_callback(handle_sysevt);
    vm_reg_keyboard_callback(handle_key);
    vm_reg_pen_callback(handle_pen);
}

