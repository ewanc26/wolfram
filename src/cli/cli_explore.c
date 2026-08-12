#include "cli_explore.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/identity.h"
#include "wolfram/plc.h"
#include "wolfram/repo_typed.h"

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default public PLC directory. Overridable with --plc-directory for
 * pointing at a test/self-hosted directory. */
#define DEFAULT_PLC_DIRECTORY "https://plc.directory"

int cmd_browse(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "error: usage: wolfram browse <service> <handle-or-did> "
                "[collection] [rkey] [--limit N] [--cursor C] [--json]\n");
        return 1;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    const char *collection = NULL;
    const char *rkey = NULL;
    int limit = 50;
    const char *cursor = NULL;

    /* collection and rkey are positional if present; anything starting
     * with "--" ends the positional run. */
    int i = 3;
    if (i < argc && strncmp(argv[i], "--", 2) != 0) collection = argv[i++];
    if (i < argc && strncmp(argv[i], "--", 2) != 0) rkey = argv[i++];
    for (; i < argc; ++i) {
        if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
            limit = atoi(argv[++i]);
        else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
            cursor = argv[++i];
        else if (strcmp(argv[i], "--json") == 0)
            g_json = true;
    }

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve '%s' (status %d)\n", actor,
                (int)s);
        free(did);
        wf_agent_free(agent);
        return 1;
    }

    int rc = 0;
    if (!collection) {
        wf_repo_description desc = {0};
        s = wf_agent_describe_repo_typed(agent, did, &desc);
        if (s != WF_OK) {
            fprintf(stderr, "error: describeRepo failed (status %d)\n", (int)s);
            rc = 1;
        } else if (g_json) {
            cJSON *out = cJSON_CreateObject();
            cJSON_AddStringToObject(out, "did", desc.did ? desc.did : did);
            cJSON_AddStringToObject(out, "handle",
                                    desc.handle ? desc.handle : "");
            if (desc.has_handle_is_correct)
                cJSON_AddBoolToObject(out, "handleIsCorrect",
                                      desc.handle_is_correct);
            cJSON *cols = cJSON_CreateArray();
            for (size_t c = 0; c < desc.collection_count; ++c)
                if (desc.collections[c])
                    cJSON_AddItemToArray(
                        cols, cJSON_CreateString(desc.collections[c]));
            cJSON_AddItemToObject(out, "collections", cols);
            if (desc.did_doc)
                cJSON_AddItemToObject(out, "didDoc",
                                      cJSON_Duplicate(desc.did_doc, true));
            char *pretty = cJSON_Print(out);
            if (pretty) {
                printf("%s\n", pretty);
                free(pretty);
            }
            cJSON_Delete(out);
        } else {
            printf("did: %s\n", desc.did ? desc.did : did);
            printf("handle: %s\n", desc.handle ? desc.handle : "?");
            if (desc.has_handle_is_correct)
                printf("handle verified: %s\n",
                       desc.handle_is_correct ? "yes" : "no");
            printf("collections (%zu):\n", desc.collection_count);
            for (size_t c = 0; c < desc.collection_count; ++c)
                printf("  %s\n",
                       desc.collections[c] ? desc.collections[c] : "?");
            if (desc.collection_count == 0) printf("  (none)\n");
        }
        wf_repo_description_free(&desc);
    } else if (!rkey) {
        wf_repo_record_list list = {0};
        s = wf_agent_list_records_typed(agent, did, collection, limit, cursor,
                                        0, &list);
        if (s != WF_OK) {
            fprintf(stderr, "error: listRecords failed (status %d)\n", (int)s);
            rc = 1;
        } else if (g_json) {
            cJSON *out = cJSON_CreateObject();
            cJSON *records = cJSON_CreateArray();
            for (size_t r = 0; r < list.count; ++r) {
                cJSON *rec = cJSON_CreateObject();
                if (list.items[r].uri)
                    cJSON_AddStringToObject(rec, "uri", list.items[r].uri);
                if (list.items[r].has_cid && list.items[r].cid)
                    cJSON_AddStringToObject(rec, "cid", list.items[r].cid);
                if (list.items[r].value)
                    cJSON_AddItemToObject(
                        rec, "value",
                        cJSON_Duplicate(list.items[r].value, true));
                cJSON_AddItemToArray(records, rec);
            }
            cJSON_AddItemToObject(out, "records", records);
            if (list.cursor)
                cJSON_AddStringToObject(out, "cursor", list.cursor);
            char *pretty = cJSON_Print(out);
            if (pretty) {
                printf("%s\n", pretty);
                free(pretty);
            }
            cJSON_Delete(out);
        } else {
            printf("%zu record(s) in %s:\n", list.count, collection);
            for (size_t r = 0; r < list.count; ++r) {
                const char *uri = list.items[r].uri ? list.items[r].uri : "";
                const char *slash = strrchr(uri, '/');
                printf("  %-20s cid=%s\n", slash ? slash + 1 : uri,
                       list.items[r].has_cid && list.items[r].cid
                           ? list.items[r].cid
                           : "?");
            }
            if (list.count == 0) printf("  (empty)\n");
            if (list.cursor) printf("cursor: %s\n", list.cursor);
        }
        wf_repo_record_list_free(&list);
    } else {
        wf_repo_record record = {0};
        s = wf_agent_get_record_typed(agent, did, collection, rkey, NULL,
                                      &record);
        if (s != WF_OK) {
            fprintf(stderr, "error: getRecord failed (status %d)\n", (int)s);
            rc = 1;
        } else {
            printf("uri: %s\n", record.uri ? record.uri : "?");
            printf("cid: %s\n",
                   record.has_cid && record.cid ? record.cid : "?");
            if (record.value) {
                char *pretty = g_json ? cJSON_PrintUnformatted(record.value)
                                      : cJSON_Print(record.value);
                if (pretty) {
                    printf("%s\n", pretty);
                    free(pretty);
                }
            }
        }
        wf_repo_record_free(&record);
    }

    free(did);
    wf_agent_free(agent);
    return rc;
}

/* Print an array of {id, type, publicKeyMultibase} verification-method
 * objects (a DID document's `verificationMethod`). */
static void print_verification_methods(const cJSON *vms) {
    printf("verification methods:\n");
    if (!cJSON_IsArray(vms) || cJSON_GetArraySize(vms) == 0) {
        printf("  (none)\n");
        return;
    }
    const cJSON *vm = NULL;
    cJSON_ArrayForEach(vm, vms) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(vm, "id");
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(vm, "type");
        const cJSON *key =
            cJSON_GetObjectItemCaseSensitive(vm, "publicKeyMultibase");
        printf("  %s (%s) %s\n", cJSON_IsString(id) ? id->valuestring : "?",
               cJSON_IsString(type) ? type->valuestring : "?",
               cJSON_IsString(key) ? key->valuestring : "");
    }
}

/* Print an array of {id, type, serviceEndpoint} service objects (a DID
 * document's `service`). */
static void print_services(const cJSON *services) {
    printf("services:\n");
    if (!cJSON_IsArray(services) || cJSON_GetArraySize(services) == 0) {
        printf("  (none)\n");
        return;
    }
    const cJSON *svc = NULL;
    cJSON_ArrayForEach(svc, services) {
        const cJSON *id = cJSON_GetObjectItemCaseSensitive(svc, "id");
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(svc, "type");
        const cJSON *endpoint =
            cJSON_GetObjectItemCaseSensitive(svc, "serviceEndpoint");
        printf("  %s (%s) -> %s\n", cJSON_IsString(id) ? id->valuestring : "?",
               cJSON_IsString(type) ? type->valuestring : "?",
               cJSON_IsString(endpoint) ? endpoint->valuestring : "?");
    }
}

/* Print every alsoKnownAs handle claim with a live bidirectional
 * verification check against `agent`. */
static void print_handles(wf_agent *agent, const cJSON *aka) {
    printf("handles:\n");
    if (!cJSON_IsArray(aka) || cJSON_GetArraySize(aka) == 0) {
        printf("  (none)\n");
        return;
    }
    const cJSON *h = NULL;
    cJSON_ArrayForEach(h, aka) {
        if (!cJSON_IsString(h)) continue;
        const char *raw = h->valuestring;
        const char *handle = strncmp(raw, "at://", 5) == 0 ? raw + 5 : raw;
        int valid = 0;
        wf_status vs = wf_agent_verify_handle(agent, handle, &valid);
        const char *tag = vs != WF_OK ? "[unverified: lookup failed]"
                          : valid     ? "[verified]"
                                      : "[MISMATCH]";
        printf("  %s %s\n", raw, tag);
    }
}

/* Fetch and print the account's current PLC rotation keys. Rotation keys
 * are a PLC-log concept, not part of the resolved (did:core-shaped) DID
 * document, so this is a separate fetch from the identity doc above. */
static void print_plc_rotation_keys(const char *did,
                                    const char *plc_directory) {
    wf_xrpc_client *client = wf_xrpc_client_new(plc_directory);
    if (!client) return;

    char *cid = NULL;
    char *op_json = NULL;
    wf_status s =
        wf_plc_get_last_op(client, plc_directory, did, &cid, &op_json);
    wf_xrpc_client_free(client);
    if (s != WF_OK || !op_json) {
        free(cid);
        free(op_json);
        return;
    }

    cJSON *op = cJSON_Parse(op_json);
    free(cid);
    free(op_json);
    if (!op) return;

    cJSON *rk = cJSON_GetObjectItemCaseSensitive(op, "rotationKeys");
    printf("rotation keys (PLC):\n");
    if (cJSON_IsArray(rk) && cJSON_GetArraySize(rk) > 0) {
        cJSON *k = NULL;
        cJSON_ArrayForEach(k, rk) {
            if (cJSON_IsString(k)) printf("  %s\n", k->valuestring);
        }
    } else {
        printf("  (none)\n");
    }
    cJSON_Delete(op);
}

int cmd_identity(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
                "error: usage: wolfram identity <service> <handle-or-did> "
                "[--plc-directory URL] [--json]\n");
        return 1;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    const char *plc_directory = DEFAULT_PLC_DIRECTORY;
    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--plc-directory") == 0 && i + 1 < argc)
            plc_directory = argv[++i];
        else if (strcmp(argv[i], "--json") == 0)
            g_json = true;
    }

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve '%s' (status %d)\n", actor,
                (int)s);
        free(did);
        wf_agent_free(agent);
        return 1;
    }

    /* Resolved directly against the DID's own authority (PLC directory for
     * did:plc, HTTPS well-known for did:web) via wf_did_resolve_raw, not
     * through `agent`'s com.atproto.identity.resolveDid -- that XRPC call
     * is proxied by whichever PDS `service` happens to be, and at least one
     * major production PDS (bsky.social) gates it behind auth, which would
     * make this command only work for repos hosted on a PDS the caller can
     * log into. A DID resolves the same way no matter who is asking. */
    wf_xrpc_client *raw_client = wf_xrpc_client_new(service);
    if (!raw_client) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        free(did);
        wf_agent_free(agent);
        return 1;
    }
    char *doc_json = NULL;
    s = wf_did_resolve_raw(raw_client, did, &doc_json);
    wf_xrpc_client_free(raw_client);
    if (s != WF_OK) {
        fprintf(stderr, "error: resolving DID document failed (status %d)\n",
                (int)s);
        free(did);
        wf_agent_free(agent);
        return 1;
    }

    if (g_json) {
        printf("%s\n", doc_json);
        free(doc_json);
        free(did);
        wf_agent_free(agent);
        return 0;
    }

    cJSON *doc = cJSON_Parse(doc_json);
    free(doc_json);
    if (!doc) {
        fprintf(stderr, "error: could not parse DID document\n");
        free(did);
        wf_agent_free(agent);
        return 1;
    }

    printf("did: %s\n", did);
    print_handles(agent, cJSON_GetObjectItemCaseSensitive(doc, "alsoKnownAs"));
    print_verification_methods(
        cJSON_GetObjectItemCaseSensitive(doc, "verificationMethod"));
    print_services(cJSON_GetObjectItemCaseSensitive(doc, "service"));
    if (strncmp(did, "did:plc:", 8) == 0)
        print_plc_rotation_keys(did, plc_directory);

    cJSON_Delete(doc);
    free(did);
    wf_agent_free(agent);
    return 0;
}
