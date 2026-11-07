/*
 * install/picker.c -- the native "open file" dialog per platform.  See
 * picker.h for why each one is the one used.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L   /* popen, pclose, getpid under -std=c99 */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "picker.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if (!defined(__APPLE__) || TARGET_OS_IPHONE) && !defined(__wasm__)
static void put(char *out, size_t n, const char *s)
{
    if (n > 0) {
        size_t len = strlen(s);
        if (len >= n) {
            len = n - 1;
        }
        memcpy(out, s, len);
        out[len] = '\0';
    }
}
#endif

#if defined(_WIN32)

/* ------------------------------------------------------------------ */
/* Windows                                                             */
/* ------------------------------------------------------------------ */

#ifndef COBJMACROS
#define COBJMACROS
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>

/* CLSID_FileOpenDialog {DC1C5A9C-E88A-4DDE-A5A1-60F82A20AEF7} and
 * IID_IFileOpenDialog {D57C7288-D4AD-4768-BE02-9D969532D960}, spelled here
 * so the build needs neither uuid.lib nor INITGUID. */
static const CLSID file_open_dialog_clsid = {
    0xDC1C5A9Cu, 0xE88A, 0x4DDE, { 0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7 }
};
static const IID file_open_dialog_iid = {
    0xD57C7288u, 0xD4AD, 0x4768, { 0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xD9, 0x60 }
};

static int narrow(const wchar_t *w, char *out, size_t n)
{
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL) > 0
           ? 0 : -1;
}

/* IFileOpenDialog (Vista and later).  BW_PICK_UNAVAILABLE if COM or the
 * dialog class cannot be had, so the caller can fall back. */
static int pick_ifiledialog(HWND parent, const wchar_t *title, char *out, size_t n)
{
    static const COMDLG_FILTERSPEC types[2] = {
        { L"Nintendo DS ROM (*.nds)", L"*.nds" },
        { L"All files (*.*)",         L"*.*" }
    };
    IFileOpenDialog *dlg = NULL;
    IShellItem *item = NULL;
    PWSTR wpath = NULL;
    FILEOPENDIALOGOPTIONS opts = 0;
    HRESULT init, hr;
    int rc = BW_PICK_UNAVAILABLE;

    init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
        return BW_PICK_UNAVAILABLE;
    }
    hr = CoCreateInstance(&file_open_dialog_clsid, NULL, CLSCTX_INPROC_SERVER,
                          &file_open_dialog_iid, (void **)&dlg);
    if (SUCCEEDED(hr)) {
        (void)IFileOpenDialog_SetFileTypes(dlg, 2, types);
        (void)IFileOpenDialog_SetFileTypeIndex(dlg, 1);
        (void)IFileOpenDialog_SetTitle(dlg, title);
        if (SUCCEEDED(IFileOpenDialog_GetOptions(dlg, &opts))) {
            (void)IFileOpenDialog_SetOptions(dlg, opts | FOS_FORCEFILESYSTEM
                                             | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
        }
        hr = IFileOpenDialog_Show(dlg, parent);
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            rc = BW_PICK_CANCELLED;
        } else if (SUCCEEDED(hr)
                   && SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item))
                   && SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wpath))
                   && narrow(wpath, out, n) == 0) {
            rc = BW_PICK_OK;
        }
        if (wpath != NULL) {
            CoTaskMemFree(wpath);
        }
        if (item != NULL) {
            IShellItem_Release(item);
        }
        IFileOpenDialog_Release(dlg);
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }
    return rc;
}

/* The older common dialog, for a system where the COM one is missing. */
static int pick_getopenfilename(HWND parent, const wchar_t *title, char *out, size_t n)
{
    OPENFILENAMEW ofn;
    wchar_t path[4096];

    path[0] = 0;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = L"Nintendo DS ROM (*.nds)\0*.nds\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = path;
    ofn.nMaxFile = 4096;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        return CommDlgExtendedError() == 0 ? BW_PICK_CANCELLED : BW_PICK_UNAVAILABLE;
    }
    return narrow(path, out, n) == 0 ? BW_PICK_OK : BW_PICK_UNAVAILABLE;
}

int bw_pick_rom(void *parent, const char *title, char *out, size_t n)
{
    wchar_t wtitle[256];
    int rc;

    if (MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 256) == 0) {
        wtitle[0] = 0;
    }
    rc = pick_ifiledialog((HWND)parent, wtitle, out, n);
    if (rc == BW_PICK_UNAVAILABLE) {
        rc = pick_getopenfilename((HWND)parent, wtitle, out, n);
    }
    if (rc == BW_PICK_UNAVAILABLE) {
        put(out, n, "The Windows file dialog could not be opened.");
    }
    return rc;
}

#elif defined(__APPLE__) && !TARGET_OS_IPHONE

/* ------------------------------------------------------------------ */
/* macOS: NSOpenPanel, in install/picker_mac.m                          */
/* ------------------------------------------------------------------ */

int bw_pick_rom_mac(void *parent, const char *title, char *out, size_t n);

int bw_pick_rom(void *parent, const char *title, char *out, size_t n)
{
    return bw_pick_rom_mac(parent, title, out, n);
}

#elif defined(__wasm__)

/* ------------------------------------------------------------------ */
/* The wasm32 build: the host program's own dialog (sdk/wasm_host.h)    */
/* ------------------------------------------------------------------ */

#include "wasm_host.h"

int bw_pick_rom(void *parent, const char *title, char *out, size_t n)
{
    (void)parent;
    return bw_host_pick_rom(title, out, (unsigned)n);
}

#elif defined(__vita__)

/* ------------------------------------------------------------------ */
/* The Vita: no file list an app can open, so the ROM is a file the     */
/* player copies next to the program's data (docs/consoles.md)          */
/* ------------------------------------------------------------------ */

#include <dirent.h>

int bw_pick_rom(void *parent, const char *title, char *out, size_t n)
{
    const char *dir = "ux0:data/eidos/";
    DIR *d = opendir(dir);
    struct dirent *e;

    (void)parent;
    (void)title;
    if (d == NULL) {
        snprintf(out, n, "Copy the game's ROM to %s on the memory card.", dir);
        return BW_PICK_UNAVAILABLE;
    }
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);

        if (len > 4 && (strcmp(e->d_name + len - 4, ".nds") == 0 ||
                        strcmp(e->d_name + len - 4, ".NDS") == 0)) {
            snprintf(out, n, "%s%s", dir, e->d_name);
            closedir(d);
            return BW_PICK_OK;
        }
    }
    closedir(d);
    snprintf(out, n, "No .nds file in %s on the memory card.", dir);
    return BW_PICK_UNAVAILABLE;
}

#elif defined(__ANDROID__) || defined(__APPLE__)

/* ------------------------------------------------------------------ */
/* Android and iOS: deferred (docs/installer.md, "Picking the ROM")     */
/* ------------------------------------------------------------------ */

int bw_pick_rom(void *parent, const char *title, char *out, size_t n)
{
    (void)parent;
    (void)title;
    put(out, n, "Choosing a file is not built for this system yet.");
    return BW_PICK_UNAVAILABLE;
}

#else

/* ------------------------------------------------------------------ */
/* Linux and the other free desktops                                   */
/* ------------------------------------------------------------------ */

#include <dlfcn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * The XDG desktop portal, org.freedesktop.portal.FileChooser.OpenFile,
 * through libdbus-1 loaded with dlopen: the portal is what works inside
 * Flatpak and Snap (where no zenity exists and the sandbox hides the
 * player's files until the portal grants one), and it shows the desktop's
 * own dialog (GTK on GNOME, Qt on KDE).  libdbus is loaded at run time so
 * neither the build nor a machine without D-Bus needs it.
 *
 * Only the public, stack-allocatable parts of libdbus's ABI are used.
 * DBusMessageIter and DBusError are declared as buffers at least as large
 * as libdbus's own structs (it fills them; nothing here reads inside).
 */
typedef union { void *p[16]; long l[16]; } DIter;     /* >= DBusMessageIter */
typedef union { void *p[8];  long l[8];  } DError;    /* >= DBusError */
typedef struct DConn DConn;
typedef struct DMsg DMsg;

#define DT_INVALID  0
#define DT_BOOLEAN  'b'
#define DT_UINT32   'u'
#define DT_STRING   's'
#define DT_ARRAY    'a'
#define DT_VARIANT  'v'
#define DT_STRUCT   'r'
#define DT_DICT     'e'

static struct {
    void *lib;
    void  (*error_init)(DError *);
    void  (*error_free)(DError *);
    unsigned (*error_is_set)(const DError *);
    DConn *(*bus_get_private)(int, DError *);
    const char *(*bus_get_unique_name)(DConn *);
    void  (*bus_add_match)(DConn *, const char *, DError *);
    DMsg *(*new_method_call)(const char *, const char *, const char *, const char *);
    void  (*iter_init_append)(DMsg *, DIter *);
    unsigned (*iter_append_basic)(DIter *, int, const void *);
    unsigned (*iter_open_container)(DIter *, int, const char *, DIter *);
    unsigned (*iter_close_container)(DIter *, DIter *);
    DMsg *(*send_with_reply_and_block)(DConn *, DMsg *, int, DError *);
    void  (*message_unref)(DMsg *);
    unsigned (*read_write)(DConn *, int);
    DMsg *(*pop_message)(DConn *);
    unsigned (*is_signal)(DMsg *, const char *, const char *);
    const char *(*get_path)(DMsg *);
    unsigned (*iter_init)(DMsg *, DIter *);
    int   (*iter_get_arg_type)(DIter *);
    void  (*iter_get_basic)(DIter *, void *);
    unsigned (*iter_next)(DIter *);
    void  (*iter_recurse)(DIter *, DIter *);
    void  (*connection_close)(DConn *);
    void  (*connection_unref)(DConn *);
} D;

static int dbus_load(void)
{
    struct { void **slot; const char *name; } syms[] = {
        { (void **)&D.error_init, "dbus_error_init" },
        { (void **)&D.error_free, "dbus_error_free" },
        { (void **)&D.error_is_set, "dbus_error_is_set" },
        { (void **)&D.bus_get_private, "dbus_bus_get_private" },
        { (void **)&D.bus_get_unique_name, "dbus_bus_get_unique_name" },
        { (void **)&D.bus_add_match, "dbus_bus_add_match" },
        { (void **)&D.new_method_call, "dbus_message_new_method_call" },
        { (void **)&D.iter_init_append, "dbus_message_iter_init_append" },
        { (void **)&D.iter_append_basic, "dbus_message_iter_append_basic" },
        { (void **)&D.iter_open_container, "dbus_message_iter_open_container" },
        { (void **)&D.iter_close_container, "dbus_message_iter_close_container" },
        { (void **)&D.send_with_reply_and_block, "dbus_connection_send_with_reply_and_block" },
        { (void **)&D.message_unref, "dbus_message_unref" },
        { (void **)&D.read_write, "dbus_connection_read_write" },
        { (void **)&D.pop_message, "dbus_connection_pop_message" },
        { (void **)&D.is_signal, "dbus_message_is_signal" },
        { (void **)&D.get_path, "dbus_message_get_path" },
        { (void **)&D.iter_init, "dbus_message_iter_init" },
        { (void **)&D.iter_get_arg_type, "dbus_message_iter_get_arg_type" },
        { (void **)&D.iter_get_basic, "dbus_message_iter_get_basic" },
        { (void **)&D.iter_next, "dbus_message_iter_next" },
        { (void **)&D.iter_recurse, "dbus_message_iter_recurse" },
        { (void **)&D.connection_close, "dbus_connection_close" },
        { (void **)&D.connection_unref, "dbus_connection_unref" }
    };
    size_t i;

    if (D.lib != NULL) {
        return 0;
    }
    D.lib = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    if (D.lib == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(syms) / sizeof(syms[0]); i++) {
        /* The object-to-function pointer conversion POSIX guarantees for
         * dlsym, done through memcpy so ISO C's rule does not warn. */
        void *f = dlsym(D.lib, syms[i].name);
        if (f == NULL) {
            dlclose(D.lib);
            D.lib = NULL;
            return -1;
        }
        memcpy(syms[i].slot, &f, sizeof(f));
    }
    return 0;
}

static void append_str(DIter *it, const char *s)
{
    D.iter_append_basic(it, DT_STRING, &s);
}

/* One a{sv} entry whose value is a string or a boolean. */
static void dict_entry(DIter *dict, const char *key, int type, const void *val)
{
    DIter e, v;
    char sig[2];

    sig[0] = (char)type;
    sig[1] = '\0';
    D.iter_open_container(dict, DT_DICT, NULL, &e);
    append_str(&e, key);
    D.iter_open_container(&e, DT_VARIANT, sig, &v);
    D.iter_append_basic(&v, type, val);
    D.iter_close_container(&e, &v);
    D.iter_close_container(dict, &e);
}

/* One (sa(us)) filter: a name and one glob pattern (kind 0). */
static void filter(DIter *arr, const char *name, const char *glob)
{
    DIter st, pats, pat;
    unsigned kind = 0;

    D.iter_open_container(arr, DT_STRUCT, NULL, &st);
    append_str(&st, name);
    D.iter_open_container(&st, DT_ARRAY, "(us)", &pats);
    D.iter_open_container(&pats, DT_STRUCT, NULL, &pat);
    D.iter_append_basic(&pat, DT_UINT32, &kind);
    append_str(&pat, glob);
    D.iter_close_container(&pats, &pat);
    D.iter_close_container(&st, &pats);
    D.iter_close_container(arr, &st);
}

/* "file:///home/a%20b/x.nds" -> "/home/a b/x.nds". */
static int uri_to_path(const char *uri, char *out, size_t n)
{
    size_t o = 0;

    if (strncmp(uri, "file://", 7) != 0) {
        return -1;
    }
    uri += 7;
    while (*uri != '\0' && o + 1 < n) {
        if (uri[0] == '%' && uri[1] != '\0' && uri[2] != '\0') {
            char hex[3];
            hex[0] = uri[1];
            hex[1] = uri[2];
            hex[2] = '\0';
            out[o++] = (char)strtol(hex, NULL, 16);
            uri += 3;
        } else {
            out[o++] = *uri++;
        }
    }
    out[o] = '\0';
    return *uri == '\0' ? 0 : -1;
}

/* The Response signal's (u a{sv}): 0 and the first of "uris" is success. */
static int parse_response(DMsg *m, char *out, size_t n)
{
    DIter it, dict, entry, var, uris;
    unsigned code = 2;

    if (!D.iter_init(m, &it) || D.iter_get_arg_type(&it) != DT_UINT32) {
        return BW_PICK_UNAVAILABLE;
    }
    D.iter_get_basic(&it, &code);
    if (code != 0) {
        return BW_PICK_CANCELLED;       /* 1 cancelled, 2 ended otherwise */
    }
    if (!D.iter_next(&it) || D.iter_get_arg_type(&it) != DT_ARRAY) {
        return BW_PICK_UNAVAILABLE;
    }
    D.iter_recurse(&it, &dict);
    while (D.iter_get_arg_type(&dict) == DT_DICT) {
        const char *key = NULL;
        D.iter_recurse(&dict, &entry);
        D.iter_get_basic(&entry, (void *)&key);
        if (key != NULL && strcmp(key, "uris") == 0 && D.iter_next(&entry)
            && D.iter_get_arg_type(&entry) == DT_VARIANT) {
            D.iter_recurse(&entry, &var);
            if (D.iter_get_arg_type(&var) == DT_ARRAY) {
                D.iter_recurse(&var, &uris);
                if (D.iter_get_arg_type(&uris) == DT_STRING) {
                    const char *uri = NULL;
                    D.iter_get_basic(&uris, (void *)&uri);
                    if (uri != NULL && uri_to_path(uri, out, n) == 0) {
                        return BW_PICK_OK;
                    }
                }
            }
        }
        if (!D.iter_next(&dict)) {
            break;
        }
    }
    return BW_PICK_UNAVAILABLE;
}

static int pick_portal(const char *title, char *out, size_t n)
{
    DError err;
    DConn *conn;
    DMsg *call, *reply;
    DIter args, dict, var, arr;
    char token[32];
    char sender[128];
    char path[256];
    char rule[512];
    const char *handle = NULL;
    const char *parent = "";
    const char *tok = token;
    unsigned modal = 1;
    const char *u;
    size_t i;
    int rc = BW_PICK_UNAVAILABLE;

    if (dbus_load() != 0) {
        return BW_PICK_UNAVAILABLE;
    }
    D.error_init(&err);
    conn = D.bus_get_private(0 /* DBUS_BUS_SESSION */, &err);
    if (conn == NULL) {
        D.error_free(&err);
        return BW_PICK_UNAVAILABLE;
    }

    /* The request object's path is predictable from our bus name and the
     * token, so the match for its Response is in place before the call
     * (the portal documents this to close the race). */
    snprintf(token, sizeof(token), "eidos%ld", (long)getpid());
    u = D.bus_get_unique_name(conn);
    for (i = 0; u != NULL && u[0] != '\0' && i + 1 < sizeof(sender); u++) {
        if (*u != ':') {
            sender[i++] = *u == '.' ? '_' : *u;
        }
    }
    sender[i] = '\0';
    snprintf(path, sizeof(path), "/org/freedesktop/portal/desktop/request/%s/%s",
             sender, token);
    snprintf(rule, sizeof(rule), "type='signal',interface='org.freedesktop.portal.Request',"
             "member='Response',path='%s'", path);
    D.bus_add_match(conn, rule, &err);
    if (D.error_is_set(&err)) {
        D.error_free(&err);
        goto out;
    }

    call = D.new_method_call("org.freedesktop.portal.Desktop",
                             "/org/freedesktop/portal/desktop",
                             "org.freedesktop.portal.FileChooser", "OpenFile");
    if (call == NULL) {
        goto out;
    }
    D.iter_init_append(call, &args);
    append_str(&args, parent);
    append_str(&args, title);
    D.iter_open_container(&args, DT_ARRAY, "{sv}", &dict);
    dict_entry(&dict, "handle_token", DT_STRING, &tok);
    dict_entry(&dict, "modal", DT_BOOLEAN, &modal);
    {
        DIter e;
        D.iter_open_container(&dict, DT_DICT, NULL, &e);
        append_str(&e, "filters");
        D.iter_open_container(&e, DT_VARIANT, "a(sa(us))", &var);
        D.iter_open_container(&var, DT_ARRAY, "(sa(us))", &arr);
        filter(&arr, "Nintendo DS ROM", "*.nds");
        filter(&arr, "All files", "*");
        D.iter_close_container(&var, &arr);
        D.iter_close_container(&e, &var);
        D.iter_close_container(&dict, &e);
    }
    D.iter_close_container(&args, &dict);
    reply = D.send_with_reply_and_block(conn, call, 5000, &err);
    D.message_unref(call);
    if (reply == NULL) {
        D.error_free(&err);             /* no portal, or no FileChooser */
        goto out;
    }
    if (D.iter_init(reply, &args) && D.iter_get_arg_type(&args) == 'o') {
        D.iter_get_basic(&args, (void *)&handle);
        if (handle != NULL && strcmp(handle, path) != 0) {
            /* An old portal that ignores handle_token: follow its path. */
            snprintf(path, sizeof(path), "%s", handle);
            snprintf(rule, sizeof(rule), "type='signal',interface="
                     "'org.freedesktop.portal.Request',member='Response',"
                     "path='%s'", path);
            D.bus_add_match(conn, rule, &err);
            if (D.error_is_set(&err)) {
                D.error_free(&err);
            }
        }
    }
    D.message_unref(reply);

    /* Wait for the player.  read_write returns 0 once the bus is gone. */
    while (D.read_write(conn, 250)) {
        DMsg *m;
        int done = 0;
        while ((m = D.pop_message(conn)) != NULL) {
            const char *mp = D.get_path(m);
            if (D.is_signal(m, "org.freedesktop.portal.Request", "Response")
                && mp != NULL && strcmp(mp, path) == 0) {
                rc = parse_response(m, out, n);
                done = 1;
            }
            D.message_unref(m);
        }
        if (done) {
            break;
        }
    }
out:
    D.connection_close(conn);
    D.connection_unref(conn);
    return rc;
}

/* A dialog program's first line of output; its exit status says whether
 * the player chose (0), cancelled (1), or the program is missing (127). */
static int pick_command(const char *cmd, char *out, size_t n)
{
    char line[4096];
    FILE *p = popen(cmd, "r");
    int status;
    int got;

    if (p == NULL) {
        return BW_PICK_UNAVAILABLE;
    }
    got = fgets(line, sizeof(line), p) != NULL;
    status = pclose(p);
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) >= 126) {
        return BW_PICK_UNAVAILABLE;
    }
    if (WEXITSTATUS(status) != 0 || !got) {
        return BW_PICK_CANCELLED;
    }
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0') {
        return BW_PICK_CANCELLED;
    }
    put(out, n, line);
    return BW_PICK_OK;
}

int bw_pick_rom(void *parent, const char *title, char *out, size_t n)
{
    int rc;

    (void)parent;
    rc = pick_portal(title, out, n);
    if (rc != BW_PICK_UNAVAILABLE) {
        return rc;
    }
    /* The title is a fixed string of the wizard's; it has no quote in it. */
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "zenity --file-selection --title='%s' "
                 "--file-filter='Nintendo DS ROM | *.nds' "
                 "--file-filter='All files | *' 2>/dev/null", title);
        rc = pick_command(cmd, out, n);
        if (rc != BW_PICK_UNAVAILABLE) {
            return rc;
        }
        snprintf(cmd, sizeof(cmd), "kdialog --title '%s' --getopenfilename . "
                 "'*.nds|Nintendo DS ROM' 2>/dev/null", title);
        rc = pick_command(cmd, out, n);
        if (rc != BW_PICK_UNAVAILABLE) {
            return rc;
        }
    }
    put(out, n, "No file dialog is available here (no desktop portal, zenity "
                "or kdialog).");
    return BW_PICK_UNAVAILABLE;
}

#endif
