#include "sf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* ---- output ---- */

void wf_sf_put(wf_sf_out *out, const char *data, size_t len) {
    if (out->failed) {
        return;
    }
    if (out->len + len + 1 > out->cap) {
        size_t cap = out->cap ? out->cap : 128;
        char *grown;
        while (out->len + len + 1 > cap) {
            cap *= 2;
        }
        grown = realloc(out->data, cap);
        if (!grown) {
            out->failed = 1;
            return;
        }
        out->data = grown;
        out->cap = cap;
    }
    memcpy(out->data + out->len, data, len);
    out->len += len;
    out->data[out->len] = '\0';
}

void wf_sf_puts(wf_sf_out *out, const char *text) {
    wf_sf_put(out, text, strlen(text));
}

char *wf_sf_take(wf_sf_out *out) {
    char *data = out->data;

    if (out->failed || !data) {
        free(data);
        data = NULL;
    }
    memset(out, 0, sizeof(*out));
    return data;
}

int wf_sf_write_string(wf_sf_out *out, const char *text) {
    const char *p;

    for (p = text; *p; p++) {
        if ((unsigned char)*p < 0x20 || (unsigned char)*p > 0x7e) {
            return 0;
        }
    }
    wf_sf_puts(out, "\"");
    for (p = text; *p; p++) {
        if (*p == '"' || *p == '\\') {
            wf_sf_puts(out, "\\");
        }
        wf_sf_put(out, p, 1);
    }
    wf_sf_puts(out, "\"");
    return 1;
}

void wf_sf_write_bytes(wf_sf_out *out, const unsigned char *data, size_t len) {
    size_t i;

    wf_sf_puts(out, ":");
    for (i = 0; i < len; i += 3) {
        unsigned long chunk = (unsigned long)data[i] << 16;
        size_t left = len - i;
        char quad[4];

        if (left > 1) {
            chunk |= (unsigned long)data[i + 1] << 8;
        }
        if (left > 2) {
            chunk |= data[i + 2];
        }
        quad[0] = B64[(chunk >> 18) & 63];
        quad[1] = B64[(chunk >> 12) & 63];
        quad[2] = left > 1 ? B64[(chunk >> 6) & 63] : '=';
        quad[3] = left > 2 ? B64[chunk & 63] : '=';
        wf_sf_put(out, quad, 4);
    }
    wf_sf_puts(out, ":");
}

static void write_bare(wf_sf_out *out, const wf_sf_bare *bare) {
    char number[32];

    switch (bare->type) {
        case WF_SF_INTEGER:
            snprintf(number, sizeof(number), "%lld", bare->integer);
            wf_sf_puts(out, number);
            break;
        case WF_SF_DECIMAL:
        case WF_SF_TOKEN:
            wf_sf_put(out, bare->data, bare->len);
            break;
        case WF_SF_STRING:
            if (!wf_sf_write_string(out, bare->data)) {
                out->failed = 1;
            }
            break;
        case WF_SF_BYTES:
            wf_sf_write_bytes(out, (const unsigned char *)bare->data,
                              bare->len);
            break;
        case WF_SF_BOOLEAN:
            wf_sf_puts(out, bare->integer ? "?1" : "?0");
            break;
        case WF_SF_INNER_LIST:
            break;
    }
}

static int is_true(const wf_sf_bare *bare) {
    return bare->type == WF_SF_BOOLEAN && bare->integer;
}

static void write_params(wf_sf_out *out, const wf_sf_item *item) {
    size_t i;

    for (i = 0; i < item->param_count; i++) {
        wf_sf_puts(out, ";");
        wf_sf_puts(out, item->params[i].key);
        if (!is_true(&item->params[i].value)) {
            wf_sf_puts(out, "=");
            write_bare(out, &item->params[i].value);
        }
    }
}

void wf_sf_write_item(wf_sf_out *out, const wf_sf_item *item) {
    size_t i;

    if (item->bare.type == WF_SF_INNER_LIST) {
        wf_sf_puts(out, "(");
        for (i = 0; i < item->item_count; i++) {
            if (i) {
                wf_sf_puts(out, " ");
            }
            write_bare(out, &item->items[i].bare);
            write_params(out, &item->items[i]);
        }
        wf_sf_puts(out, ")");
    } else {
        write_bare(out, &item->bare);
    }
    write_params(out, item);
}

void wf_sf_write_dict(wf_sf_out *out, const wf_sf_dict *dict) {
    size_t i;

    for (i = 0; i < dict->count; i++) {
        if (i) {
            wf_sf_puts(out, ", ");
        }
        wf_sf_puts(out, dict->members[i].key);
        if (is_true(&dict->members[i].item.bare)) {
            write_params(out, &dict->members[i].item);
        } else {
            wf_sf_puts(out, "=");
            wf_sf_write_item(out, &dict->members[i].item);
        }
    }
}

/* ---- freeing ---- */

static void free_item(wf_sf_item *item) {
    size_t i;

    free(item->bare.data);
    for (i = 0; i < item->param_count; i++) {
        free(item->params[i].key);
        free(item->params[i].value.data);
    }
    free(item->params);
    /* Inner lists do not nest, so this recurses once at most. */
    for (i = 0; i < item->item_count; i++) {
        free_item(&item->items[i]);
    }
    free(item->items);
    memset(item, 0, sizeof(*item));
}

void wf_sf_dict_free(wf_sf_dict *dict) {
    size_t i;

    if (!dict) {
        return;
    }
    for (i = 0; i < dict->count; i++) {
        free(dict->members[i].key);
        free_item(&dict->members[i].item);
    }
    free(dict->members);
    memset(dict, 0, sizeof(*dict));
}

const wf_sf_member *wf_sf_dict_get(const wf_sf_dict *dict, const char *key) {
    size_t i;

    for (i = 0; i < dict->count; i++) {
        if (strcmp(dict->members[i].key, key) == 0) {
            return &dict->members[i];
        }
    }
    return NULL;
}

const wf_sf_bare *wf_sf_param_get(const wf_sf_item *item, const char *key) {
    size_t i;

    for (i = 0; i < item->param_count; i++) {
        if (strcmp(item->params[i].key, key) == 0) {
            return &item->params[i].value;
        }
    }
    return NULL;
}

/* ---- parsing ---- */

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}
static int is_lower(char c) {
    return c >= 'a' && c <= 'z';
}
static int is_alpha(char c) {
    return is_lower(c) || (c >= 'A' && c <= 'Z');
}

static void skip_sp(const char **p) {
    while (**p == ' ') {
        (*p)++;
    }
}

static void skip_ows(const char **p) {
    while (**p == ' ' || **p == '\t') {
        (*p)++;
    }
}

static char *copy_text(const char *start, size_t len) {
    char *s = malloc(len + 1);
    if (s) {
        memcpy(s, start, len);
        s[len] = '\0';
    }
    return s;
}

/* Make room for one more element of `size` bytes, zeroed. */
static wf_status grow(void **array, size_t count, size_t size) {
    unsigned char *grown;

    if (count >= WF_SF_MAX_COUNT) {
        return WF_ERR_PARSE;
    }
    grown = realloc(*array, (count + 1) * size);
    if (!grown) {
        return WF_ERR_ALLOC;
    }
    memset(grown + count * size, 0, size);
    *array = grown;
    return WF_OK;
}

static wf_status parse_key(const char **p, char **out) {
    const char *start = *p;

    if (!is_lower(**p) && **p != '*') {
        return WF_ERR_PARSE;
    }
    while (is_lower(**p) || is_digit(**p) || (**p && strchr("_-.*", **p))) {
        (*p)++;
    }
    *out = copy_text(start, (size_t)(*p - start));
    return *out ? WF_OK : WF_ERR_ALLOC;
}

static wf_status parse_number(const char **p, wf_sf_bare *out) {
    const char *q = *p;
    int negative = 0;
    long long value = 0;
    size_t whole = 0;
    size_t fraction = 0;
    const char *digits;

    if (*q == '-') {
        negative = 1;
        q++;
    }
    digits = q;
    while (is_digit(*q)) {
        if (++whole > 15) {
            return WF_ERR_PARSE;
        }
        value = value * 10 + (*q - '0');
        q++;
    }
    if (whole == 0) {
        return WF_ERR_PARSE;
    }
    if (*q != '.') {
        out->type = WF_SF_INTEGER;
        out->integer = negative ? -value : value;
        *p = q;
        return WF_OK;
    }
    if (whole > 12) {
        return WF_ERR_PARSE;
    }
    q++;
    while (is_digit(*q)) {
        if (++fraction > 3) {
            return WF_ERR_PARSE;
        }
        q++;
    }
    if (fraction == 0) {
        return WF_ERR_PARSE;
    }
    {
        /* Canonical form: no leading zeros, no trailing fraction zeros. */
        const char *end = q;
        wf_sf_out text = {0};

        while (whole > 1 && *digits == '0') {
            digits++;
            whole--;
        }
        while (fraction > 1 && end[-1] == '0') {
            end--;
            fraction--;
        }
        if (negative) {
            wf_sf_puts(&text, "-");
        }
        wf_sf_put(&text, digits, (size_t)(end - digits));
        out->type = WF_SF_DECIMAL;
        out->len = text.len;
        out->data = wf_sf_take(&text);
        if (!out->data) {
            return WF_ERR_ALLOC;
        }
    }
    *p = q;
    return WF_OK;
}

static wf_status parse_string(const char **p, wf_sf_bare *out) {
    const char *q = *p + 1;
    wf_sf_out text = {0};

    wf_sf_put(&text, "", 0);
    for (; *q != '"'; q++) {
        if (*q == '\\') {
            q++;
            if (*q != '"' && *q != '\\') {
                goto bad;
            }
        } else if ((unsigned char)*q < 0x20 || (unsigned char)*q > 0x7e) {
            goto bad;
        }
        wf_sf_put(&text, q, 1);
    }
    out->type = WF_SF_STRING;
    out->len = text.len;
    out->data = wf_sf_take(&text);
    *p = q + 1;
    return out->data ? WF_OK : WF_ERR_ALLOC;
bad:
    free(wf_sf_take(&text));
    return WF_ERR_PARSE;
}

/* Standard base64 between colons; padding may be omitted. */
static wf_status parse_bytes(const char **p, wf_sf_bare *out) {
    const char *q = *p + 1;
    const char *end = strchr(q, ':');
    unsigned long bits = 0;
    int have = 0;
    size_t n = 0;
    size_t len;
    size_t padding = 0;
    size_t i;

    if (!end) {
        return WF_ERR_PARSE;
    }
    len = (size_t)(end - q);
    while (len && q[len - 1] == '=' && padding < 2) {
        len--;
        padding++;
    }
    if (len % 4 == 1 || (padding && (len + padding) % 4 != 0)) {
        return WF_ERR_PARSE;
    }
    out->data = malloc(len / 4 * 3 + 3);
    if (!out->data) {
        return WF_ERR_ALLOC;
    }
    for (i = 0; i < len; i++) {
        const char *digit = q[i] ? strchr(B64, q[i]) : NULL;
        if (!digit) {
            return WF_ERR_PARSE; /* the caller frees out->data */
        }
        bits = ((bits << 6) | (unsigned long)(digit - B64)) & 0xffffffUL;
        have += 6;
        if (have >= 8) {
            have -= 8;
            out->data[n++] = (char)((bits >> have) & 0xff);
        }
    }
    out->data[n] = '\0';
    out->len = n;
    out->type = WF_SF_BYTES;
    *p = end + 1;
    return WF_OK;
}

static wf_status parse_bare(const char **p, wf_sf_bare *out) {
    char c = **p;

    if (c == '-' || is_digit(c)) {
        return parse_number(p, out);
    }
    if (c == '"') {
        return parse_string(p, out);
    }
    if (c == ':') {
        return parse_bytes(p, out);
    }
    if (c == '?') {
        if ((*p)[1] != '0' && (*p)[1] != '1') {
            return WF_ERR_PARSE;
        }
        out->type = WF_SF_BOOLEAN;
        out->integer = (*p)[1] == '1';
        *p += 2;
        return WF_OK;
    }
    if (is_alpha(c) || c == '*') {
        const char *start = *p;
        while (is_alpha(**p) || is_digit(**p) ||
               (**p && strchr("!#$%&'*+-.^_`|~:/", **p))) {
            (*p)++;
        }
        out->type = WF_SF_TOKEN;
        out->len = (size_t)(*p - start);
        out->data = copy_text(start, out->len);
        return out->data ? WF_OK : WF_ERR_ALLOC;
    }
    return WF_ERR_PARSE;
}

static wf_status parse_params(const char **p, wf_sf_item *item) {
    while (**p == ';') {
        char *key = NULL;
        wf_sf_param *param = NULL;
        wf_status status;
        size_t i;

        (*p)++;
        skip_sp(p);
        status = parse_key(p, &key);
        if (status != WF_OK) {
            return status;
        }
        /* A repeated key replaces the earlier value, keeping its place. */
        for (i = 0; i < item->param_count; i++) {
            if (strcmp(item->params[i].key, key) == 0) {
                param = &item->params[i];
                free(param->value.data);
                memset(&param->value, 0, sizeof(param->value));
                free(key);
                break;
            }
        }
        if (!param) {
            status = grow((void **)&item->params, item->param_count,
                          sizeof(*item->params));
            if (status != WF_OK) {
                free(key);
                return status;
            }
            param = &item->params[item->param_count++];
            param->key = key;
        }
        if (**p == '=') {
            (*p)++;
            status = parse_bare(p, &param->value);
            if (status != WF_OK) {
                return status;
            }
        } else {
            param->value.type = WF_SF_BOOLEAN;
            param->value.integer = 1;
        }
    }
    return WF_OK;
}

static wf_status parse_item(const char **p, wf_sf_item *item) {
    wf_status status = parse_bare(p, &item->bare);
    return status == WF_OK ? parse_params(p, item) : status;
}

static wf_status parse_member_value(const char **p, wf_sf_item *item) {
    wf_status status;

    if (**p != '(') {
        return parse_item(p, item);
    }
    (*p)++;
    item->bare.type = WF_SF_INNER_LIST;
    for (;;) {
        skip_sp(p);
        if (**p == ')') {
            (*p)++;
            return parse_params(p, item);
        }
        status =
            grow((void **)&item->items, item->item_count, sizeof(*item->items));
        if (status != WF_OK) {
            return status;
        }
        status = parse_item(p, &item->items[item->item_count++]);
        if (status != WF_OK) {
            return status;
        }
        if (**p != ' ' && **p != ')') {
            return WF_ERR_PARSE;
        }
    }
}

wf_status wf_sf_parse_dict(const char *text, wf_sf_dict *out) {
    const char *p = text;
    wf_status status = WF_OK;

    memset(out, 0, sizeof(*out));
    if (!text || strlen(text) > WF_SF_MAX_INPUT) {
        return WF_ERR_PARSE;
    }
    skip_sp(&p);
    while (*p) {
        char *key = NULL;
        wf_sf_member *member = NULL;
        size_t i;

        status = parse_key(&p, &key);
        if (status != WF_OK) {
            goto fail;
        }
        for (i = 0; i < out->count; i++) {
            if (strcmp(out->members[i].key, key) == 0) {
                member = &out->members[i];
                free_item(&member->item);
                free(key);
                break;
            }
        }
        if (!member) {
            status =
                grow((void **)&out->members, out->count, sizeof(*out->members));
            if (status != WF_OK) {
                free(key);
                goto fail;
            }
            member = &out->members[out->count++];
            member->key = key;
        }
        if (*p == '=') {
            p++;
            status = parse_member_value(&p, &member->item);
        } else {
            member->item.bare.type = WF_SF_BOOLEAN;
            member->item.bare.integer = 1;
            status = parse_params(&p, &member->item);
        }
        if (status != WF_OK) {
            goto fail;
        }
        skip_ows(&p);
        if (!*p) {
            break;
        }
        if (*p != ',') {
            status = WF_ERR_PARSE;
            goto fail;
        }
        p++;
        skip_ows(&p);
        if (!*p) {
            status = WF_ERR_PARSE; /* trailing comma */
            goto fail;
        }
    }
    return WF_OK;
fail:
    wf_sf_dict_free(out);
    return status;
}
