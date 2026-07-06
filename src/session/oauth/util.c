#include "internal.h"

#include <curl/curl.h>
#include <ctype.h>
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdlib.h>
#include <string.h>

char *wf_oauth_strdup(const char *value) {
    size_t len;
    char *copy;
    if (!value) return NULL;
    len = strlen(value);
    copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, value, len + 1);
    return copy;
}

void wf_oauth_string_free(char *value) {
    free(value);
}

void wf_oauth_string_list_free(wf_oauth_string_list *list) {
    size_t i;
    if (!list) return;
    for (i = 0; i < list->count; i++) free(list->items[i]);
    free(list->items);
    memset(list, 0, sizeof(*list));
}

int wf_oauth_string_list_has(const wf_oauth_string_list *list,
                             const char *value) {
    size_t i;
    if (!list || !value) return 0;
    for (i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], value) == 0) return 1;
    }
    return 0;
}

wf_status wf_oauth_json_string(const cJSON *object, const char *name,
                               int required, char **out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    *out = NULL;
    if (!item) return required ? WF_ERR_PARSE : WF_OK;
    if (!cJSON_IsString(item) || !item->valuestring || item->valuestring[0] == '\0') {
        return WF_ERR_PARSE;
    }
    *out = wf_oauth_strdup(item->valuestring);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

wf_status wf_oauth_json_array(const cJSON *object, const char *name,
                              int required, wf_oauth_string_list *out) {
    const cJSON *array = cJSON_GetObjectItemCaseSensitive(object, name);
    const cJSON *item;
    size_t index = 0;
    memset(out, 0, sizeof(*out));
    if (!array) return required ? WF_ERR_PARSE : WF_OK;
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array) == 0) return WF_ERR_PARSE;
    out->count = (size_t)cJSON_GetArraySize(array);
    out->items = calloc(out->count, sizeof(*out->items));
    if (!out->items) return WF_ERR_ALLOC;
    cJSON_ArrayForEach(item, array) {
        if (!cJSON_IsString(item) || !item->valuestring || item->valuestring[0] == '\0') {
            wf_oauth_string_list_free(out);
            return WF_ERR_PARSE;
        }
        out->items[index] = wf_oauth_strdup(item->valuestring);
        if (!out->items[index]) {
            wf_oauth_string_list_free(out);
            return WF_ERR_ALLOC;
        }
        index++;
    }
    return WF_OK;
}

wf_status wf_oauth_json_bool(const cJSON *object, const char *name,
                             int *value, int *present) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    *value = 0;
    if (present) *present = item != NULL;
    if (!item) return WF_OK;
    if (!cJSON_IsBool(item)) return WF_ERR_PARSE;
    *value = cJSON_IsTrue(item) ? 1 : 0;
    return WF_OK;
}

wf_status wf_oauth_json_object_encoded(const cJSON *object, const char *name,
                                       char **out) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    *out = NULL;
    if (!item) return WF_OK;
    if (!cJSON_IsObject(item)) return WF_ERR_PARSE;
    *out = cJSON_PrintUnformatted(item);
    return *out ? WF_OK : WF_ERR_ALLOC;
}

wf_status wf_oauth_positive_integer(const cJSON *root, const char *name,
                                    int required, int64_t *out, int *present) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    int64_t value;
    if (present) *present = item != NULL;
    if (!item) return required ? WF_ERR_PARSE : WF_OK;
    if (!cJSON_IsNumber(item) || item->valuedouble <= 0 ||
        item->valuedouble > 9007199254740991.0) return WF_ERR_PARSE;
    value = (int64_t)item->valuedouble;
    if ((double)value != item->valuedouble) return WF_ERR_PARSE;
    *out = value;
    return WF_OK;
}

int wf_oauth_url_valid(const char *url, int https_only, int origin_only,
                       int reject_port) {
    CURLU *parsed = NULL;
    char *scheme = NULL, *host = NULL, *part = NULL;
    int valid = 0;
    if (!url || !*url) return 0;
    parsed = curl_url();
    if (!parsed) return 0;
    if (curl_url_set(parsed, CURLUPART_URL, url, CURLU_NON_SUPPORT_SCHEME) != CURLUE_OK) {
        goto done;
    }
    if (curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        !scheme || !host || !*host) {
        goto done;
    }
    if (https_only && strcmp(scheme, "https") != 0) goto done;
    if (!https_only && strcmp(scheme, "https") != 0 && strcmp(scheme, "http") != 0) {
        goto done;
    }
    if (curl_url_get(parsed, CURLUPART_USER, &part, 0) == CURLUE_OK) goto done;
    if (curl_url_get(parsed, CURLUPART_PASSWORD, &part, 0) == CURLUE_OK) goto done;
    if (curl_url_get(parsed, CURLUPART_FRAGMENT, &part, 0) == CURLUE_OK) goto done;
    if (reject_port && curl_url_get(parsed, CURLUPART_PORT, &part, 0) == CURLUE_OK) goto done;
    if (origin_only && curl_url_get(parsed, CURLUPART_PORT, &part, 0) == CURLUE_OK) {
        if ((strcmp(scheme, "https") == 0 && strcmp(part, "443") == 0) ||
            (strcmp(scheme, "http") == 0 && strcmp(part, "80") == 0)) {
            goto done;
        }
        curl_free(part);
        part = NULL;
    }
    if (origin_only) {
        if (curl_url_get(parsed, CURLUPART_QUERY, &part, 0) == CURLUE_OK) goto done;
        if (curl_url_get(parsed, CURLUPART_PATH, &part, 0) == CURLUE_OK &&
            part && strcmp(part, "/") != 0 && part[0] != '\0') {
            goto done;
        }
        curl_free(part);
        part = NULL;
    }
    valid = 1;
done:
    curl_free(part);
    curl_free(host);
    curl_free(scheme);
    curl_url_cleanup(parsed);
    return valid;
}

int wf_oauth_ascii_equal_fold(const char *left, const char *right) {
    if (!left || !right) return 0;
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) return 0;
        left++;
        right++;
    }
    return *left == *right;
}

int wf_oauth_url_hosts_equal(const char *left, const char *right) {
    CURLU *left_url = NULL, *right_url = NULL;
    char *left_host = NULL, *right_host = NULL;
    int equal = 0;
    left_url = curl_url();
    right_url = curl_url();
    if (!left_url || !right_url ||
        curl_url_set(left_url, CURLUPART_URL, left, 0) != CURLUE_OK ||
        curl_url_set(right_url, CURLUPART_URL, right, 0) != CURLUE_OK ||
        curl_url_get(left_url, CURLUPART_HOST, &left_host, 0) != CURLUE_OK ||
        curl_url_get(right_url, CURLUPART_HOST, &right_host, 0) != CURLUE_OK) {
        goto done;
    }
    equal = wf_oauth_ascii_equal_fold(left_host, right_host);
done:
    curl_free(left_host);
    curl_free(right_host);
    curl_url_cleanup(left_url);
    curl_url_cleanup(right_url);
    return equal;
}

int wf_oauth_client_id_valid(const char *client_id) {
    CURLU *parsed = NULL;
    char *path = NULL, *host = NULL;
    size_t path_len, i;
    int host_is_ipv4 = 1, valid = 0;
    if (!wf_oauth_url_valid(client_id, 1, 0, 1)) return 0;
    parsed = curl_url();
    if (!parsed || curl_url_set(parsed, CURLUPART_URL, client_id, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_PATH, &path, 0) != CURLUE_OK ||
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) != CURLUE_OK) {
        goto done;
    }
    path_len = strlen(path);
    if (path_len <= 1 || path[path_len - 1] == '/') goto done;
    if (strchr(host, ':')) goto done;
    for (i = 0; host[i]; i++) {
        if (!isdigit((unsigned char)host[i]) && host[i] != '.') {
            host_is_ipv4 = 0;
            break;
        }
    }
    if (host_is_ipv4) goto done;
    valid = 1;
done:
    curl_free(path);
    curl_free(host);
    curl_url_cleanup(parsed);
    return valid;
}

int wf_oauth_scope_has(const char *scope, const char *wanted) {
    const char *cursor;
    size_t wanted_len;
    if (!scope || !wanted) return 0;
    cursor = scope;
    wanted_len = strlen(wanted);
    while (*cursor) {
        const char *end;
        while (*cursor == ' ') cursor++;
        end = strchr(cursor, ' ');
        if (!end) end = cursor + strlen(cursor);
        if ((size_t)(end - cursor) == wanted_len &&
            memcmp(cursor, wanted, wanted_len) == 0) {
            return 1;
        }
        cursor = end;
    }
    return 0;
}

wf_status wf_oauth_base64url(const unsigned char *input, size_t input_len,
                             char **out) {
    size_t encoded_len = 4 * ((input_len + 2) / 3);
    char *encoded;
    int result;
    if ((!input && input_len != 0) || !out || input_len > (size_t)INT_MAX) {
        return WF_ERR_INVALID_ARG;
    }
    *out = NULL;
    encoded = malloc(encoded_len + 1);
    if (!encoded) return WF_ERR_ALLOC;
    result = EVP_EncodeBlock((unsigned char *)encoded, input, (int)input_len);
    if (result < 0) {
        free(encoded);
        return WF_ERR_PARSE;
    }
    while (result > 0 && encoded[result - 1] == '=') result--;
    encoded[result] = '\0';
    for (int i = 0; i < result; i++) {
        if (encoded[i] == '+') encoded[i] = '-';
        else if (encoded[i] == '/') encoded[i] = '_';
    }
    *out = encoded;
    return WF_OK;
}

wf_status wf_oauth_random_jti(char **out) {
    unsigned char random[16];
    if (!out) return WF_ERR_INVALID_ARG;
    if (RAND_bytes(random, sizeof(random)) != 1) return WF_ERR_PARSE;
    return wf_oauth_base64url(random, sizeof(random), out);
}
