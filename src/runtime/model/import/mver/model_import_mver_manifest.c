#include "model_import_mver_manifest.h"
#include "../model_import_manifest.h"
#include "bongo_cat/file.h"
#include "bongo_cat/json.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static size_t skip_space(const char *data, size_t size, size_t pos) {
    while (pos < size && (data[pos] == ' ' || data[pos] == '\t' ||
        data[pos] == '\r' || data[pos] == '\n')) pos++;
    return pos;
}

static yyjson_doc *repair_closures(char *data, size_t size,
    const yyjson_read_err *parse_error, yyjson_read_flag flags) {
    size_t first = parse_error->pos;
    /* Legacy hand-edited manifests contain an extra '] }' before the
       actual FileReferences closing brace. Only remove this exact form
       at the parser's error position, then reparse the entire document. */
    if (first >= size || data[first] != ']') return NULL;
    size_t second = skip_space(data, size, first + 1);
    size_t third = skip_space(data, size, second + 1);
    if (second >= size || third >= size || data[second] != '}' ||
        data[third] != '}') return NULL;
    data[first] = data[second] = ' ';
    yyjson_doc *document = yyjson_read_opts(data, size, flags, NULL, NULL);
    yyjson_val *root = document ? yyjson_doc_get_root(document) : NULL;
    if (!yyjson_is_obj(root) ||
        !yyjson_is_obj(yyjson_obj_get(root, "FileReferences"))) {
        yyjson_doc_free(document);
        return NULL;
    }
    return document;
}

yyjson_doc *bongo_cat_import_mver_manifest_read(const char *path,
    bool *repaired) {
    if (repaired) *repaired = false;
    uint64_t size;
    if (!bongo_cat_path_file_size(path, &size) || !size ||
        size > 4u * 1024u * 1024u) return NULL;
    char *data = malloc((size_t)size);
    if (!data) return NULL;
    FILE *file = bongo_cat_file_open(path, "rb");
    bool loaded = file && fread(data, 1, (size_t)size, file) == (size_t)size;
    if (file && fclose(file) != 0) loaded = false;
    if (!loaded) { free(data); return NULL; }
    yyjson_doc *document = yyjson_read_opts(data, (size_t)size, 0, NULL, NULL);
    if (!document) {
        yyjson_read_flag flags = YYJSON_READ_ALLOW_COMMENTS |
            YYJSON_READ_ALLOW_TRAILING_COMMAS;
        yyjson_read_err parse_error = {0};
        document = yyjson_read_opts(data, (size_t)size, flags, NULL, &parse_error);
        if (!document) document = repair_closures(data, (size_t)size,
            &parse_error, flags);
        if (repaired) *repaired = document != NULL;
    }
    free(data);
    return document;
}

bool bongo_cat_import_mver_manifest_valid(const char *root,
    const char *setting) {
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), root, setting)) return false;
    yyjson_doc *document = bongo_cat_import_mver_manifest_read(path, NULL);
    bool valid = bongo_cat_import_manifest_document_valid(root, document);
    yyjson_doc_free(document);
    return valid;
}

bool bongo_cat_import_mver_manifest_copy(const char *root, const char *name,
    const char *source, const char *target, BongoCatError *error) {
    bool repaired = false;
    yyjson_doc *document = bongo_cat_import_mver_manifest_read(source, &repaired);
    if (!repaired) {
        yyjson_doc_free(document);
        return bongo_cat_path_copy_file(source, target);
    }
    bool valid = bongo_cat_import_manifest_document_valid(root, document);
    yyjson_mut_doc *output = valid ? yyjson_doc_mut_copy(document, NULL) : NULL;
    yyjson_doc_free(document);
    bool ok = output && bongo_cat_json_write_file(target, output,
        YYJSON_WRITE_PRETTY, NULL);
    yyjson_mut_doc_free(output);
    if (!ok) bongo_cat_error_set(error,
        valid ? BONGO_CAT_ERROR_IO : BONGO_CAT_ERROR_FORMAT,
        "Cannot normalize Mver model manifest: %s", source);
    else SDL_Log("Normalized Mver model manifest during import: %s (%s)",
        source, name);
    return ok;
}
