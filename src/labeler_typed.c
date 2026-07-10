/*
 * labeler_typed.c — owned typed parsers + agent convenience wrappers for the
 * labeler / label namespaces. See include/wolfram/labeler_typed.h for the
 * public API, the authoritative wire format, and ownership rules.
 *
 * Mirrors contact_typed.c / admin_typed.c: static strdup/set_string/reset
 * helpers, owned strings, detached `extra` cJSON subtrees where shapes are
 * open/unbounded, and full cleanup on the first error. The agent wrappers call
 * the generated lex wrappers directly after syncing auth via wf_agent_sync_auth.
 */

#include "wolfram/labeler_typed.h"

#include "agent/_internal.h"
#include "wolfram/atproto_lex.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

/* Local copies of the small string/reset helpers (kept static per TU). */
static char *wf_labeler_strdup(const char *s) {
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

static wf_status wf_labeler_set_string(char **dst, const char *src) {
    char *copy = wf_labeler_strdup(src);
    if (src && !copy) {
        return WF_ERR_ALLOC;
    }
    free(*dst);
    *dst = copy;
    return WF_OK;
}

/* ---- label ---- */

static void wf_labeler_label_reset(wf_labeler_label *l) {
    if (!l) {
        return;
    }
    free(l->src);
    free(l->uri);
    free(l->cid);
    free(l->val);
    free(l->cts);
    free(l->exp);
    free(l->sig);
    memset(l, 0, sizeof(*l));
}

static wf_status wf_labeler_parse_label(cJSON *obj, wf_labeler_label *l) {
    wf_status status = WF_OK;
    cJSON *ver = cJSON_GetObjectItemCaseSensitive(obj, "ver");
    cJSON *src = cJSON_GetObjectItemCaseSensitive(obj, "src");
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *val = cJSON_GetObjectItemCaseSensitive(obj, "val");
    cJSON *neg = cJSON_GetObjectItemCaseSensitive(obj, "neg");
    cJSON *cts = cJSON_GetObjectItemCaseSensitive(obj, "cts");
    cJSON *exp = cJSON_GetObjectItemCaseSensitive(obj, "exp");
    cJSON *sig = cJSON_GetObjectItemCaseSensitive(obj, "sig");

    if (cJSON_IsNumber(ver)) {
        l->has_ver = true;
        l->ver = (int64_t)ver->valuedouble;
    }
    if (cJSON_IsString(src) && src->valuestring) {
        status = wf_labeler_set_string(&l->src, src->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(uri) && uri->valuestring) {
        status = wf_labeler_set_string(&l->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        l->has_cid = true;
        status = wf_labeler_set_string(&l->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(val) && val->valuestring) {
        status = wf_labeler_set_string(&l->val, val->valuestring);
    }
    if (status == WF_OK && cJSON_IsBool(neg)) {
        l->has_neg = true;
        l->neg = cJSON_IsTrue(neg);
    }
    if (status == WF_OK && cJSON_IsString(cts) && cts->valuestring) {
        status = wf_labeler_set_string(&l->cts, cts->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(exp) && exp->valuestring) {
        l->has_exp = true;
        status = wf_labeler_set_string(&l->exp, exp->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(sig) && sig->valuestring) {
        l->has_sig = true;
        status = wf_labeler_set_string(&l->sig, sig->valuestring);
    }
    return status;
}

static void wf_labeler_label_list_reset(wf_labeler_label *labels,
                                        size_t count) {
    for (size_t i = 0; i < count; ++i) {
        wf_labeler_label_reset(&labels[i]);
    }
}

/* Parse a "labels" array of label objects (already fetched) into an owned
 * label pointer/count pair. Returns WF_OK / WF_ERR_PARSE / WF_ERR_ALLOC. On
 * error `items` is left NULL and `count` is 0. */
static wf_status wf_labeler_parse_labels_into(cJSON *arr,
                                              wf_labeler_label **out_items,
                                              size_t *out_count) {
    wf_status status = WF_OK;
    if (!cJSON_IsArray(arr)) {
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_labeler_label *items = NULL;
    if (count > 0) {
        items = (wf_labeler_label *)calloc(count, sizeof(*items));
        if (!items) {
            return WF_ERR_ALLOC;
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_labeler_parse_label(obj, &items[i]);
        if (status != WF_OK) {
            wf_labeler_label_reset(&items[i]);
        }
    }

    if (status == WF_OK) {
        *out_items = items;
        *out_count = count;
    } else {
        wf_labeler_label_list_reset(items, count);
        free(items);
        *out_items = NULL;
        *out_count = 0;
    }
    return status;
}

/* ---- label value definitions ---- */

static void wf_labeler_label_value_def_locale_reset(
    wf_labeler_label_value_def_locale *l) {
    if (!l) {
        return;
    }
    free(l->lang);
    free(l->name);
    free(l->description);
    memset(l, 0, sizeof(*l));
}

static void wf_labeler_label_value_def_reset(wf_labeler_label_value_def *d) {
    if (!d) {
        return;
    }
    free(d->identifier);
    free(d->severity);
    free(d->blurs);
    free(d->default_setting);
    for (size_t i = 0; i < d->locale_count; ++i) {
        wf_labeler_label_value_def_locale_reset(&d->locales[i]);
    }
    free(d->locales);
    memset(d, 0, sizeof(*d));
}

static wf_status wf_labeler_parse_label_value_def_locale(
    cJSON *obj, wf_labeler_label_value_def_locale *l) {
    wf_status status = WF_OK;
    cJSON *lang = cJSON_GetObjectItemCaseSensitive(obj, "lang");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "name");
    cJSON *desc = cJSON_GetObjectItemCaseSensitive(obj, "description");
    if (cJSON_IsString(lang) && lang->valuestring) {
        status = wf_labeler_set_string(&l->lang, lang->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_labeler_set_string(&l->name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(desc) && desc->valuestring) {
        status = wf_labeler_set_string(&l->description, desc->valuestring);
    }
    return status;
}

static wf_status wf_labeler_parse_label_value_def(cJSON *obj,
                                                  wf_labeler_label_value_def *d) {
    wf_status status = WF_OK;
    cJSON *identifier = cJSON_GetObjectItemCaseSensitive(obj, "identifier");
    cJSON *severity = cJSON_GetObjectItemCaseSensitive(obj, "severity");
    cJSON *blurs = cJSON_GetObjectItemCaseSensitive(obj, "blurs");
    cJSON *def = cJSON_GetObjectItemCaseSensitive(obj, "defaultSetting");
    cJSON *adult = cJSON_GetObjectItemCaseSensitive(obj, "adultOnly");
    cJSON *locales = cJSON_GetObjectItemCaseSensitive(obj, "locales");

    if (cJSON_IsString(identifier) && identifier->valuestring) {
        status = wf_labeler_set_string(&d->identifier, identifier->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(severity) && severity->valuestring) {
        status = wf_labeler_set_string(&d->severity, severity->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(blurs) && blurs->valuestring) {
        status = wf_labeler_set_string(&d->blurs, blurs->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(def) && def->valuestring) {
        d->has_default_setting = true;
        status = wf_labeler_set_string(&d->default_setting, def->valuestring);
    }
    if (status == WF_OK && cJSON_IsBool(adult)) {
        d->has_adult_only = true;
        d->adult_only = cJSON_IsTrue(adult);
    }
    if (status == WF_OK && cJSON_IsArray(locales)) {
        size_t n = (size_t)cJSON_GetArraySize(locales);
        if (n > 0) {
            d->locales = (wf_labeler_label_value_def_locale *)calloc(
                n, sizeof(*d->locales));
            if (!d->locales) {
                status = WF_ERR_ALLOC;
            }
            for (size_t i = 0; i < n && status == WF_OK; ++i) {
                cJSON *lo = cJSON_GetArrayItem(locales, (int)i);
                if (!cJSON_IsObject(lo)) {
                    status = WF_ERR_PARSE;
                    break;
                }
                status = wf_labeler_parse_label_value_def_locale(lo,
                                                                &d->locales[i]);
                if (status != WF_OK) {
                    wf_labeler_label_value_def_locale_reset(&d->locales[i]);
                }
            }
            if (status == WF_OK) {
                d->locale_count = n;
            } else {
                for (size_t i = 0; i < n; ++i) {
                    wf_labeler_label_value_def_locale_reset(&d->locales[i]);
                }
                free(d->locales);
                d->locales = NULL;
            }
        }
    }
    return status;
}

/* ---- policies ---- */

void wf_labeler_policies_free(wf_labeler_policies *p) {
    if (!p) {
        return;
    }
    for (size_t i = 0; i < p->label_value_count; ++i) {
        free(p->label_values[i]);
    }
    free(p->label_values);
    for (size_t i = 0; i < p->label_value_definition_count; ++i) {
        wf_labeler_label_value_def_reset(&p->label_value_definitions[i]);
    }
    free(p->label_value_definitions);
    memset(p, 0, sizeof(*p));
}

/* Parse an app.bsky.labeler.defs#labelerPolicies object into `p`. Returns
 * WF_OK / WF_ERR_PARSE / WF_ERR_ALLOC. On error `p` is reset. */
static wf_status wf_labeler_parse_policies(cJSON *obj, wf_labeler_policies *p) {
    wf_status status = WF_OK;
    cJSON *values = cJSON_GetObjectItemCaseSensitive(obj, "labelValues");
    cJSON *defs = cJSON_GetObjectItemCaseSensitive(obj, "labelValueDefinitions");

    if (cJSON_IsArray(values)) {
        size_t n = (size_t)cJSON_GetArraySize(values);
        if (n > 0) {
            p->label_values = (char **)calloc(n, sizeof(char *));
            if (!p->label_values) {
                status = WF_ERR_ALLOC;
            }
            for (size_t i = 0; i < n && status == WF_OK; ++i) {
                cJSON *it = cJSON_GetArrayItem(values, (int)i);
                if (!cJSON_IsString(it) || !it->valuestring) {
                    status = WF_ERR_PARSE;
                    break;
                }
                status = wf_labeler_set_string(&p->label_values[i],
                                               it->valuestring);
            }
            if (status == WF_OK) {
                p->label_value_count = n;
            } else {
                for (size_t i = 0; i < n; ++i) {
                    free(p->label_values[i]);
                }
                free(p->label_values);
                p->label_values = NULL;
            }
        }
    } else if (values != NULL) {
        status = WF_ERR_PARSE;
    }
    if (status != WF_OK) {
        wf_labeler_policies_free(p);
        return status;
    }

    if (cJSON_IsArray(defs)) {
        size_t n = (size_t)cJSON_GetArraySize(defs);
        if (n > 0) {
            p->label_value_definitions =
                (wf_labeler_label_value_def *)calloc(n, sizeof(*p->label_value_definitions));
            if (!p->label_value_definitions) {
                status = WF_ERR_ALLOC;
            }
            for (size_t i = 0; i < n && status == WF_OK; ++i) {
                cJSON *it = cJSON_GetArrayItem(defs, (int)i);
                if (!cJSON_IsObject(it)) {
                    status = WF_ERR_PARSE;
                    break;
                }
                status = wf_labeler_parse_label_value_def(
                    it, &p->label_value_definitions[i]);
                if (status != WF_OK) {
                    wf_labeler_label_value_def_reset(
                        &p->label_value_definitions[i]);
                }
            }
            if (status == WF_OK) {
                p->label_value_definition_count = n;
            } else {
                for (size_t i = 0; i < n; ++i) {
                    wf_labeler_label_value_def_reset(
                        &p->label_value_definitions[i]);
                }
                free(p->label_value_definitions);
                p->label_value_definitions = NULL;
            }
        }
    } else if (defs != NULL) {
        status = WF_ERR_PARSE;
    }

    if (status != WF_OK) {
        wf_labeler_policies_free(p);
    }
    return status;
}

/* ---- creator (profileView) ---- */

static void wf_labeler_creator_reset(wf_labeler_creator *c) {
    if (!c) {
        return;
    }
    free(c->did);
    free(c->handle);
    free(c->display_name);
    free(c->avatar);
    if (c->extra) {
        cJSON_Delete(c->extra);
    }
    memset(c, 0, sizeof(*c));
}

static wf_status wf_labeler_parse_creator(cJSON *obj, wf_labeler_creator *c) {
    wf_status status = WF_OK;
    cJSON *did = cJSON_GetObjectItemCaseSensitive(obj, "did");
    cJSON *handle = cJSON_GetObjectItemCaseSensitive(obj, "handle");
    cJSON *name = cJSON_GetObjectItemCaseSensitive(obj, "displayName");
    cJSON *avatar = cJSON_GetObjectItemCaseSensitive(obj, "avatar");
    if (cJSON_IsString(did) && did->valuestring) {
        status = wf_labeler_set_string(&c->did, did->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(handle) && handle->valuestring) {
        status = wf_labeler_set_string(&c->handle, handle->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(name) && name->valuestring) {
        status = wf_labeler_set_string(&c->display_name, name->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(avatar) && avatar->valuestring) {
        status = wf_labeler_set_string(&c->avatar, avatar->valuestring);
    }
    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "did");
        cJSON_DetachItemFromObject(obj, "handle");
        cJSON_DetachItemFromObject(obj, "displayName");
        cJSON_DetachItemFromObject(obj, "avatar");
        c->extra = obj;
    } else {
        wf_labeler_creator_reset(c);
    }
    return status;
}

/* ---- service view ---- */

static void wf_labeler_service_view_reset(wf_labeler_service_view *v) {
    if (!v) {
        return;
    }
    free(v->uri);
    free(v->cid);
    wf_labeler_creator_reset(&v->creator);
    free(v->indexed_at);
    wf_labeler_label_list_reset(v->labels, v->label_count);
    free(v->labels);
    wf_labeler_policies_free(&v->policies);
    for (size_t i = 0; i < v->reason_type_count; ++i) {
        free(v->reason_types[i]);
    }
    free(v->reason_types);
    for (size_t i = 0; i < v->subject_type_count; ++i) {
        free(v->subject_types[i]);
    }
    free(v->subject_types);
    for (size_t i = 0; i < v->subject_collection_count; ++i) {
        free(v->subject_collections[i]);
    }
    free(v->subject_collections);
    if (v->extra) {
        cJSON_Delete(v->extra);
    }
    memset(v, 0, sizeof(*v));
}

static wf_status wf_labeler_parse_string_array(cJSON *arr, char ***out_items,
                                               size_t *out_count) {
    if (!cJSON_IsArray(arr)) {
        return WF_ERR_PARSE;
    }
    size_t n = (size_t)cJSON_GetArraySize(arr);
    if (n == 0) {
        *out_items = NULL;
        *out_count = 0;
        return WF_OK;
    }
    char **items = (char **)calloc(n, sizeof(char *));
    if (!items) {
        return WF_ERR_ALLOC;
    }
    wf_status status = WF_OK;
    for (size_t i = 0; i < n; ++i) {
        cJSON *it = cJSON_GetArrayItem(arr, (int)i);
        if (!cJSON_IsString(it) || !it->valuestring) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_labeler_set_string(&items[i], it->valuestring);
    }
    if (status == WF_OK) {
        *out_items = items;
        *out_count = n;
    } else {
        for (size_t i = 0; i < n; ++i) {
            free(items[i]);
        }
        free(items);
        *out_items = NULL;
        *out_count = 0;
    }
    return status;
}

static wf_status wf_labeler_parse_service_view(cJSON *obj,
                                               wf_labeler_service_view *v) {
    wf_status status = WF_OK;
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(obj, "uri");
    cJSON *cid = cJSON_GetObjectItemCaseSensitive(obj, "cid");
    cJSON *creator = cJSON_GetObjectItemCaseSensitive(obj, "creator");
    cJSON *like = cJSON_GetObjectItemCaseSensitive(obj, "likeCount");
    cJSON *indexed = cJSON_GetObjectItemCaseSensitive(obj, "indexedAt");
    cJSON *labels = cJSON_GetObjectItemCaseSensitive(obj, "labels");
    cJSON *policies = cJSON_GetObjectItemCaseSensitive(obj, "policies");
    cJSON *rt = cJSON_GetObjectItemCaseSensitive(obj, "reasonTypes");
    cJSON *st = cJSON_GetObjectItemCaseSensitive(obj, "subjectTypes");
    cJSON *sc = cJSON_GetObjectItemCaseSensitive(obj, "subjectCollections");

    if (cJSON_IsString(uri) && uri->valuestring) {
        status = wf_labeler_set_string(&v->uri, uri->valuestring);
    }
    if (status == WF_OK && cJSON_IsString(cid) && cid->valuestring) {
        status = wf_labeler_set_string(&v->cid, cid->valuestring);
    }
    if (status == WF_OK && cJSON_IsObject(creator)) {
        status = wf_labeler_parse_creator(creator, &v->creator);
    }
    if (status == WF_OK && cJSON_IsNumber(like)) {
        v->has_like_count = true;
        v->like_count = (int64_t)like->valuedouble;
    }
    if (status == WF_OK && cJSON_IsString(indexed) && indexed->valuestring) {
        status = wf_labeler_set_string(&v->indexed_at, indexed->valuestring);
    }
    if (status == WF_OK && cJSON_IsArray(labels)) {
        status = wf_labeler_parse_labels_into(labels, &v->labels,
                                              &v->label_count);
    }
    if (status == WF_OK && cJSON_IsObject(policies)) {
        v->has_policies = true;
        v->is_detailed = true;
        status = wf_labeler_parse_policies(policies, &v->policies);
    } else if (policies != NULL) {
        status = WF_ERR_PARSE;
    }
    if (status == WF_OK && rt != NULL) {
        status = wf_labeler_parse_string_array(rt, &v->reason_types,
                                               &v->reason_type_count);
    }
    if (status == WF_OK && st != NULL) {
        status = wf_labeler_parse_string_array(st, &v->subject_types,
                                               &v->subject_type_count);
    }
    if (status == WF_OK && sc != NULL) {
        status = wf_labeler_parse_string_array(sc, &v->subject_collections,
                                               &v->subject_collection_count);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(obj, "uri");
        cJSON_DetachItemFromObject(obj, "cid");
        cJSON_DetachItemFromObject(obj, "creator");
        cJSON_DetachItemFromObject(obj, "likeCount");
        cJSON_DetachItemFromObject(obj, "indexedAt");
        cJSON_DetachItemFromObject(obj, "labels");
        cJSON_DetachItemFromObject(obj, "policies");
        cJSON_DetachItemFromObject(obj, "reasonTypes");
        cJSON_DetachItemFromObject(obj, "subjectTypes");
        cJSON_DetachItemFromObject(obj, "subjectCollections");
        v->extra = obj;
    } else {
        wf_labeler_service_view_reset(v);
    }
    return status;
}

/* ---- service record (raw) ---- */

void wf_labeler_service_record_free(wf_labeler_service_record *r) {
    if (!r) {
        return;
    }
    wf_labeler_policies_free(&r->policies);
    for (size_t i = 0; i < r->self_label_count; ++i) {
        free(r->self_label_values[i]);
    }
    free(r->self_label_values);
    free(r->created_at);
    for (size_t i = 0; i < r->reason_type_count; ++i) {
        free(r->reason_types[i]);
    }
    free(r->reason_types);
    for (size_t i = 0; i < r->subject_type_count; ++i) {
        free(r->subject_types[i]);
    }
    free(r->subject_types);
    for (size_t i = 0; i < r->subject_collection_count; ++i) {
        free(r->subject_collections[i]);
    }
    free(r->subject_collections);
    if (r->extra) {
        cJSON_Delete(r->extra);
    }
    memset(r, 0, sizeof(*r));
}

wf_status wf_labeler_parse_service_record(const char *json,
                                                 size_t json_len,
                                                 wf_labeler_service_record *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *policies = cJSON_GetObjectItemCaseSensitive(root, "policies");
    cJSON *labels = cJSON_GetObjectItemCaseSensitive(root, "labels");
    cJSON *created = cJSON_GetObjectItemCaseSensitive(root, "createdAt");
    cJSON *rt = cJSON_GetObjectItemCaseSensitive(root, "reasonTypes");
    cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "subjectTypes");
    cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "subjectCollections");

    if (!cJSON_IsObject(policies)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }
    status = wf_labeler_parse_policies(policies, &out->policies);
    if (status == WF_OK && cJSON_IsObject(labels)) {
        out->has_labels = true;
        cJSON *values = cJSON_GetObjectItemCaseSensitive(labels, "values");
        if (cJSON_IsArray(values)) {
            size_t n = (size_t)cJSON_GetArraySize(values);
            if (n > 0) {
                out->self_label_values = (char **)calloc(n, sizeof(char *));
                if (!out->self_label_values) {
                    status = WF_ERR_ALLOC;
                }
                for (size_t i = 0; i < n && status == WF_OK; ++i) {
                    cJSON *it = cJSON_GetArrayItem(values, (int)i);
                    cJSON *val = cJSON_GetObjectItemCaseSensitive(it, "val");
                    if (!cJSON_IsString(val) || !val->valuestring) {
                        status = WF_ERR_PARSE;
                        break;
                    }
                    status = wf_labeler_set_string(&out->self_label_values[i],
                                                   val->valuestring);
                }
                if (status == WF_OK) {
                    out->self_label_count = n;
                } else {
                    for (size_t i = 0; i < n; ++i) {
                        free(out->self_label_values[i]);
                    }
                    free(out->self_label_values);
                    out->self_label_values = NULL;
                }
            }
        }
    }
    if (status == WF_OK && cJSON_IsString(created) && created->valuestring) {
        status = wf_labeler_set_string(&out->created_at, created->valuestring);
    }
    if (status == WF_OK && rt != NULL) {
        status = wf_labeler_parse_string_array(rt, &out->reason_types,
                                               &out->reason_type_count);
    }
    if (status == WF_OK && st != NULL) {
        status = wf_labeler_parse_string_array(st, &out->subject_types,
                                               &out->subject_type_count);
    }
    if (status == WF_OK && sc != NULL) {
        status = wf_labeler_parse_string_array(sc, &out->subject_collections,
                                               &out->subject_collection_count);
    }

    if (status == WF_OK) {
        cJSON_DetachItemFromObject(root, "policies");
        cJSON_DetachItemFromObject(root, "labels");
        cJSON_DetachItemFromObject(root, "createdAt");
        cJSON_DetachItemFromObject(root, "reasonTypes");
        cJSON_DetachItemFromObject(root, "subjectTypes");
        cJSON_DetachItemFromObject(root, "subjectCollections");
        out->extra = root;
    } else {
        wf_labeler_service_record_free(out);
        cJSON_Delete(root);
    }
    return status;
}

/* ---- top-level parse functions ---- */

wf_status wf_labeler_parse_query_labels(const char *json, size_t json_len,
                                        wf_labeler_label_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = wf_labeler_parse_labels_into(
        cJSON_GetObjectItemCaseSensitive(root, "labels"), &out->labels,
        &out->label_count);
    if (status == WF_OK) {
        cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
        if (cJSON_IsString(cursor) && cursor->valuestring) {
            status = wf_labeler_set_string(&out->cursor, cursor->valuestring);
        }
    }
    if (status != WF_OK) {
        wf_labeler_label_list_reset(out->labels, out->label_count);
        free(out->labels);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_labeler_parse_services(const char *json, size_t json_len,
                                    wf_labeler_service_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = WF_OK;
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "views");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    size_t count = (size_t)cJSON_GetArraySize(arr);
    wf_labeler_service_view *items = NULL;
    cJSON **ptrs = NULL;
    if (count > 0) {
        items = (wf_labeler_service_view *)calloc(count, sizeof(*items));
        ptrs = (cJSON **)calloc(count, sizeof(*ptrs));
        if (!items || !ptrs) {
            free(items);
            free(ptrs);
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
        for (size_t i = 0; i < count; ++i) {
            ptrs[i] = cJSON_GetArrayItem(arr, (int)i);
        }
    }

    for (size_t i = 0; i < count && status == WF_OK; ++i) {
        cJSON *obj = ptrs[i];
        if (!cJSON_IsObject(obj)) {
            status = WF_ERR_PARSE;
            break;
        }
        status = wf_labeler_parse_service_view(obj, &items[i]);
        if (status == WF_OK) {
            items[i].extra = cJSON_DetachItemViaPointer(arr, obj);
        }
        if (status != WF_OK) {
            wf_labeler_service_view_reset(&items[i]);
        }
    }
    free(ptrs);

    if (status == WF_OK) {
        out->services = items;
        out->service_count = count;
    } else {
        for (size_t i = 0; i < count; ++i) {
            wf_labeler_service_view_reset(&items[i]);
        }
        free(items);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

wf_status wf_labeler_parse_temp_labels(const char *json, size_t json_len,
                                       wf_labeler_temp_label_list *out) {
    if (!json || !out) {
        return WF_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(json, json_len);
    if (!root) {
        return WF_ERR_PARSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return WF_ERR_PARSE;
    }

    wf_status status = wf_labeler_parse_labels_into(
        cJSON_GetObjectItemCaseSensitive(root, "labels"), &out->labels,
        &out->label_count);
    if (status != WF_OK) {
        wf_labeler_label_list_reset(out->labels, out->label_count);
        free(out->labels);
        memset(out, 0, sizeof(*out));
    }
    cJSON_Delete(root);
    return status;
}

/* ---- free functions ---- */

void wf_labeler_label_list_free(wf_labeler_label_list *l) {
    if (!l) {
        return;
    }
    wf_labeler_label_list_reset(l->labels, l->label_count);
    free(l->labels);
    free(l->cursor);
    memset(l, 0, sizeof(*l));
}

void wf_labeler_temp_label_list_free(wf_labeler_temp_label_list *l) {
    if (!l) {
        return;
    }
    wf_labeler_label_list_reset(l->labels, l->label_count);
    free(l->labels);
    memset(l, 0, sizeof(*l));
}

void wf_labeler_service_list_free(wf_labeler_service_list *l) {
    if (!l) {
        return;
    }
    for (size_t i = 0; i < l->service_count; ++i) {
        wf_labeler_service_view_reset(&l->services[i]);
    }
    free(l->services);
    memset(l, 0, sizeof(*l));
}

/* ---- Agent convenience wrappers ---- */

wf_status wf_agent_query_labels(wf_agent *agent, const char *const *uris,
                                size_t uri_count, const char *const *sources,
                                size_t source_count,
                                wf_labeler_label_list *out) {
    if (!agent || !agent->client || !uris || uri_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < uri_count; ++i) {
        if (!uris[i] || !uris[i][0]) {
            return WF_ERR_INVALID_ARG;
        }
    }
    if (sources != NULL && source_count > 0) {
        for (size_t i = 0; i < source_count; ++i) {
            if (!sources[i] || !sources[i][0]) {
                return WF_ERR_INVALID_ARG;
            }
        }
    }

    wf_labeler_label_list list = {0};
    wf_lex_com_atproto_label_query_labels_main_params params = {0};
    params.uri_patterns.items = uris;
    params.uri_patterns.count = uri_count;
    if (sources != NULL && source_count > 0) {
        params.has_sources = true;
        params.sources.items = sources;
        params.sources.count = source_count;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_label_query_labels_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_labeler_parse_query_labels(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_get_labeler_services(wf_agent *agent, const char *const *dids,
                                        size_t did_count, bool detailed,
                                        wf_labeler_service_list *out) {
    if (!agent || !agent->client || !dids || did_count == 0 || !out) {
        return WF_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < did_count; ++i) {
        if (!dids[i] || !dids[i][0]) {
            return WF_ERR_INVALID_ARG;
        }
    }

    wf_labeler_service_list list = {0};
    wf_lex_app_bsky_labeler_get_services_main_params params = {0};
    params.dids.items = dids;
    params.dids.count = did_count;
    if (detailed) {
        params.has_detailed = true;
        params.detailed = true;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_app_bsky_labeler_get_services_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_labeler_parse_services(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}

wf_status wf_agent_fetch_labels_typed(wf_agent *agent, const char *did,
                                       wf_labeler_temp_label_list *out) {
    (void)agent;
    (void)did;
    (void)out;
    return WF_ERR_INVALID_ARG;
}

wf_status wf_agent_fetch_labels_list(wf_agent *agent,
                                     int has_since, int64_t since, int limit,
                                     wf_labeler_temp_label_list *out) {
    if (!agent || !agent->client || !out || limit < 0 || limit > 250) {
        return WF_ERR_INVALID_ARG;
    }

    wf_labeler_temp_label_list list = {0};
    wf_lex_com_atproto_temp_fetch_labels_main_params params = {0};
    params.has_since = (has_since != 0);
    params.since = since;
    if (limit != 0) {
        params.has_limit = true;
        params.limit = limit;
    }

    wf_agent_sync_auth(agent);
    wf_response res = {0};
    wf_status status = wf_lex_com_atproto_temp_fetch_labels_main_call(
        agent->client, &params, &res);
    if (status != WF_OK) {
        wf_response_free(&res);
        return status;
    }
    status = wf_labeler_parse_temp_labels(res.body, res.body_len, &list);
    wf_response_free(&res);
    if (status == WF_OK) {
        *out = list;
    }
    return status;
}
