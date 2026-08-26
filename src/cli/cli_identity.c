/*
 * cli_identity.c — the `resolve-did` / `check-handle` /
 * `get-recommended-did-credentials` / `rotate-handle` / `plc` subcommands,
 * split out of main.c as their own self-contained concern.
 */

#include "cli_identity.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/identity_typed.h"
#include "wolfram/plc.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_resolve_did(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *did = argv[2];

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_resolve_did(agent, did, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_check_handle(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];

    wf_agent *agent = cli_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_identity_check_handle_input input = {.handle = handle};
    wf_identity_check_handle_result result = {0};
    wf_status s = wf_agent_check_handle(agent, &input, &result);
    if (s != WF_OK) {
        fprintf(stderr, "error: checkHandle failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("handle %s: %s\n", handle,
           result.valid == 1 ? "valid" : "not available");
    wf_agent_free(agent);
    return 0;
}

int cmd_get_recommended_did_credentials(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_get_recommended_did_credentials(agent, &res);
    return finish_agent_response(agent, s, &res);
}

int cmd_rotate_handle(int argc, char **argv) {
    const char *new_handle = NULL, *token = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--token") == 0 && i + 1 < argc)
            token = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
        else if (!new_handle)
            new_handle = argv[i];
    }
    if (pi < 3 || !new_handle) {
        fprintf(stderr, "error: usage: wolfram rotate-handle <service> "
                        "<handle> <password> <new-handle> [--token "
                        "<plc-token>]\n");
        return 1;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    wf_status s;
    if (token) {
        s = wf_agent_request_plc_operation_signature_typed(
            agent, wf_agent_get_did(agent));
        if (s == WF_OK) {
            s = wf_agent_update_handle_typed(agent, new_handle);
        }
    } else {
        s = wf_agent_update_handle(agent, new_handle);
    }
    if (s != WF_OK) {
        fprintf(stderr, "error: rotateHandle failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("handle rotated to %s\n", new_handle);
    wf_agent_free(agent);
    return 0;
}

/* Compare two cJSON string arrays for exact, order-sensitive equality.
 * Two absent/non-array values count as unchanged. */
static bool plc_json_array_eq(const cJSON *a, const cJSON *b) {
    bool a_arr = cJSON_IsArray(a), b_arr = cJSON_IsArray(b);
    if (!a_arr && !b_arr) return true;
    if (a_arr != b_arr) return false;
    int na = cJSON_GetArraySize(a), nb = cJSON_GetArraySize(b);
    if (na != nb) return false;
    for (int i = 0; i < na; ++i) {
        cJSON *ea = cJSON_GetArrayItem(a, i);
        cJSON *eb = cJSON_GetArrayItem(b, i);
        if (!cJSON_IsString(ea) || !cJSON_IsString(eb)) return false;
        if (strcmp(ea->valuestring, eb->valuestring) != 0) return false;
    }
    return true;
}

/* Effective alsoKnownAs array for a PLC operation, always caller-owned
 * (cJSON_Delete when done). Normalizes the pre-2022 legacy genesis format
 * (`type: "create"`, a single `handle` string instead of an `alsoKnownAs`
 * array) to a one-element array so callers never need to special-case it --
 * plenty of real accounts still have a legacy op as entry [0] of their
 * audit log. */
static cJSON *plc_op_handles(const cJSON *op) {
    cJSON *out = cJSON_CreateArray();
    if (!out || !op) return out;
    cJSON *aka = cJSON_GetObjectItemCaseSensitive((cJSON *)op, "alsoKnownAs");
    if (cJSON_IsArray(aka)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, aka) {
            if (cJSON_IsString(item))
                cJSON_AddItemToArray(out,
                                     cJSON_CreateString(item->valuestring));
        }
        return out;
    }
    cJSON *legacy = cJSON_GetObjectItemCaseSensitive((cJSON *)op, "handle");
    if (cJSON_IsString(legacy)) {
        char buf[300];
        snprintf(buf, sizeof(buf), "at://%s", legacy->valuestring);
        cJSON_AddItemToArray(out, cJSON_CreateString(buf));
    }
    return out;
}

/* Effective rotationKeys array for a PLC operation, always caller-owned.
 * Normalizes the legacy genesis format's single `recoveryKey` string to a
 * one-element array, same rationale as plc_op_handles. */
static cJSON *plc_op_rotation_keys(const cJSON *op) {
    cJSON *out = cJSON_CreateArray();
    if (!out || !op) return out;
    cJSON *rk = cJSON_GetObjectItemCaseSensitive((cJSON *)op, "rotationKeys");
    if (cJSON_IsArray(rk)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, rk) {
            if (cJSON_IsString(item))
                cJSON_AddItemToArray(out,
                                     cJSON_CreateString(item->valuestring));
        }
        return out;
    }
    cJSON *legacy =
        cJSON_GetObjectItemCaseSensitive((cJSON *)op, "recoveryKey");
    if (cJSON_IsString(legacy))
        cJSON_AddItemToArray(out, cJSON_CreateString(legacy->valuestring));
    return out;
}

/* Effective PDS endpoint for a PLC operation: modern
 * services.atproto_pds.endpoint, falling back to the legacy `service`
 * string. Borrowed pointer into `op`, no ownership. */
static const char *plc_op_pds_endpoint(const cJSON *op) {
    if (!op) return NULL;
    cJSON *services = cJSON_GetObjectItemCaseSensitive((cJSON *)op, "services");
    cJSON *pds = services
                     ? cJSON_GetObjectItemCaseSensitive(services, "atproto_pds")
                     : NULL;
    cJSON *endpoint =
        pds ? cJSON_GetObjectItemCaseSensitive(pds, "endpoint") : NULL;
    if (cJSON_IsString(endpoint)) return endpoint->valuestring;
    cJSON *legacy = cJSON_GetObjectItemCaseSensitive((cJSON *)op, "service");
    return cJSON_IsString(legacy) ? legacy->valuestring : NULL;
}

/* Effective atproto signing key for a PLC operation: modern
 * verificationMethods.atproto, falling back to the legacy `signingKey`
 * string. Borrowed pointer into `op`, no ownership. */
static const char *plc_op_signing_key(const cJSON *op) {
    if (!op) return NULL;
    cJSON *vms =
        cJSON_GetObjectItemCaseSensitive((cJSON *)op, "verificationMethods");
    cJSON *atproto_vm =
        vms ? cJSON_GetObjectItemCaseSensitive(vms, "atproto") : NULL;
    if (cJSON_IsString(atproto_vm)) return atproto_vm->valuestring;
    cJSON *legacy = cJSON_GetObjectItemCaseSensitive((cJSON *)op, "signingKey");
    return cJSON_IsString(legacy) ? legacy->valuestring : NULL;
}

/* Print a cJSON string array as a single comma-joined line, or "(none)". */
static void plc_print_array_line(const cJSON *arr) {
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) {
        printf("(none)");
        return;
    }
    const cJSON *item = NULL;
    bool first = true;
    cJSON_ArrayForEach(item, arr) {
        if (!cJSON_IsString(item)) continue;
        printf("%s%s", first ? "" : ", ", item->valuestring);
        first = false;
    }
}

int cmd_plc(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "error: usage: wolfram plc <request-signature|sign|"
                        "submit> ...\n");
        return 1;
    }
    const char *sub = argv[1];

    if (strcmp(sub, "request-signature") == 0) {
        if (argc < 4) {
            fprintf(stderr, "error: usage: wolfram plc request-signature "
                            "<service> <did>\n");
            return 1;
        }
        const char *service = argv[2];
        const char *did = argv[3];

        wf_agent *agent = cli_agent_new(service);
        if (!agent) {
            fprintf(stderr, "error: failed to create agent\n");
            return 1;
        }
        wf_status s =
            wf_agent_request_plc_operation_signature_typed(agent, did);
        wf_agent_free(agent);
        if (s != WF_OK) {
            fprintf(stderr, "error: requestSignature failed (status %d)\n",
                    (int)s);
            return 1;
        }
        printf("PLC signature requested for %s\n", did);
        return 0;
    }

    if (strcmp(sub, "sign") == 0) {
        if (argc < 4) {
            fprintf(stderr,
                    "error: usage: wolfram plc sign <service> "
                    "<token> [--rotation-key <key>]... [--also-known-as "
                    "<handle>]...\n");
            return 1;
        }
        const char *service = argv[2];
        const char *token = argv[3];

        const char *rotation_keys[16];
        int n_rot = 0;
        const char *aka[16];
        int n_aka = 0;
        for (int i = 4; i < argc; ++i) {
            if (strcmp(argv[i], "--rotation-key") == 0 && i + 1 < argc &&
                n_rot < 16)
                rotation_keys[n_rot++] = argv[++i];
            else if (strcmp(argv[i], "--also-known-as") == 0 && i + 1 < argc &&
                     n_aka < 16)
                aka[n_aka++] = argv[++i];
        }

        wf_agent *agent = cli_agent_new(service);
        if (!agent) {
            fprintf(stderr, "error: failed to create agent\n");
            return 1;
        }

        wf_identity_signed_operation out = {0};
        wf_status s = wf_agent_sign_plc_operation_typed(
            agent, token, rotation_keys, n_rot, aka, n_aka, NULL, NULL, &out);
        wf_agent_free(agent);
        if (s != WF_OK) {
            fprintf(stderr, "error: signPlcOperation failed (status %d)\n",
                    (int)s);
            wf_identity_signed_operation_free(&out);
            return 1;
        }
        printf("signed operation: %s\n",
               out.operation_json ? out.operation_json : "(none)");
        wf_identity_signed_operation_free(&out);
        return 0;
    }

    if (strcmp(sub, "submit") == 0) {
        if (argc < 4) {
            fprintf(stderr, "error: usage: wolfram plc submit <service> "
                            "<operation-json>\n");
            return 1;
        }
        const char *service = argv[2];
        const char *op_json = argv[3];

        wf_agent *agent = cli_agent_new(service);
        if (!agent) {
            fprintf(stderr, "error: failed to create agent\n");
            return 1;
        }

        wf_status s = wf_agent_submit_plc_operation_typed(agent, op_json);
        wf_agent_free(agent);
        if (s != WF_OK) {
            fprintf(stderr, "error: submitPlcOperation failed (status %d)\n",
                    (int)s);
            return 1;
        }
        printf("PLC operation submitted\n");
        return 0;
    }

    if (strcmp(sub, "log") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: usage: wolfram plc log <did> "
                            "[--plc-directory URL] [--json]\n");
            return 1;
        }
        const char *did = argv[2];
        const char *plc_directory = "https://plc.directory";
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--plc-directory") == 0 && i + 1 < argc)
                plc_directory = argv[++i];
            else if (strcmp(argv[i], "--json") == 0)
                g_json = true;
        }

        wf_xrpc_client *client = wf_xrpc_client_new(plc_directory);
        if (!client) {
            fprintf(stderr, "error: failed to create XRPC client\n");
            return 1;
        }

        char *log_json = NULL;
        wf_status s =
            wf_plc_get_audit_log(client, plc_directory, did, &log_json);
        wf_xrpc_client_free(client);
        if (s != WF_OK) {
            fprintf(stderr, "error: fetching audit log failed (status %d)\n",
                    (int)s);
            return 1;
        }

        if (g_json) {
            printf("%s\n", log_json);
            free(log_json);
            return 0;
        }

        cJSON *log = cJSON_Parse(log_json);
        free(log_json);
        if (!cJSON_IsArray(log)) {
            fprintf(stderr, "error: could not parse audit log\n");
            cJSON_Delete(log);
            return 1;
        }

        cJSON *prev_op = NULL;
        int idx = 0;
        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, log) {
            cJSON *op = cJSON_GetObjectItemCaseSensitive(entry, "operation");
            cJSON *cid = cJSON_GetObjectItemCaseSensitive(entry, "cid");
            cJSON *created =
                cJSON_GetObjectItemCaseSensitive(entry, "createdAt");
            cJSON *nullified =
                cJSON_GetObjectItemCaseSensitive(entry, "nullified");
            cJSON *type =
                op ? cJSON_GetObjectItemCaseSensitive(op, "type") : NULL;

            printf("[%d] %s  cid=%s%s\n", idx,
                   cJSON_IsString(created) ? created->valuestring : "?",
                   cJSON_IsString(cid) ? cid->valuestring : "?",
                   cJSON_IsBool(nullified) && cJSON_IsTrue(nullified)
                       ? "  [NULLIFIED]"
                       : "");

            if (cJSON_IsString(type) &&
                strcmp(type->valuestring, "plc_tombstone") == 0) {
                printf("  tombstone (account deleted)\n");
                prev_op = op;
                idx++;
                continue;
            }

            cJSON *aka = plc_op_handles(op);
            cJSON *rk = plc_op_rotation_keys(op);
            const char *ep = plc_op_pds_endpoint(op);
            const char *vk = plc_op_signing_key(op);

            cJSON *prev_aka = plc_op_handles(prev_op);
            cJSON *prev_rk = plc_op_rotation_keys(prev_op);
            const char *prev_ep = plc_op_pds_endpoint(prev_op);
            const char *prev_vk = plc_op_signing_key(prev_op);

            if (!prev_op) {
                printf("  handles: ");
                plc_print_array_line(aka);
                printf("\n  rotation keys: ");
                plc_print_array_line(rk);
                printf("\n  pds: %s\n", ep ? ep : "(none)");
                printf("  signing key: %s\n", vk ? vk : "(none)");
            } else {
                bool any_change = false;
                if (!plc_json_array_eq(aka, prev_aka)) {
                    printf("  handles: ");
                    plc_print_array_line(prev_aka);
                    printf(" -> ");
                    plc_print_array_line(aka);
                    printf("\n");
                    any_change = true;
                }
                if (!plc_json_array_eq(rk, prev_rk)) {
                    printf("  rotation keys: ");
                    plc_print_array_line(prev_rk);
                    printf(" -> ");
                    plc_print_array_line(rk);
                    printf("\n");
                    any_change = true;
                }
                if ((ep && !prev_ep) || (!ep && prev_ep) ||
                    (ep && prev_ep && strcmp(ep, prev_ep) != 0)) {
                    printf("  pds: %s -> %s\n", prev_ep ? prev_ep : "(none)",
                           ep ? ep : "(none)");
                    any_change = true;
                }
                if ((vk && !prev_vk) || (!vk && prev_vk) ||
                    (vk && prev_vk && strcmp(vk, prev_vk) != 0)) {
                    printf("  signing key: %s -> %s\n",
                           prev_vk ? prev_vk : "(none)", vk ? vk : "(none)");
                    any_change = true;
                }
                if (!any_change) printf("  (no tracked fields changed)\n");
            }

            cJSON_Delete(aka);
            cJSON_Delete(rk);
            cJSON_Delete(prev_aka);
            cJSON_Delete(prev_rk);
            prev_op = op;
            idx++;
        }
        cJSON_Delete(log);
        return 0;
    }

    fprintf(stderr,
            "error: unknown plc subcommand '%s' (try "
            "request-signature/sign/submit/log)\n",
            sub);
    return 1;
}
