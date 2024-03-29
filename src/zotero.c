#include <rofi/helper.h>
#include <rofi/history.h>
#include <rofi/mode-private.h>
#include <sqlite3.h>
#include <unistd.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "Plugin_Zotero"
#define QUOTE(...) #__VA_ARGS__

G_MODULE_EXPORT Mode mode;
#define DRUN_CACHE_FILE "rofi3.zoterocache"

// clang-format off
static const char *STATEMENT = QUOTE(
    SELECT
      name,
      path,
      group_concat(author, '; ') as authors,
      SUBSTR(year, 1, INSTR(year || '-', '-') - 1) as year
    FROM
      (
        SELECT
          MAX(
            CASE
              WHEN fieldName = 'title' THEN parentItemDataValues.value
            END
          ) as name,
          'storage/' || items.key || '/' ||
        REPLACE
          (itemAttachments.path, 'storage:', "") as path,
          creators.lastName || ', ' || creators.firstName as author,
          MAX(
            CASE
              WHEN fieldName = 'date' THEN parentItemDataValues.value
            END
          ) as year
        FROM
          itemAttachments
          INNER JOIN items ON items.itemID = itemAttachments.itemID
          INNER JOIN itemData ON itemData.itemID = items.itemID
          INNER JOIN itemDataValues ON itemData.valueID = itemDataValues.valueID
          INNER JOIN items AS parentInfo ON itemAttachments.parentItemID = parentInfo.itemID
          INNER JOIN itemData as parentItemData ON parentItemData.itemID = parentInfo.itemID
          INNER JOIN itemDataValues as parentItemDataValues ON parentItemDataValues.valueID = parentItemData.valueID
          INNER JOIN itemCreators ON itemCreators.itemID = parentInfo.itemID
          INNER JOIN creators ON creators.creatorID = itemCreators.creatorID
          INNER JOIN fields ON fields.fieldID = parentItemData.fieldID
        WHERE
          (
            itemAttachments.contentType LIKE '%pdf'
            OR itemAttachments.contentType LIKE '%djvu'
          )
        GROUP BY
          items.itemID,
          author
        ORDER BY
          itemCreators.orderIndex
      )
    GROUP BY
      name
);
// clang-format on

typedef struct {
    gchar *name;
    gchar *path;
    gchar *author;
    gchar *year;
    int sort_index;
} Entry;

typedef struct {
    sqlite3 *db;
    gchar *zotero_path;
    GPtrArray *entries;
} ZoteroModePrivateData;

static void destroy_element(gpointer data) {
    if (data != NULL) {
        Entry *entry = (Entry *)data;
        g_free(entry->name);
        g_free(entry->path);
        g_free(entry->author);
        g_free(entry->year);
        g_free(entry);
    }
}

static int sort_entries(gconstpointer a, gconstpointer b) {
    Entry *e1 = *((Entry **)a);
    Entry *e2 = *((Entry **)b);
    return e1->sort_index - e2->sort_index;
}

static void get_zotero(Mode *sw) {
    ZoteroModePrivateData *pd = (ZoteroModePrivateData *)mode_get_private_data(sw);
    pd->entries = g_ptr_array_new_with_free_func(destroy_element);
    pd->zotero_path = g_strconcat(g_get_home_dir(), "/Zotero/", NULL);
    gchar *db_name = g_strconcat(pd->zotero_path, "zotero.sqlite", NULL);
    gchar *url = g_strconcat("file:", db_name, "?mode=ro&immutable=1", NULL);
    if (access(db_name, F_OK) == 0) {
        int rc = sqlite3_open_v2(url, &pd->db, SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI, NULL);
        if (rc) {
            g_debug("Can't open database: %s\n", sqlite3_errmsg(pd->db));
            sqlite3_close(pd->db);
        }
    } else {
        g_debug("Database does not exist.");
    }
    g_free(db_name);
    g_free(url);

    sqlite3_stmt *statement = 0;
    sqlite3_prepare_v2(pd->db, STATEMENT, -1, &statement, 0);
    int n = 0;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        Entry *e = g_malloc(sizeof(Entry));
        char *year = (char *)sqlite3_column_text(statement, 3);
        e->name = g_strdup((gchar *)sqlite3_column_text(statement, 0));
        e->path = g_strdup((gchar *)sqlite3_column_text(statement, 1));
        e->author = g_strdup((gchar *)sqlite3_column_text(statement, 2));
        e->year = year == NULL ? g_strdup("") : g_strdup(year);
        e->sort_index = n++;
        g_ptr_array_add(pd->entries, e);
    }
    sqlite3_finalize(statement);

    unsigned int length = 0;
    const char *cache_dir = g_get_user_cache_dir();
    char *path = g_build_filename(cache_dir, DRUN_CACHE_FILE, NULL);
    gchar **retv = history_get_list(path, &length);
    for (unsigned int index = 0; index < length; index++) {
        for (unsigned int i = 0; i < pd->entries->len; i++) {
            Entry *e = g_ptr_array_index(pd->entries, i);
            if (g_strcmp0(e->path, retv[index]) == 0) {
                e->sort_index = -(length - index);
            }
        }
    }
    g_ptr_array_sort(pd->entries, sort_entries);
    g_free(path);
    g_strfreev(retv);
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
        const char *cache_dir = g_get_user_cache_dir();
        char *path = g_build_filename(cache_dir, DRUN_CACHE_FILE, NULL);
        history_set(path, res->path);
        g_free(path);
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
    gsize size = strlen(res->name) + strlen(res->author) + strlen(res->year) + 3 + 3 + 1;
    gchar *buffer = g_newa(gchar, size);
    g_snprintf(buffer, size, "[%s] %s - %s", res->year, res->name, res->author);
    return get_entry ? g_strdup(buffer) : NULL;
}

static int zotero_token_match(const Mode *sw, rofi_int_matcher **tokens, unsigned int index) {
    ZoteroModePrivateData *pd = (ZoteroModePrivateData *)mode_get_private_data(sw);
    Entry *res = g_ptr_array_index(pd->entries, index);
    gsize size = strlen(res->name) + strlen(res->author) + strlen(res->year) + 3 + 3 + 1;
    gchar *buffer = g_newa(gchar, size);
    g_snprintf(buffer, size, "[%s] %s - %s", res->year, res->name, res->author);
    return helper_token_match(tokens, buffer);
}

static char *zotero_get_message(const Mode *sw) { return g_markup_printf_escaped("Results:"); }

static char *zotero_preprocess_input(Mode *sw, const char *input) { return g_markup_printf_escaped("%s", input); }

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
