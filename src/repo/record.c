#include "wolfram/repo/cbor.h"
#include "cJSON.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static wf_cbor_item *new_item(wf_cbor_type t) {
    wf_cbor_item *v = calloc(1, sizeof(*v)); if (v) v->type = t; return v;
}
static wf_cbor_item *new_string(const char *s) {
    wf_cbor_item *v = new_item(WF_CBOR_STRING); if (!v) return NULL;
    v->string.len = strlen(s); v->string.str = malloc(v->string.len + 1);
    if (!v->string.str) { free(v); return NULL; }
    memcpy(v->string.str, s, v->string.len + 1); return v;
}
static const wf_record_property *property(const wf_record_schema *s, const char *name) {
    for (size_t i = 0; i < s->property_count; i++)
        if (s->properties[i].name && strcmp(s->properties[i].name, name) == 0)
            return &s->properties[i];
    return NULL;
}
static int pair_cmp(const void *a, const void *b) {
    const wf_cbor_pair *x = a, *y = b;
    size_t xn = x->key->string.len, yn = y->key->string.len;
    if (xn != yn) return xn < yn ? -1 : 1;
    return memcmp(x->key->string.str, y->key->string.str, xn);
}
static wf_status convert(const cJSON *, const wf_record_schema *, wf_cbor_item **);

static wf_status convert_object(const cJSON *j, const wf_record_schema *s,
                                wf_cbor_item **out) {
    if (!cJSON_IsObject(j) || (s->property_count && !s->properties))
        return WF_ERR_INVALID_ARG;
    size_t count = 0; const cJSON *child;
    cJSON_ArrayForEach(child, j) {
        if (!child->string || !property(s, child->string)) return WF_ERR_INVALID_ARG;
        for (const cJSON *other = child->next; other; other = other->next)
            if (other->string && strcmp(child->string, other->string) == 0)
                return WF_ERR_INVALID_ARG;
        count++;
    }
    for (size_t i = 0; i < s->property_count; i++) {
        const wf_record_property *p = &s->properties[i];
        if (!p->name || !p->schema ||
            (p->required && !cJSON_GetObjectItemCaseSensitive(j, p->name)))
            return WF_ERR_INVALID_ARG;
        for (size_t k = i + 1; k < s->property_count; k++)
            if (s->properties[k].name && strcmp(p->name, s->properties[k].name) == 0)
                return WF_ERR_INVALID_ARG;
    }
    wf_cbor_item *map = new_item(WF_CBOR_MAP); if (!map) return WF_ERR_ALLOC;
    map->map.count = count; map->map.pairs = calloc(count, sizeof(*map->map.pairs));
    if (count && !map->map.pairs) { wf_cbor_free(map); return WF_ERR_ALLOC; }
    size_t i = 0;
    cJSON_ArrayForEach(child, j) {
        const wf_record_property *p = property(s, child->string);
        map->map.pairs[i].key = new_string(child->string);
        if (!map->map.pairs[i].key) { wf_cbor_free(map); return WF_ERR_ALLOC; }
        wf_status status = convert(child, p->schema, &map->map.pairs[i].value);
        if (status != WF_OK) { wf_cbor_free(map); return status; }
        i++;
    }
    qsort(map->map.pairs, count, sizeof(*map->map.pairs), pair_cmp);
    *out = map; return WF_OK;
}

static wf_status convert(const cJSON *j, const wf_record_schema *s, wf_cbor_item **out) {
    if (!j || !s || !out) return WF_ERR_INVALID_ARG; *out = NULL;
    wf_cbor_item *v = NULL;
    switch (s->type) {
    case WF_RECORD_STRING:
        if (!cJSON_IsString(j) || !j->valuestring) return WF_ERR_INVALID_ARG;
        v = new_string(j->valuestring); break;
    case WF_RECORD_INTEGER:
        if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) ||
            trunc(j->valuedouble) != j->valuedouble || fabs(j->valuedouble) > 9007199254740991.0)
            return WF_ERR_INVALID_ARG;
        v = new_item(j->valuedouble >= 0 ? WF_CBOR_UNSIGNED : WF_CBOR_NEGATIVE);
        if (v && j->valuedouble >= 0) v->uinteger = (uint64_t)j->valuedouble;
        else if (v) v->neginteger = (uint64_t)(-1.0 - j->valuedouble);
        break;
    case WF_RECORD_BOOLEAN:
        if (!cJSON_IsBool(j)) return WF_ERR_INVALID_ARG;
        v = new_item(WF_CBOR_SIMPLE); if (v) v->simple_value = cJSON_IsTrue(j) ? 21 : 20;
        break;
    case WF_RECORD_ARRAY: {
        if (!cJSON_IsArray(j) || !s->items) return WF_ERR_INVALID_ARG;
        int n = cJSON_GetArraySize(j); v = new_item(WF_CBOR_ARRAY); if (!v) break;
        v->children.count = (size_t)n; v->children.items = calloc((size_t)n, sizeof(*v->children.items));
        if (n && !v->children.items) { wf_cbor_free(v); return WF_ERR_ALLOC; }
        for (int i = 0; i < n; i++) { wf_status status = convert(cJSON_GetArrayItem(j, i), s->items, &v->children.items[i]); if (status != WF_OK) { wf_cbor_free(v); return status; } }
        break;
    }
    case WF_RECORD_OBJECT: return convert_object(j, s, out);
    default: return WF_ERR_INVALID_ARG;
    }
    if (!v) return WF_ERR_ALLOC; *out = v; return WF_OK;
}

wf_status wf_record_encode_json(const char *collection, const wf_record_schema *schema,
                                const char *json, unsigned char **out, size_t *out_len) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (!collection || !*collection || !schema || schema->type != WF_RECORD_OBJECT ||
        !json || !out || !out_len) return WF_ERR_INVALID_ARG;
    cJSON *root = cJSON_Parse(json);
    if (!root || !cJSON_IsObject(root) || cJSON_GetObjectItemCaseSensitive(root, "$type")) {
        cJSON_Delete(root); return WF_ERR_INVALID_ARG;
    }
    wf_cbor_item *record = NULL; wf_status status = convert_object(root, schema, &record);
    cJSON_Delete(root); if (status != WF_OK) return status;
    wf_cbor_pair *pairs = realloc(record->map.pairs, (record->map.count + 1) * sizeof(*pairs));
    if (!pairs) { wf_cbor_free(record); return WF_ERR_ALLOC; }
    record->map.pairs = pairs; size_t i = record->map.count++;
    pairs[i].key = new_string("$type"); pairs[i].value = new_string(collection);
    if (!pairs[i].key || !pairs[i].value) { wf_cbor_free(record); return WF_ERR_ALLOC; }
    qsort(pairs, record->map.count, sizeof(*pairs), pair_cmp);
    *out = wf_cbor_serialize(record, out_len); wf_cbor_free(record);
    return *out ? WF_OK : WF_ERR_ALLOC;
}
