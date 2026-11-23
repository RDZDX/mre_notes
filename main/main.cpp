#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vmchset.h"
#include "vmeditor.h"
#include "vmgraph.h"
#include "vmio.h"
#include "vmstdlib.h"
#include "vmsys.h"

#define NOTES_DIR "Notes"
#define MAX_PATH_LEN 260
#define MAX_NOTES 128
#define MAX_FILENAME 96
//#define MAX_NOTE_CHARS 4096
#define MAX_NOTE_CHARS  1024
#define MAX_NOTE_BYTES (64 * 1024)
#define EDITOR_BUFFER_BYTES ((MAX_NOTE_CHARS + 1) * sizeof(VMWCHAR))

typedef enum { UI_BROWSER, UI_MENU, UI_DELETE_CONFIRM } UiMode;
typedef enum { INPUT_NONE, INPUT_NEW_NAME, INPUT_RENAME } InputAction;
typedef struct {
    VMWCHAR name[MAX_FILENAME];
} NoteEntry;

static VMINT g_layer = -1;
static VMINT g_screen_w, g_screen_h, g_font_h, g_header_h, g_footer_h;
static VMINT g_selected, g_scroll, g_note_count, g_visible_rows;
static UiMode g_ui = UI_BROWSER;
static InputAction g_input_action = INPUT_NONE;
static VMINT g_menu_selected = 0;
static NoteEntry g_notes[MAX_NOTES];
static VMWCHAR g_current_path[260];
static VMWCHAR g_folder[32];
static VMWCHAR g_status[96];

static VMINT32 g_editor = 0;
static VMWCHAR* g_editor_buffer = NULL;
static VMINT g_editor_buffer_bytes = 0;
static VMINT g_editor_text_len = 0;
static VMBOOL g_editor_suspended = VM_FALSE;
static VMWCHAR g_edit_title[MAX_FILENAME];
static vm_editor_font_attribute g_editor_font = {0, 0, 0, 18};
static vm_editor_font_attribute g_editor_font1 = {0, 0, 0, 20};
static vm_input_mode_enum g_editor_input_modes[] = {VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, VM_INPUT_MODE_123, VM_INPUT_MODE_123_SYMBOLS, VM_INPUT_MODE_NONE};
static VMINT g_softkey_h;
static VMWCHAR g_editor_left_label[16];
static VMWCHAR g_editor_center_label[16];
static VMWCHAR g_editor_right_label[16];
static VMWCHAR g_editor_right_label1[16];
static VMBOOL g_renaming = VM_FALSE;
static VMBOOL runing_rename = VM_FALSE;
static VMCHAR c_choose[7] = "Choose";
static VMWCHAR w_choose[15];

static void refresh_notes(void);
static void open_selected_note(void);
static void start_new_note(void);
static void start_editor(void);
static void render_screen(void);
static void draw_ascii(VMINT x, VMINT y, const char* text, vm_graphic_color color);
static void filename_input_cb(VMINT state, VMWSTR text);
static void rename_input_cb(VMINT state, VMWSTR text);
static void delete_selected_note(void);
static void handle_key(VMINT event, VMINT keycode);
static void handle_pen(VMINT event, VMINT x, VMINT y);
static void handle_sysevt(VMINT message, VMINT param);
static void update_scroll(void);
static void close_editor(VMBOOL save);
static VMUINT32 editor_ime_cb(VMINT32 handle, vm_editor_message_struct_p message);
static void editor_text_changed_cb(VMUINT8* text, VMUINT8* cursor, VMINT32 text_length);
void create_auto_filename(VMWSTR text);
void create_default_path(void);
static void left_softkey_cb(void);
static void center_softkey_cb(void);
static void right_softkey_cb(void);
static void save_editor_action(void);

static VMINT ascii_to_ucs2_const(VMWSTR dst, VMINT size, const char* src)
{
    if (!dst || size <= 0) return -1;
    return vm_ascii_to_ucs2(dst, size, (VMSTR)(src ? src : ""));
}

static vm_graphic_color rgb(VMUINT8 r, VMUINT8 g, VMUINT8 b)
{
    vm_graphic_color c;
    c.vm_color_565 = VM_COLOR_888_TO_565(r, g, b);
    c.vm_color_888 = ((VMUINT32)b << 16) | ((VMUINT32)g << 8) | r;
    return c;
}

static void set_status(const char* message)
{
    ascii_to_ucs2_const(g_status, sizeof(g_status), message);
}

static void build_search_pattern(VMWSTR out)
{
    VMWCHAR suffix[16];

    vm_ascii_to_ucs2(suffix, sizeof(suffix), (VMCHAR*)"\\*.txt");
    vm_wstrcpy(out, g_folder);
    vm_wstrcat(out, suffix);
}

static void build_note_path(VMWSTR out, const VMWCHAR* name)
{
    VMWCHAR slash[4];

    vm_ascii_to_ucs2(slash, sizeof(slash), (VMCHAR*)"\\");
    vm_wstrcpy(out, g_folder);
    vm_wstrcat(out, slash);
    vm_wstrcat(out, (VMWSTR)name);
}

static VMBOOL is_txt_name(const VMWCHAR* name)
{
    VMINT n = vm_wstrlen((VMWSTR)name);
    VMWCHAR a, b, c, d;
    if (n < 4) return VM_FALSE;
    a = name[n - 4];
    b = name[n - 3];
    c = name[n - 2];
    d = name[n - 1];
    if (a != '.') return VM_FALSE;
    if (b >= 'A' && b <= 'Z') b += ('a' - 'A');
    if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    if (d >= 'A' && d <= 'Z') d += ('a' - 'A');
    return (b == 't' && c == 'x' && d == 't') ? VM_TRUE : VM_FALSE;
}

static void refresh_notes(void)
{
    VMWCHAR pattern[260], previously_selected[MAX_FILENAME];
    vm_fileinfo_ext info;
    VMINT find_handle, old_selected = g_selected;
    VMBOOL have_previous = VM_FALSE;

    previously_selected[0] = 0;
    if (g_note_count > 0 && g_selected >= 0 && g_selected < g_note_count) {
        vm_wstrncpy(previously_selected, g_notes[g_selected].name, MAX_FILENAME - 1);
        previously_selected[MAX_FILENAME - 1] = 0;
        have_previous = VM_TRUE;
    }
    g_note_count = 0;
    build_search_pattern(pattern);
    find_handle = vm_find_first_ext(pattern, &info);
    if (find_handle >= 0) {
        do {
            if ((info.attributes & VM_FS_ATTR_DIR) == 0 && is_txt_name(info.filefullname) && g_note_count < MAX_NOTES) {
                vm_wstrncpy(g_notes[g_note_count].name, info.filefullname, MAX_FILENAME - 1);
                g_notes[g_note_count].name[MAX_FILENAME - 1] = 0;
                g_note_count++;
            }
        } while (vm_find_next_ext(find_handle, &info) == 0);
        vm_find_close_ext(find_handle);
    }
    if (g_note_count <= 0) {
        g_selected = 0;
        g_scroll = 0;
        return;
    }
    if (have_previous) {
        VMINT i;
        VMBOOL found = VM_FALSE;
        for (i = 0; i < g_note_count; i++) {
            if (vm_wstrcmp(g_notes[i].name, previously_selected) == 0) {
                g_selected = i;
                found = VM_TRUE;
                break;
            }
        }
        if (!found) g_selected = old_selected;
    }
    if (g_selected < 0) g_selected = 0;
    if (g_selected >= g_note_count) g_selected = g_note_count - 1;
    if (g_selected < g_scroll) g_scroll = g_selected;
    if (g_visible_rows > 0 && g_selected >= g_scroll + g_visible_rows)
        g_scroll = g_selected - g_visible_rows + 1;
    if (g_scroll < 0) g_scroll = 0;
}

static void draw_text(VMINT x, VMINT y, VMWSTR text, vm_graphic_color color)
{
    vm_graphic_setcolor(&color);
    vm_graphic_textout_to_layer(g_layer, x, y, text, vm_wstrlen(text));
}

static void draw_ascii(VMINT x, VMINT y, const char* text, vm_graphic_color color)
{
    VMWCHAR wide[128];
    ascii_to_ucs2_const(wide, sizeof(wide), text);
    draw_text(x, y, wide, color);
}

static void draw_truncated(VMINT x, VMINT y, VMWSTR text, VMINT max_width, vm_graphic_color color)
{
    VMWCHAR tmp[MAX_FILENAME];
    VMWCHAR dots[4];

    VMINT len;

    if (!text || max_width <= 0) return;

    len = vm_wstrlen(text);

    if (is_txt_name(text)) len -= 4;

    if (len >= MAX_FILENAME) len = MAX_FILENAME - 1;

    if (len <= 0) return;

    vm_wstrncpy(tmp, text, len);
    tmp[len] = 0;

    ascii_to_ucs2_const(dots, sizeof(dots), "");
    //    vm_graphic_show_truncated_text(g_layer, x, y, max_width, tmp, dots, 0, color.vm_color_565);
    vm_graphic_show_truncated_text(g_layer, x, y, g_screen_w - 7, tmp, dots, 0, color.vm_color_565);
}

static void render_screen(void)
{
    VMINT i, row_h, body_h, footer_y;
    //    vm_graphic_color bg = rgb(18, 20, 26), header = rgb(34, 39, 50);
    //    vm_graphic_color text = rgb(225, 229, 238), selected = rgb(36, 90, 150);
    ////    vm_graphic_color accent = rgb(80, 220, 150), popup = rgb(30, 34, 44);
    //    vm_graphic_color accent = rgb(225, 229, 238), popup = rgb(30, 34, 44);
    //    vm_graphic_color muted = rgb(150, 160, 176);

    //    vm_graphic_color bg = rgb(255, 255, 255), header = rgb(0, 0, 0);
    //    vm_graphic_color text = rgb(225, 0, 0), selected = rgb(225, 229, 238);
    //    vm_graphic_color accent = rgb(0, 225, 0), popup = rgb(0, 0, 0);
    //    vm_graphic_color muted = rgb(255, 0, 255);

    //    vm_graphic_color bg = rgb(255, 255, 255), header = rgb(0, 0, 0);
    //    vm_graphic_color text = rgb(255,255,255), selected = rgb(225,229,238);
    //    vm_graphic_color accent = rgb(0, 255, 0), popup = rgb(34,39,50);
    //    vm_graphic_color muted = rgb(0,0,0);

    vm_graphic_color bg = rgb(255, 255, 153), header = rgb(0, 0, 0);
    vm_graphic_color text = rgb(255, 255, 255), selected = rgb(122, 91, 84);
    vm_graphic_color accent = rgb(0, 255, 0), popup = rgb(34, 39, 50);
    vm_graphic_color muted = rgb(0, 0, 0);

    char title[40];
    if (g_layer < 0) return;
    vm_graphic_active_layer(g_layer);
    vm_graphic_set_font(VM_SMALL_FONT);  // VM_MEDIUM_FONT
    vm_graphic_setcolor(&bg);
    vm_graphic_fill_rect_ex(g_layer, 0, 0, g_screen_w, g_screen_h);
    vm_graphic_setcolor(&header);
    vm_graphic_fill_rect_ex(g_layer, 0, 0, g_screen_w, g_header_h);
    sprintf(title, "Notes (%d/%d)", g_note_count ? g_selected + 1 : 0, g_note_count);
    draw_ascii(8, ((g_header_h - g_font_h) / 2) + 3, title, text);

    footer_y = g_screen_h - g_footer_h;
    row_h = g_font_h + 5;
    body_h = footer_y - g_header_h;
    g_visible_rows = body_h > 0 ? body_h / row_h : 1;
    if (g_visible_rows < 1) g_visible_rows = 1;
    if (g_note_count == 0) {
        VMINT w = 0;
        //        draw_ascii(8, g_header_h + 12, "No notes found.", muted);
        //        draw_ascii(8, g_header_h + 12 + row_h, "Choose Options > Create new.", muted);
    } else {
        vm_graphic_set_font(VM_MEDIUM_FONT);  // VM_MEDIUM_FONT VM_SMALL_FONT

        for (i = 0; i < g_visible_rows; i++) {
            VMINT idx = g_scroll + i,
                  y = g_header_h + i * row_h;
            if (idx >= g_note_count) break;
            if (idx == g_selected) {
                vm_graphic_setcolor(&selected);
                vm_graphic_fill_rect_ex(g_layer, 3, y, g_screen_w - 8, row_h);
            }
            draw_truncated(3, y + 5, g_notes[idx].name, g_screen_w - 22, idx == g_selected ? text : muted);
        }
    }
    vm_graphic_setcolor(&header);
    vm_graphic_fill_rect_ex(g_layer, 0, footer_y, g_screen_w, g_footer_h);
    vm_graphic_set_font(VM_SMALL_FONT);
    draw_ascii(3, (footer_y + (g_footer_h - g_font_h) / 2) + 7, g_ui == UI_BROWSER ? "Options" : "", text);
//    draw_ascii(99, (footer_y + (g_footer_h - g_font_h) / 2) + 7, g_ui == UI_BROWSER ? " Open" : "Choose", text);
    draw_ascii((g_screen_w - vm_graphic_get_string_width(w_choose)) / 2, (footer_y + (g_footer_h - g_font_h) / 2) + 7, g_ui == UI_BROWSER ? "  Open" : "Choose", text);
    {
        VMWCHAR w_exit[16];
        ascii_to_ucs2_const(w_exit, sizeof(w_exit), g_ui == UI_BROWSER ? "Exit" : "Back");
        draw_text(g_screen_w - vm_graphic_get_string_width(w_exit) - 3, 7 + footer_y + (g_footer_h - g_font_h) / 2, w_exit, text);
    }

    if (g_ui == UI_MENU || g_ui == UI_DELETE_CONFIRM) {
        VMINT pw = g_screen_w - 28;
        VMINT ph = g_ui == UI_MENU ? (g_font_h + 8) * 3 + 12 : g_font_h * 3 + 24;
        VMINT px = 14, py = (g_screen_h - ph) / 2;
        vm_graphic_setcolor(&popup);

        if (g_ui == UI_MENU)
            vm_graphic_fill_rect_ex(g_layer, 0, g_screen_h - (4 * g_softkey_h), g_screen_w, 3 * g_softkey_h);

        if (g_ui == UI_DELETE_CONFIRM)
            vm_graphic_fill_rect_ex(g_layer, 0, g_screen_h - (8 * g_softkey_h), g_screen_w, 7 * g_softkey_h);

        vm_graphic_setcolor(&accent);
        if (g_ui == UI_MENU) {
            vm_graphic_set_font(VM_MEDIUM_FONT);  // VM_SMALL_FONT
            const char* items[] = {"Create new", "Rename", "Delete"};
            for (i = 0; i < 3; i++) {
                if (i == g_menu_selected) {
                    vm_graphic_setcolor(&selected);
                    vm_graphic_fill_rect_ex(
                        g_layer, 0, g_screen_h - ((4 - i) * g_softkey_h), g_screen_w, g_softkey_h);
                }
                draw_ascii(0 + 5, 9 + g_screen_h - ((4 - i) * g_softkey_h), items[i], text);
            }
        } else {
            vm_graphic_set_font(VM_MEDIUM_FONT);  // VM_SMALL_FONT
            draw_ascii(px + 5, py + 8, "Delete selected note?", text);
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
    if (g_note_count > 0 && g_selected >= g_note_count)
        g_selected = g_note_count - 1;
    if (g_selected < g_scroll) g_scroll = g_selected;
    if (g_selected >= g_scroll + g_visible_rows)
        g_scroll = g_selected - g_visible_rows + 1;
    if (g_scroll < 0) g_scroll = 0;
}

static VMINT read_note_as_ucs2(VMWSTR path, VMWSTR out_text, VMINT out_bytes)
{
    VMFILE file;
    VMUINT size = 0, nread = 0;
    VMCHAR* utf8;
    file = vm_file_open(path, MODE_READ, VM_FALSE);
    if (file < 0) return -1;
    if (vm_file_getfilesize(file, &size) != 0 || size > MAX_NOTE_BYTES) {
        vm_file_close(file);
        return -2;
    }
    utf8 = (VMCHAR*)vm_malloc(size + 1);
    if (!utf8) {
        vm_file_close(file);
        return -3;
    }
    if (size > 0 && vm_file_read(file, utf8, size, &nread) < 0) {
        vm_free(utf8);
        vm_file_close(file);
        return -4;
    }
    vm_file_close(file);
    utf8[nread] = 0;
    memset(out_text, 0, out_bytes);
    if (nread > 0 && vm_chset_convert(VM_CHSET_UTF8, VM_CHSET_UCS2, utf8, (VMCHAR*)out_text, out_bytes) != VM_CHSET_CONVERT_SUCCESS) {
        vm_free(utf8);
        return -5;
    }
    out_text[MAX_NOTE_CHARS] = 0;
    vm_free(utf8);
    return 0;
}

static VMINT save_note(VMWSTR path, VMWSTR text)
{
    VMFILE file;
    VMINT chars = vm_wstrlen(text), out_bytes;
    VMCHAR* utf8;
    VMUINT written = 0;
    if (chars > MAX_NOTE_CHARS) chars = MAX_NOTE_CHARS;
    out_bytes = chars * 3 + 8;
    utf8 = (VMCHAR*)vm_malloc(out_bytes);
    if (!utf8) return -1;
    memset(utf8, 0, out_bytes);
    if (chars > 0 && vm_chset_convert(VM_CHSET_UCS2, VM_CHSET_UTF8, (VMCHAR*)text, utf8, out_bytes) != VM_CHSET_CONVERT_SUCCESS) {
        vm_free(utf8);
        return -2;
    }
    file = vm_file_open(path, MODE_CREATE_ALWAYS_WRITE, VM_FALSE);
    if (file < 0) {
        vm_free(utf8);
        return -3;
    }
    if (strlen(utf8) > 0 &&
        vm_file_write(file, utf8, strlen(utf8), &written) < 0) {
        vm_file_close(file);
        vm_free(utf8);
        return -4;
    }
    vm_file_close(file);
    vm_free(utf8);
    return 0;
}

static VMUINT32 editor_ime_cb(VMINT32 handle, vm_editor_message_struct_p message)
{
    (void)handle;
    (void)message;
    return 0;
}

static void editor_text_changed_cb(VMUINT8* text, VMUINT8* cursor, VMINT32 text_length)
{
    (void)text;
    (void)cursor;
    if (text_length < 0) text_length = 0;
    if (text_length > MAX_NOTE_CHARS) text_length = MAX_NOTE_CHARS;
    g_editor_text_len = text_length;
}

static void close_editor(VMBOOL save)
{
    VMINT save_result = 0;

    if (g_editor == 0) return;

    if (save && g_editor_buffer) {
        vm_editor_get_text(g_editor, (VMUWSTR)g_editor_buffer, g_editor_buffer_bytes);
        g_editor_text_len = vm_wstrlen(g_editor_buffer);
        if (g_editor_text_len > MAX_NOTE_CHARS) {
            g_editor_text_len = MAX_NOTE_CHARS;
            g_editor_buffer[g_editor_text_len] = 0;
        }
        save_result = save_note(g_current_path, g_editor_buffer);
    }

    vm_editor_deactivate(g_editor);
    vm_editor_close(g_editor);
    g_editor = 0;
    g_editor_suspended = VM_FALSE;

    if (g_editor_buffer) {
        vm_free(g_editor_buffer);
        g_editor_buffer = NULL;
    }
    g_editor_buffer_bytes = 0;
    g_editor_text_len = 0;

    if (save) {
        if (save_result == 0)
            set_status("Saved");
        else
            set_status("Save failed");
        if (save_result == 0) refresh_notes();
    }

    g_current_path[0] = 0;
    g_ui = UI_BROWSER;
    render_screen();
}

static void start_editor(void)
{
    VMINT rc;
    VMINT editor_h;

    if (g_current_path[0] == 0 || g_editor != 0) return;

    if (g_layer < 0 || g_screen_w <= 0 || g_screen_h <= 0) {
        set_status("Invalid display layer or size");
        render_screen();
        return;
    }

    editor_h = g_screen_h - vm_editor_get_softkey_height();
    if (editor_h <= 0) {
        set_status("Invalid editor size");
        render_screen();
        return;
    }

    g_editor_buffer_bytes = EDITOR_BUFFER_BYTES;
    g_editor_buffer = (VMWCHAR*)vm_malloc(g_editor_buffer_bytes);
    if (!g_editor_buffer) {
        g_editor_buffer_bytes = 0;
        set_status("Not enough memory");
        render_screen();
        return;
    }

    memset(g_editor_buffer, 0, g_editor_buffer_bytes);

    rc = read_note_as_ucs2(g_current_path, g_editor_buffer, g_editor_buffer_bytes);
    if (rc < 0 && rc != -1) {
        vm_free(g_editor_buffer);
        g_editor_buffer = NULL;
        g_editor_buffer_bytes = 0;
        set_status("Could not read note");
        render_screen();
        return;
    }

    g_editor_text_len = vm_wstrlen(g_editor_buffer);

    {
        vm_graphic_color clear = rgb(255, 255, 255); /* editor background */
        VMINT layer = g_layer;

        vm_graphic_active_layer(g_layer);
        vm_graphic_setcolor(&clear);
        vm_graphic_fill_rect_ex(g_layer, 0, 0, g_screen_w, g_screen_h);
        vm_graphic_flush_layer(&layer, 1);
    }

    g_editor = vm_editor_create(VM_EDITOR_MULTILINE, 0, 0, g_screen_w, editor_h, (VMUWSTR)g_editor_buffer, g_editor_buffer_bytes, VM_FALSE, g_layer);

    if (g_editor == 0) {
        vm_free(g_editor_buffer);
        g_editor_buffer = NULL;
        g_editor_buffer_bytes = 0;
        g_editor_text_len = 0;
        set_status("Could not create editor");
        render_screen();
        return;
    }

    vm_editor_set_multiline_text_font(g_editor, g_editor_font);
    vm_editor_set_bg_border_style(g_editor, VM_EDITOR_NO_BORDER, 0xFFFF, 0xFFFF);
    vm_editor_set_margin(g_editor, 0, 0, 0, 0);
    vm_editor_set_IME(g_editor, VM_INPUT_TYPE_SENTENCE, g_editor_input_modes, VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, editor_ime_cb);
    vm_editor_set_softkey(g_editor, (VMUWSTR)g_editor_right_label, VM_LEFT_SOFTKEY, right_softkey_cb);
    vm_editor_set_softkey(g_editor, (VMUWSTR)g_editor_center_label, VM_CENTER_SOFTKEY, center_softkey_cb);
    vm_editor_set_softkey(g_editor, (VMUWSTR)g_editor_right_label1, VM_RIGHT_SOFTKEY, right_softkey_cb);

    vm_editor_activate(g_editor, VM_FALSE);
    vm_editor_show(g_editor);
    vm_editor_redraw_ime_screen();
}

static void open_selected_note(void)
{
    if (g_note_count <= 0) return;
    build_note_path(g_current_path, g_notes[g_selected].name);
    vm_wstrncpy(g_edit_title, g_notes[g_selected].name, MAX_FILENAME - 1);
    g_edit_title[MAX_FILENAME - 1] = 0;
    start_editor();
}

static VMBOOL valid_note_name(VMWSTR name)
{
    VMINT i, len = vm_wstrlen(name);
    if (len <= 0 || len >= MAX_FILENAME - 5) return VM_FALSE;
    if ((len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.'))
        return VM_FALSE;
    for (i = 0; i < len; i++) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == ':' ||
            name[i] == '*' || name[i] == '?' || name[i] == '"' ||
            name[i] == '<' || name[i] == '>' || name[i] == '|')
            return VM_FALSE;
    }
    return VM_TRUE;
}

static void append_txt_extension(VMWSTR out, VMWSTR input)
{
    vm_wstrncpy(out, input, MAX_FILENAME - 5);
    out[MAX_FILENAME - 5] = 0;
    if (!is_txt_name(out)) {
        VMWCHAR ext[5];
        ascii_to_ucs2_const(ext, sizeof(ext), ".txt");
        vm_wstrcat(out, ext);
    }
}

static void start_new_note(void)
{
    g_renaming = VM_FALSE;
    runing_rename = VM_FALSE;

    create_auto_filename(g_current_path);
    g_edit_title[0] = 0;

    start_editor();
}

static void filename_input_cb(VMINT state, VMWSTR text)
{
    VMWCHAR name[MAX_FILENAME], path[260];
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
    vm_wstrncpy(g_edit_title, name, MAX_FILENAME - 1);
    g_edit_title[MAX_FILENAME - 1] = 0;
    g_input_action = INPUT_NONE;
    start_editor();
}

static void rename_input_cb(VMINT state, VMWSTR text)
{
    VMWCHAR name[MAX_FILENAME], new_path[260];
    if (state == VM_INPUT_OK && text && valid_note_name(text)) {
        append_txt_extension(name, text);
        build_note_path(new_path, name);
        if (vm_file_rename(g_current_path, new_path) == 0)
            set_status("Renamed");
        else
            set_status("Rename failed");
        refresh_notes();
    }
    g_current_path[0] = 0;
    g_ui = UI_BROWSER;
    runing_rename = VM_FALSE;
    render_screen();
}

static void begin_rename(void)
{
    VMINT len;

    if (g_note_count <= 0) {
        set_status("No note selected");
        g_ui = UI_BROWSER;
        render_screen();
        return;
    }
    if (g_editor != 0) return;

    build_note_path(g_current_path, g_notes[g_selected].name);

    len = vm_wstrlen(g_notes[g_selected].name);
    if (is_txt_name(g_notes[g_selected].name)) len -= 4;

    g_editor_buffer_bytes = MAX_FILENAME * sizeof(VMWCHAR);
    g_editor_buffer = (VMWCHAR*)vm_malloc(g_editor_buffer_bytes);
    if (!g_editor_buffer) {
        g_editor_buffer_bytes = 0;
        g_current_path[0] = 0;
        set_status("Not enough memory");
        render_screen();
        return;
    }

    memset(g_editor_buffer, 0, g_editor_buffer_bytes);
    vm_wstrncpy(g_editor_buffer, g_notes[g_selected].name, len);
    g_editor_buffer[len] = 0;

    g_renaming = VM_TRUE;
    g_ui = UI_BROWSER;

    g_editor = vm_editor_create(VM_EDITOR_MULTILINE, 0, g_screen_h - (g_softkey_h * 2),
                         g_screen_w, g_softkey_h, (VMUWSTR)g_editor_buffer, g_editor_buffer_bytes, VM_FALSE, g_layer);

    if (g_editor == 0) {
        vm_free(g_editor_buffer);
        g_editor_buffer = NULL;
        g_editor_buffer_bytes = 0;
        g_renaming = VM_FALSE;
        g_current_path[0] = 0;
        set_status("Could not create editor");
        render_screen();
        return;
    }

    runing_rename = VM_TRUE;

    vm_editor_set_multiline_text_font(g_editor, g_editor_font1);

    //    vm_editor_set_bg_border_style(g_editor, VM_EDITOR_NO_BORDER, 0xFFFF, 0xFFFF);
    vm_editor_set_bg_border_style(g_editor, VM_EDITOR_SINGLE_BORDER, 0x0000, 0x0000);
    //    vm_editor_set_margin(g_editor, 0, 0, 0, 0);

    vm_editor_set_IME(g_editor, VM_INPUT_TYPE_SENTENCE, g_editor_input_modes,
                      VM_INPUT_MODE_MULTITAP_FIRST_UPPERCASE_ABC, editor_ime_cb);

    vm_editor_set_softkey(g_editor, (VMUWSTR)g_editor_right_label, VM_LEFT_SOFTKEY, right_softkey_cb);
    vm_editor_set_softkey(g_editor, (VMUWSTR)g_editor_center_label, VM_CENTER_SOFTKEY, center_softkey_cb);
    vm_editor_set_softkey(g_editor, (VMUWSTR)g_editor_right_label1, VM_RIGHT_SOFTKEY, right_softkey_cb);

    render_screen();

    vm_editor_activate(g_editor, VM_FALSE);
    vm_editor_show(g_editor);
    vm_editor_redraw_ime_screen();
}

static void delete_selected_note(void)
{
    if (g_note_count > 0) {
        build_note_path(g_current_path, g_notes[g_selected].name);
        if (vm_file_delete(g_current_path) == 0)
            set_status("Deleted");
        else
            set_status("Delete failed");
        g_current_path[0] = 0;
        refresh_notes();
    }
    g_ui = UI_BROWSER;
    render_screen();
}

static void activate_menu_item(void)
{
    VMINT choice = g_menu_selected;
    g_ui = UI_BROWSER;
    switch (choice) {
        case 0:
            start_new_note();
            return;
        case 1:
            begin_rename();
            return;
        case 2:
            if (g_note_count > 0)
                g_ui = UI_DELETE_CONFIRM;
            else
                set_status("No note selected");
            break;
    }
    render_screen();
}

static void handle_key(VMINT event, VMINT keycode)
{
    if (g_editor != 0) return;

    if (event != VM_KEY_EVENT_DOWN && event != VM_KEY_EVENT_REPEAT) return;

    if (g_ui == UI_DELETE_CONFIRM) {
        if (keycode == VM_KEY_OK || keycode == VM_KEY_LEFT_SOFTKEY)
            delete_selected_note();
        else if (keycode == VM_KEY_RIGHT_SOFTKEY || keycode == VM_KEY_CLEAR || keycode == VM_KEY_BACK) {
            g_ui = UI_BROWSER;
            render_screen();
        }
        return;
    }

    if (g_ui == UI_MENU) {
        if (keycode == VM_KEY_UP) {
            g_menu_selected = (g_menu_selected + 2) % 3;
        } else if (keycode == VM_KEY_DOWN) {
            g_menu_selected = (g_menu_selected + 1) % 3;
        } else if (keycode == VM_KEY_OK || keycode == VM_KEY_LEFT_SOFTKEY) {
            activate_menu_item();

            if (g_editor != 0) return;
        } else if (keycode == VM_KEY_RIGHT_SOFTKEY || keycode == VM_KEY_CLEAR || keycode == VM_KEY_BACK) {
            g_ui = UI_BROWSER;
        }

        render_screen();
        return;
    }

    if (keycode == VM_KEY_LEFT_SOFTKEY) {
        g_menu_selected = 0;
        g_ui = UI_MENU;
        render_screen();
        return;
    }

    if (keycode == VM_KEY_RIGHT_SOFTKEY || keycode == VM_KEY_BACK || keycode == VM_KEY_CLEAR) {
        vm_exit_app();
        return;
    }

    if (keycode == VM_KEY_UP) {
        if (g_note_count > 0 && g_selected > 0) g_selected--;
    } else if (keycode == VM_KEY_DOWN) {
        if (g_note_count > 0 && g_selected < g_note_count - 1) g_selected++;
    } else if (keycode == VM_KEY_OK) {
        open_selected_note();
        return;
    }

    update_scroll();
    render_screen();
}

static void handle_sysevt(VMINT message, VMINT param)
{
    switch (message) {
        case VM_MSG_CREATE:
            g_font_h = vm_graphic_get_character_height();
            if (g_font_h <= 0) g_font_h = 14;

            g_softkey_h = vm_editor_get_softkey_height();
            g_header_h = g_softkey_h;
            g_footer_h = g_softkey_h;

            vm_file_mkdir(g_folder);
            g_layer = vm_graphic_create_layer(0, 0, g_screen_w, g_screen_h, -1);
            vm_graphic_active_layer(g_layer);
            refresh_notes();
            if (g_note_count == 0) {
                start_new_note();
            } else {
                render_screen();
            }
            break;

        case VM_MSG_ACTIVE:
            if (g_editor != 0 && g_editor_suspended) {
                g_editor_suspended = VM_FALSE;
                vm_editor_activate(g_editor, VM_FALSE);
                vm_editor_show(g_editor);
                vm_editor_redraw_ime_screen();
            } else if (g_layer >= 0) {
                vm_graphic_active_layer(g_layer);
                if (g_editor != 0) {
                    vm_editor_show(g_editor);
                    vm_editor_redraw_ime_screen();
                } else {
                    render_screen();
                }
            }
            break;

        case VM_MSG_PAINT:
            if (g_editor != 0) {
                if (runing_rename == VM_TRUE) {
                    render_screen();
                }
                vm_editor_show(g_editor);
                vm_editor_redraw_ime_screen();
            } else {
                render_screen();
            }
            break;

        case VM_MSG_INACTIVE:
            if (g_editor != 0) {
                vm_editor_save(g_editor);
                vm_editor_deactivate(g_editor);
                g_editor_suspended = VM_TRUE;
            }
            break;

        case VM_MSG_QUIT:
            if (g_editor != 0) {
                vm_editor_deactivate(g_editor);
                vm_editor_close(g_editor);
                g_editor = 0;
            }
            if (g_editor_buffer) {
                vm_free(g_editor_buffer);
                g_editor_buffer = NULL;
            }
            if (g_layer >= 0) vm_graphic_delete_layer(g_layer);
            g_layer = -1;
            break;
    }
}

void vm_main(void)
{
    vm_reg_sysevt_callback(handle_sysevt);
    vm_reg_keyboard_callback(handle_key);
    g_screen_w = vm_graphic_get_screen_width();
    g_screen_h = vm_graphic_get_screen_height();
    create_default_path();
    ascii_to_ucs2_const(g_editor_left_label, sizeof(g_editor_left_label), "Save");
    ascii_to_ucs2_const(g_editor_center_label, sizeof(g_editor_center_label), "OK");
    ascii_to_ucs2_const(g_editor_right_label, sizeof(g_editor_right_label), "Back");
    ascii_to_ucs2_const(g_editor_right_label1, sizeof(g_editor_right_label1), "Clear");
    ascii_to_ucs2_const(w_choose, sizeof(w_choose), c_choose);
}

void create_auto_filename(VMWSTR text)
{
    VMINT drv;
    VMCHAR path[128];
    struct vm_time_t curr_time;

    vm_get_time(&curr_time);

    drv = vm_get_removable_driver();
    if (drv < 0) drv = vm_get_system_driver();

    sprintf(path, "%c:\\Notes\\%02d%02d%02d%02d%02d.txt", drv, curr_time.mon, curr_time.day, curr_time.hour, curr_time.min, curr_time.sec);
    vm_ascii_to_ucs2(text, (strlen(path) + 1) * 2, path);
}

void create_default_path(void)
{
    VMINT drv;
    VMCHAR path[32];

    drv = vm_get_removable_driver();
    if (drv < 0) {
        drv = vm_get_system_driver();
    }

    sprintf(path, "%c:\\%s", drv, NOTES_DIR);
    vm_ascii_to_ucs2(g_folder, sizeof(g_folder), path);
}

static void save_editor_action(void)
{
    if (g_editor == 0 || !g_editor_buffer) return;

    if (g_renaming) {
        VMWCHAR name[MAX_FILENAME];
        VMWCHAR new_path[260];
        VMFILE existing;

        vm_editor_get_text(g_editor, (VMUWSTR)g_editor_buffer, g_editor_buffer_bytes);
        g_editor_buffer[MAX_FILENAME - 1] = 0;

        if (!valid_note_name(g_editor_buffer)) {
            set_status("Invalid note name");
            return;
        }

        append_txt_extension(name, g_editor_buffer);
        build_note_path(new_path, name);

        if (vm_wstrcmp(new_path, g_current_path) != 0) {
            existing = vm_file_open(new_path, MODE_READ, VM_FALSE);

            if (existing >= 0) {
                vm_file_close(existing);
                set_status("That note name already exists");
                return;
            }

            if (vm_file_rename(g_current_path, new_path) != 0) {
                set_status("Rename failed");
                return;
            }
        }

        vm_editor_deactivate(g_editor);
        vm_editor_close(g_editor);
        g_editor = 0;
        g_editor_suspended = VM_FALSE;
        g_renaming = VM_FALSE;
        runing_rename = VM_FALSE;

        vm_free(g_editor_buffer);
        g_editor_buffer = NULL;
        g_editor_buffer_bytes = 0;
        g_editor_text_len = 0;

        g_current_path[0] = 0;
        set_status("Renamed");
        refresh_notes();
        render_screen();
        return;
    }

    runing_rename = VM_FALSE;
    close_editor(VM_TRUE);
}

static void left_softkey_cb(void) { save_editor_action(); }

static void center_softkey_cb(void) { save_editor_action(); }

static void right_softkey_cb(void)
{
    if (g_renaming) {
        g_renaming = VM_FALSE;
        runing_rename = VM_FALSE;
    }

    close_editor(VM_FALSE);
}
