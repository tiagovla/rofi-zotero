#include <gmodule.h>
#include <pwd.h>
#include <rofi/helper.h>
#include <rofi/mode-private.h>
#include <rofi/mode.h>
#include <sqlite3.h>
#include <stdio.h>
#include <unistd.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "Plugin_Zotero"

G_MODULE_EXPORT Mode mode;

static char *STATEMENT = " \
    SELECT \n\
    parentItemDataValues.value as name, \n\
    'storage/' || items.key || '/' || REPLACE(itemAttachments.path, 'storage:', '') as path, \n\
    creators.lastName || ', ' || creators.firstName as author \n\
    FROM itemAttachments  \n\
    INNER JOIN items \n\
        ON items.itemID = itemAttachments.itemID  \n\
    INNER JOIN itemData  \n\
        ON itemData.itemID = items.itemID  \n\
    INNER JOIN itemDataValues \n\
        ON itemData.valueID = itemDataValues.valueID  \n\
    INNER JOIN items AS parentInfo  \n\
        ON itemAttachments.parentItemID = parentInfo.itemID  \n\
    INNER JOIN itemData as parentItemData  \n\
        ON parentItemData.itemID = parentInfo.itemID  \n\
    INNER JOIN itemDataValues as parentItemDataValues  \n\
        ON parentItemDataValues.valueID = parentItemData.valueID \n\
    INNER JOIN itemCreators  \n\
        ON itemCreators.itemID = parentInfo.itemID \n\
    INNER JOIN creators  \n\
        ON creators.creatorID = itemCreators.creatorID \n\
    WHERE itemData.fieldID = 1 \n\
      AND itemCreators.orderIndex = 0 \n\
      AND parentItemData.fieldID = 1 \n\
      AND (itemAttachments.contentType LIKE '%pdf' \n\
          OR itemAttachments.contentType LIKE '%djvu') \n\
    ";

typedef struct {
    gchar *name;
    gchar *path;
    gchar *author;
} Entry;

typedef struct {
    sqlite3 *db;
    gchar *zotero_path;
    GPtrArray *entries;
} ZoteroModePrivateData;

void destroy_element(gpointer data) {
    if (data != NULL) {
        Entry *entry = (Entry *)data;
        g_free(entry->name);
        g_free(entry->path);
        g_free(entry->author);
        g_free(entry);
    }
}

static int callback(void *data, int argc, char **argv, char **azColName) {
    Entry *e = g_malloc(sizeof(Entry));
    e->name = g_strdup(argv[0]);
    e->path = g_strdup(argv[1]);
    e->author = g_strdup(argv[2]);
    g_ptr_array_add(((ZoteroModePrivateData *)data)->entries, e);
    return 0;
}

static void get_zotero(Mode *sw) {
    ZoteroModePrivateData *pd = (ZoteroModePrivateData *)mode_get_private_data(sw);
    pd->entries = g_ptr_array_new_with_free_func(destroy_element);
    pd->zotero_path = g_strconcat(g_get_home_dir(), "/Zotero/", NULL);
    gchar *db_name = g_strconcat(pd->zotero_path, "zotero.sqlite", NULL);
    if (access(db_name, F_OK) == 0) {
        int rc = sqlite3_open(db_name, &pd->db);
        if (rc) {
            g_debug("Can't open database: %s\n", sqlite3_errmsg(pd->db));
            sqlite3_close(pd->db);
        }
    } else {
        g_debug("Database does not exist.");
    }
    g_free(db_name);
    char *zErrMsg = 0;
    int rc = sqlite3_exec(pd->db, STATEMENT, callback, (void *)pd, &zErrMsg);
    if (rc != SQLITE_OK) {
        g_debug("SQL error: %s", zErrMsg);
        sqlite3_free(zErrMsg);
    }
}

static int zotero_mode_init(Mode *sw) {
    if (mode_get_private_data(sw) == NULL) {
        ZoteroModePrivateData *pd = g_malloc0(sizeof(*pd));
        mode_set_private_data(sw, (void *)pd);
        get_zotero(sw);
    }
    return TRUE;
}

static unsigned int zotero_mode_get_num_entries(const Mode *sw) {
    const ZoteroModePrivateData *pd = (const ZoteroModePrivateData *)mode_get_private_data(sw);
    return pd->entries->len;
}

static ModeMode zotero_mode_result(Mode *sw, int menu_entry, char **input, unsigned int selected_line) {
    ModeMode retv = MODE_EXIT;
    ZoteroModePrivateData *pd = (ZoteroModePrivateData *)mode_get_private_data(sw);
    if (menu_entry & MENU_NEXT) {
        retv = NEXT_DIALOG;
    } else if (menu_entry & MENU_PREVIOUS) {
        retv = PREVIOUS_DIALOG;
    } else if (menu_entry & MENU_QUICK_SWITCH) {
        retv = (menu_entry & MENU_LOWER_MASK);
    } else if ((menu_entry & MENU_OK)) {
        Entry *res = g_ptr_array_index(pd->entries, selected_line);
        char *default_cmd = "xdg-open";
        gchar *cmd = g_strconcat(default_cmd, " \"", pd->zotero_path, res->path, "\"", NULL);
        helper_execute_command(NULL, cmd, FALSE, NULL);
        g_free(cmd);
    }
    return retv;
}

static void zotero_mode_destroy(Mode *sw) {
    ZoteroModePrivateData *pd = (ZoteroModePrivateData *)mode_get_private_data(sw);
    if (pd != NULL) {
        g_ptr_array_free(pd->entries, TRUE);
        sqlite3_close(pd->db);
        g_free(pd->zotero_path);
        g_free(pd);
        mode_set_private_data(sw, NULL);
    }
}

static char *zotero_get_display_value(const Mode *sw, unsigned int selected_line, G_GNUC_UNUSED int *state,
                                      G_GNUC_UNUSED GList **attr_list, int get_entry) {
    ZoteroModePrivateData *pd = (ZoteroModePrivateData *)mode_get_private_data(sw);
    Entry *res = g_ptr_array_index(pd->entries, selected_line);
    gsize size = strlen(res->name) + strlen(res->author) + 3 + 1;
    gchar *buffer = g_newa(gchar, size);
    g_snprintf(buffer, size, "%s - %s", res->name, res->author);
    return get_entry ? g_strdup(buffer) : NULL;
}

static int zotero_token_match(const Mode *sw, rofi_int_matcher **tokens, unsigned int index) {
    ZoteroModePrivateData *pd = (ZoteroModePrivateData *)mode_get_private_data(sw);
    Entry *res = g_ptr_array_index(pd->entries, index);
    gsize size = strlen(res->name) + strlen(res->author) + 3 + 1;
    gchar *buffer = g_newa(gchar, size);
    g_snprintf(buffer, size, "%s - %s", res->name, res->author);
    return helper_token_match(tokens, buffer);
}

static char *zotero_get_message(const Mode *sw) { return g_markup_printf_escaped("Results:"); }

static char *zotero_preprocess_input(Mode *sw, const char *input) {
    ZoteroModePrivateData *pd = (ZoteroModePrivateData *)mode_get_private_data(sw);
    return g_markup_printf_escaped("%s", input);
}

// static char *zotero_get_completion(const Mode *sw, unsigned int index);
// static cairo_surface_t *yt_get_icon(const Mode *sw, unsigned int selected_line, unsigned int height);

Mode mode = {
    .abi_version = ABI_VERSION,
    .name = "zotero",
    .cfg_name_key = "display-zotero",
    ._init = zotero_mode_init,
    ._get_num_entries = zotero_mode_get_num_entries,
    ._result = zotero_mode_result,
    ._destroy = zotero_mode_destroy,
    ._token_match = zotero_token_match,
    ._get_display_value = zotero_get_display_value,
    ._get_message = zotero_get_message,
    ._preprocess_input = zotero_preprocess_input,
    // ._get_completion = zotero_get_completion,
    // ._get_icon = zotero_get_icon,
    .private_data = NULL,
    .free = NULL,
};