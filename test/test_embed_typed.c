/*
 * test_embed_typed.c — offline tests for the getEmbedExternalView typed parser
 * and agent wrapper. Builds representative response bodies with cJSON, asserts
 * the owned struct is populated correctly, then frees it. The agent wrapper
 * requires live auth and is exercised only for argument validation.
 */

#include "wolfram/embed_typed.h"
#include "test.h"

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *json_of(cJSON *root) {
    char *s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

int main(void) {
    /* ---- Invalid args -> WF_ERR_INVALID_ARG ---- */
    wf_embed_external_view v = {0};
    WF_CHECK(wf_embed_parse_external_view(NULL, 0, &v) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_embed_parse_external_view("{}", 2, NULL) == WF_ERR_INVALID_ARG);

    /* ---- Malformed JSON -> WF_ERR_PARSE ---- */
    WF_CHECK(wf_embed_parse_external_view("not json", 8, &v) == WF_ERR_PARSE);
    WF_CHECK(wf_embed_parse_external_view("[]", 2, &v) == WF_ERR_PARSE);

    /* ---- Empty ({}) response: no view resolved ---- */
    WF_CHECK(wf_embed_parse_external_view("{}", 2, &v) == WF_OK);
    WF_CHECK(v.has_view == false);
    WF_CHECK(v.uri == NULL && v.view == NULL);
    WF_CHECK(v.associated_refs == NULL && v.associated_records == NULL);
    wf_embed_external_view_free(&v);

    /* ---- Full hydrated view round-trip ---- */
    cJSON *root = cJSON_CreateObject();

    cJSON *view = cJSON_CreateObject();
    cJSON *external = cJSON_CreateObject();
    cJSON_AddStringToObject(external, "uri", "https://example.com/article");
    cJSON_AddStringToObject(external, "title", "An Example Article");
    cJSON_AddStringToObject(external, "description", "A short description.");
    cJSON_AddStringToObject(external, "thumb",
                            "https://cdn.bsky.app/img/thumb.jpg");
    /* An open/unbounded nested field that should be preserved inside `view`. */
    cJSON *source = cJSON_CreateObject();
    cJSON_AddStringToObject(source, "title", "Example Publication");
    cJSON_AddItemToObject(external, "source", source);
    cJSON_AddItemToObject(view, "external", external);
    cJSON_AddItemToObject(root, "view", view);

    cJSON *refs = cJSON_CreateArray();
    cJSON *ref = cJSON_CreateObject();
    cJSON_AddStringToObject(ref, "uri",
                            "at://did:plc:x/site.standard.document/1");
    cJSON_AddStringToObject(ref, "cid", "bafyref1111111111111111111111111111");
    cJSON_AddItemToArray(refs, ref);
    cJSON_AddItemToObject(root, "associatedRefs", refs);

    cJSON *records = cJSON_CreateArray();
    cJSON *rec = cJSON_CreateObject();
    cJSON_AddStringToObject(rec, "$type", "site.standard.document");
    cJSON_AddItemToArray(records, rec);
    cJSON_AddItemToObject(root, "associatedRecords", records);

    char *body = json_of(root);

    WF_CHECK(wf_embed_parse_external_view(body, strlen(body), &v) == WF_OK);
    WF_CHECK(v.has_view == true);
    WF_CHECK(v.uri && strcmp(v.uri, "https://example.com/article") == 0);
    WF_CHECK(v.title && strcmp(v.title, "An Example Article") == 0);
    WF_CHECK(v.description && strcmp(v.description, "A short description.") == 0);
    WF_CHECK(v.thumb &&
             strcmp(v.thumb, "https://cdn.bsky.app/img/thumb.jpg") == 0);
    /* The full "view" subtree is retained, including nested open fields. */
    WF_CHECK(v.view != NULL);
    cJSON *v_external = cJSON_GetObjectItemCaseSensitive(v.view, "external");
    WF_CHECK(cJSON_IsObject(v_external));
    cJSON *v_source = cJSON_GetObjectItemCaseSensitive(v_external, "source");
    WF_CHECK(cJSON_IsObject(v_source));
    /* Detached arrays are preserved. */
    WF_CHECK(cJSON_IsArray(v.associated_refs) &&
             cJSON_GetArraySize(v.associated_refs) == 1);
    WF_CHECK(cJSON_IsArray(v.associated_records) &&
             cJSON_GetArraySize(v.associated_records) == 1);

    wf_embed_external_view_free(&v);
    /* Free resets the struct (idempotent). */
    WF_CHECK(v.uri == NULL && v.view == NULL && v.has_view == false);
    wf_embed_external_view_free(&v); /* second free on a reset struct is safe */
    free(body);

    /* ---- View present but without an "external" object ---- */
    cJSON *root2 = cJSON_CreateObject();
    cJSON *view2 = cJSON_CreateObject();
    cJSON_AddItemToObject(root2, "view", view2);
    char *body2 = json_of(root2);
    WF_CHECK(wf_embed_parse_external_view(body2, strlen(body2), &v) == WF_OK);
    WF_CHECK(v.has_view == true);
    WF_CHECK(v.uri == NULL && v.title == NULL);
    WF_CHECK(v.view != NULL);
    wf_embed_external_view_free(&v);
    free(body2);

    /* ---- Agent wrapper argument validation ---- */
    const char *uris[] = {"at://did:plc:x/site.standard.document/1"};
    const char *empty_uris[] = {""};
    WF_CHECK(wf_agent_get_embed_external_view_typed(
                 NULL, "https://example.com", uris, 1, &v) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_embed_external_view_typed(
                 NULL, NULL, uris, 1, &v) == WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_embed_external_view_typed(
                 NULL, "https://example.com", NULL, 0, &v) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_embed_external_view_typed(
                 NULL, "https://example.com", empty_uris, 1, &v) ==
             WF_ERR_INVALID_ARG);
    WF_CHECK(wf_agent_get_embed_external_view_typed(
                 NULL, "https://example.com", uris, 1, NULL) ==
             WF_ERR_INVALID_ARG);

    printf("embed_typed: all checks passed\n");
    return 0;
}
