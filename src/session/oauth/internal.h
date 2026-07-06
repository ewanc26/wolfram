#ifndef WOLFRAM_SESSION_OAUTH_INTERNAL_H
#define WOLFRAM_SESSION_OAUTH_INTERNAL_H

#include <openssl/ec.h>

#include <cJSON.h>

#include "wolfram/oauth.h"

#ifdef __cplusplus
extern "C" {
#endif

char *wf_oauth_strdup(const char *value);
void wf_oauth_string_list_free(wf_oauth_string_list *list);
int wf_oauth_string_list_has(const wf_oauth_string_list *list,
                             const char *value);

wf_status wf_oauth_json_string(const cJSON *object, const char *name,
                               int required, char **out);
wf_status wf_oauth_json_array(const cJSON *object, const char *name,
                              int required, wf_oauth_string_list *out);
wf_status wf_oauth_json_bool(const cJSON *object, const char *name,
                             int *value, int *present);
wf_status wf_oauth_json_object_encoded(const cJSON *object, const char *name,
                                       char **out);
wf_status wf_oauth_positive_integer(const cJSON *root, const char *name,
                                    int required, int64_t *out, int *present);

int wf_oauth_url_valid(const char *url, int https_only, int origin_only,
                       int reject_port);
int wf_oauth_ascii_equal_fold(const char *left, const char *right);
int wf_oauth_url_hosts_equal(const char *left, const char *right);
int wf_oauth_client_id_valid(const char *client_id);
int wf_oauth_scope_has(const char *scope, const char *wanted);

wf_status wf_oauth_base64url(const unsigned char *input, size_t input_len,
                             char **out);
wf_status wf_oauth_random_jti(char **out);
wf_status wf_oauth_dpop_coordinates(const wf_oauth_dpop_key *key,
                                    unsigned char x[32], unsigned char y[32]);
wf_status wf_oauth_dpop_jwk(const wf_oauth_dpop_key *key, cJSON **out,
                            char **x_out, char **y_out);
wf_status wf_oauth_verify_token_subject(wf_xrpc_client *transport,
                                        const char *subject,
                                        const char *issuer,
                                        char **audience_out);

#ifdef __cplusplus
}
#endif

#endif /* WOLFRAM_SESSION_OAUTH_INTERNAL_H */
