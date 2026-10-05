/*
 * manifest.c -- release manifest parsing (see wolfram/update.h and
 * docs/update.md). Needs cJSON; not part of the Mac OS 9 target.
 */

#include "wolfram/update.h"

#include <cJSON.h>

#include <math.h>
#include <string.h>

#define WF_UPDATE_DEFAULT_MAX_SIZE (64UL * 1024UL * 1024UL)

/* Borrowed non-empty string that fits `cap` (with NUL), else NULL. */
static const char *str_fits(const cJSON *obj, const char *key, size_t cap) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(v) || !v->valuestring || !v->valuestring[0])
        return NULL;
    if (strlen(v->valuestring) >= cap) return NULL;
    return v->valuestring;
}

/* https://host[:port]/..., no userinfo, no whitespace or control bytes. */
static int url_ok(const char *u) {
    const char *p;
    const char *host;
    size_t i;
    if (strncmp(u, "https://", 8) != 0) return 0;
    for (i = 0; u[i]; i++)
        if ((unsigned char)u[i] <= 0x20 || u[i] == 0x7f || u[i] == '\\')
            return 0;
    host = u + 8;
    for (p = host; *p && *p != '/' && *p != '?' && *p != '#'; p++)
        if (*p == '@') return 0;
    return p != host;
}

wf_status wf_update_parse_manifest(const char *body, size_t len,
                                   const wf_update_policy *policy,
                                   wf_update_manifest *out) {
    cJSON *root;
    const cJSON *schema, *asset, *size, *sig;
    const char *app, *version, *name, *url, *sha;
    unsigned long max_size = WF_UPDATE_DEFAULT_MAX_SIZE;
    wf_status rc = WF_ERR_PARSE;

    if (!body || !out) return WF_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (policy && policy->max_size) max_size = policy->max_size;

    root = cJSON_ParseWithLength(body, len);
    if (!cJSON_IsObject(root)) goto done;
    schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    if (!cJSON_IsNumber(schema) || schema->valuedouble != WF_UPDATE_SCHEMA)
        goto done;
    app = str_fits(root, "app", WF_UPDATE_APP_MAX);
    version = str_fits(root, "version", WF_UPDATE_VERSION_MAX);
    if (!app || !version || !wf_update_version_valid(version)) goto done;
    asset = cJSON_GetObjectItemCaseSensitive(root, "asset");
    if (!cJSON_IsObject(asset)) goto done;
    name = str_fits(asset, "name", WF_UPDATE_NAME_MAX);
    url = str_fits(asset, "url", WF_UPDATE_URL_MAX);
    sha = str_fits(asset, "sha256", 65);
    size = cJSON_GetObjectItemCaseSensitive(asset, "size");
    if (!name || !url || !sha || !url_ok(url)) goto done;
    if (!cJSON_IsNumber(size) || size->valuedouble < 1 ||
        size->valuedouble != floor(size->valuedouble) ||
        size->valuedouble > 4294967295.0)
        goto done;
    if (wf_sha256_from_hex(sha, strlen(sha), out->asset.sha256) != WF_OK)
        goto done;

    {
        const cJSON *notes = cJSON_GetObjectItemCaseSensitive(root, "notes");
        if (notes && !cJSON_IsNull(notes)) {
            if (!cJSON_IsString(notes) || !notes->valuestring ||
                strlen(notes->valuestring) >= WF_UPDATE_NOTES_MAX)
                goto done;
            strcpy(out->notes, notes->valuestring);
        }
    }
    sig = cJSON_GetObjectItemCaseSensitive(root, "signature");
    out->has_signature = sig && !cJSON_IsNull(sig);

    out->asset.size = (unsigned long)size->valuedouble;
    strcpy(out->app, app);
    strcpy(out->version, version);
    strcpy(out->asset.name, name);
    strcpy(out->asset.url, url);

    rc = WF_OK;
    if (out->asset.size > max_size)
        rc = WF_ERR_VALIDATION;
    else if (policy && policy->app && strcmp(policy->app, app) != 0)
        rc = WF_ERR_VALIDATION;
    else if (policy && policy->url_prefix &&
             strncmp(url, policy->url_prefix, strlen(policy->url_prefix)) != 0)
        rc = WF_ERR_VALIDATION;
    else if (policy && policy->url_prefix &&
             strncmp(policy->url_prefix, "https://", 8) != 0)
        rc = WF_ERR_VALIDATION;

done:
    cJSON_Delete(root);
    if (rc != WF_OK) memset(out, 0, sizeof(*out));
    return rc;
}
