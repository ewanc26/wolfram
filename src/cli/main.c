/*
 * main.c — the `wolfram` command-line client.
 *
 * A thin demonstration over the wolfram SDK that exercises the high-level
 * agent API (and a little raw XRPC) end to end. Every subcommand is
 * network-gated: when its required arguments are missing the program prints
 * usage and exits 0 without performing any network I/O.
 *
 * Conventions follow the rest of the SDK: wf_ prefixes, wf_status returns,
 * and cJSON for JSON handling. All allocated resources (sessions, agents,
 * responses) are freed before exit.
 *
 * Usage:
 *   wolfram login       <service> <handle> <password>
 *   wolfram post        <service> <handle> <password> <text...>
 *   wolfram timeline    <service> <handle> <password> [pages]
 *   wolfram get-post    <service> <at-uri>
 *   wolfram profile     <service> <actor>
 *   wolfram follow      <service> <handle> <password> <actor>
 *   wolfram unfollow    <service> <handle> <password> <actor>
 *   wolfram resolve     <handle-or-did>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/agent.h"
#include "wolfram/identity.h"
#include "wolfram/syntax.h"
#include "wolfram/xrpc.h"

/* ----------------------------------------------------------------- */
/* Usage                                                             */
/* ----------------------------------------------------------------- */

static void usage_stream(FILE *out) {
    fprintf(out,
        "wolfram — a command-line client for the AT Protocol (via wolfram SDK)\n\n"
        "usage: wolfram <command> [args...]\n\n"
        "commands:\n"
        "  login     <service> <handle> <password>\n"
        "  post      <service> <handle> <password> <text...>\n"
        "  timeline  <service> <handle> <password> [pages]\n"
        "  get-post  <service> <at-uri>\n"
        "  profile   <service> <actor>\n"
        "  follow    <service> <handle> <password> <actor>\n"
        "  unfollow  <service> <handle> <password> <actor>\n"
        "  resolve   <handle-or-did>\n");
}

/* Print usage and exit 0 (offline-safe: never touches the network). */
static int usage_exit(void) {
    usage_stream(stdout);
    return 0;
}

/* ----------------------------------------------------------------- */
/* Small helpers                                                     */
/* ----------------------------------------------------------------- */

/* Join argv[first..argc-1] into a single heap string (caller frees). */
static char *join_args(int argc, char **argv, int first) {
    size_t total = 1;
    for (int i = first; i < argc; ++i) {
        size_t part = strlen(argv[i]);
        if (part > SIZE_MAX - total - 1) {
            return NULL;
        }
        total += part;
        if (i + 1 < argc) {
            ++total;
        }
    }

    char *out = malloc(total);
    if (!out) {
        return NULL;
    }

    char *dst = out;
    for (int i = first; i < argc; ++i) {
        size_t part = strlen(argv[i]);
        memcpy(dst, argv[i], part);
        dst += part;
        if (i + 1 < argc) {
            *dst++ = ' ';
        }
    }
    *dst = '\0';
    return out;
}

/* Resolve an actor argument (a handle or DID) to a heap-owned DID string.
 * When `actor` is already a valid DID, a copy is returned. Otherwise the
 * agent's handle-resolution endpoint is used. Caller frees *out_did. */
static wf_status resolve_actor_to_did(wf_agent *agent, const char *actor,
                                      char **out_did) {
    if (!agent || !actor || !out_did) {
        return WF_ERR_INVALID_ARG;
    }
    *out_did = NULL;

    if (wf_syntax_did_is_valid(actor)) {
        char *dup = strdup(actor);
        if (!dup) {
            return WF_ERR_ALLOC;
        }
        *out_did = dup;
        return WF_OK;
    }

    return wf_agent_resolve_handle(agent, actor, out_did);
}

/* ----------------------------------------------------------------- */
/* Subcommands                                                       */
/* ----------------------------------------------------------------- */

static int cmd_login(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_session_data sd = {0};
    wf_agent_get_session_data(agent, &sd);
    printf("logged in as %s (%s)\n", sd.handle ? sd.handle : "?",
           sd.did ? sd.did : "?");
    wf_agent_session_data_free(&sd);
    wf_agent_free(agent);
    return 0;
}

static int cmd_post(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];

    char *text = join_args(argc, argv, 4);
    if (!text) {
        fprintf(stderr, "error: failed to assemble post text\n");
        return 1;
    }

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        free(text);
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        free(text);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result result = {0};
    s = wf_agent_post(agent, text, &result);
    free(text);
    if (s != WF_OK) {
        fprintf(stderr, "error: post failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("%s\n", result.uri ? result.uri : "(no uri returned)");
    wf_agent_post_result_free(&result);
    wf_agent_free(agent);
    return 0;
}

typedef struct {
    int printed;
} timeline_ctx;

static wf_status timeline_on_page(wf_agent *agent,
                                  const wf_agent_feed_list *feed,
                                  const char *cursor, void *ud) {
    (void)agent;
    (void)cursor;
    timeline_ctx *ctx = (timeline_ctx *)ud;

    for (size_t i = 0; i < feed->item_count; ++i) {
        const wf_agent_post_view *post = &feed->items[i].post;
        const char *author = post->author.handle
                                 ? post->author.handle
                                 : (post->author.did ? post->author.did : "?");
        const char *text = "";
        if (post->record) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(post->record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("%s: %s\n", author, text);
        ctx->printed++;
    }
    return WF_OK;
}

static int cmd_timeline(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int max_pages = (argc >= 5) ? atoi(argv[4]) : 0; /* 0 = until exhausted */

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    timeline_ctx ctx = {0};
    char *last_cursor = NULL;
    s = wf_agent_get_timeline_paged(agent, 10, max_pages, timeline_on_page,
                                    &ctx, &last_cursor);
    if (s != WF_OK) {
        fprintf(stderr, "error: timeline failed (status %d)\n", (int)s);
        free(last_cursor);
        wf_agent_free(agent);
        return 1;
    }

    if (ctx.printed == 0) {
        printf("(timeline empty)\n");
    }
    free(last_cursor);
    wf_agent_free(agent);
    return 0;
}

static int cmd_get_post(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *uri = argv[2];

    wf_syntax_aturi parsed = {0};
    if (!wf_syntax_aturi_parse(uri, &parsed) || !parsed.authority ||
        !parsed.collection || !parsed.record_key) {
        fprintf(stderr, "error: invalid at-uri: %s\n", uri);
        wf_syntax_aturi_free(&parsed);
        return 1;
    }

    wf_xrpc_client *client = wf_xrpc_client_new(service);
    if (!client) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        wf_syntax_aturi_free(&parsed);
        return 1;
    }

    wf_xrpc_param params[] = {
        {"repo", parsed.authority},
        {"collection", parsed.collection},
        {"rkey", parsed.record_key},
    };

    wf_response res = {0};
    wf_status s = wf_xrpc_query_params(client, "com.atproto.repo.getRecord",
                                       params, 3, &res);
    wf_syntax_aturi_free(&parsed);

    if (s != WF_OK && s != WF_ERR_HTTP) {
        fprintf(stderr, "error: getRecord failed (status %d)\n", (int)s);
        wf_response_free(&res);
        wf_xrpc_client_free(client);
        return 1;
    }

    if (res.body && res.body_len > 0) {
        printf("%s\n", res.body);
    } else {
        printf("(empty response, HTTP %ld)\n", res.status);
    }

    wf_response_free(&res);
    wf_xrpc_client_free(client);
    return 0;
}

static int cmd_profile(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_agent_profile prof = {0};
    wf_status s = wf_agent_get_profile(agent, actor, &prof);
    if (s != WF_OK) {
        fprintf(stderr, "error: getProfile failed (status %d)\n", (int)s);
        wf_agent_profile_free(&prof);
        wf_agent_free(agent);
        return 1;
    }

    printf("%s\n", prof.display_name ? prof.display_name : "(no display name)");
    if (prof.description) {
        printf("%s\n", prof.description);
    }
    wf_agent_profile_free(&prof);
    wf_agent_free(agent);
    return 0;
}

static int cmd_follow(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    char *subject_did = NULL;
    s = resolve_actor_to_did(agent, actor, &subject_did);
    if (s != WF_OK || !subject_did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result result = {0};
    s = wf_agent_follow(agent, subject_did, &result);
    free(subject_did);
    if (s != WF_OK) {
        fprintf(stderr, "error: follow failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&result);
        wf_agent_free(agent);
        return 1;
    }

    printf("followed: %s\n", result.uri ? result.uri : "(no uri returned)");
    wf_agent_post_result_free(&result);
    wf_agent_free(agent);
    return 0;
}

static int cmd_unfollow(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_status s = wf_agent_login(agent, handle, password);
    if (s != WF_OK) {
        fprintf(stderr, "error: login failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    char *subject_did = NULL;
    s = resolve_actor_to_did(agent, actor, &subject_did);
    if (s != WF_OK || !subject_did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    /* Look up the existing follow URI via the viewer's relationship state. */
    wf_session_data sd = {0};
    wf_agent_get_session_data(agent, &sd);
    const char *others[1] = {subject_did};
    wf_response res = {0};
    s = wf_agent_get_relationships(agent, sd.did, others, 1, &res);
    wf_agent_session_data_free(&sd);
    free(subject_did);

    if (s != WF_OK && s != WF_ERR_HTTP) {
        fprintf(stderr, "error: getRelationships failed (status %d)\n", (int)s);
        wf_response_free(&res);
        wf_agent_free(agent);
        return 1;
    }

    char *follow_uri = NULL;
    if (res.body) {
        cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
        if (root) {
            cJSON *rels = cJSON_GetObjectItemCaseSensitive(root, "relationships");
            if (cJSON_IsArray(rels) && cJSON_GetArraySize(rels) > 0) {
                cJSON *rel = cJSON_GetArrayItem(rels, 0);
                cJSON *following =
                    cJSON_GetObjectItemCaseSensitive(rel, "following");
                if (cJSON_IsString(following) && following->valuestring) {
                    follow_uri = strdup(following->valuestring);
                }
            }
            cJSON_Delete(root);
        }
    }
    wf_response_free(&res);

    if (!follow_uri) {
        printf("(not currently following %s)\n", actor);
        wf_agent_free(agent);
        return 0;
    }

    s = wf_agent_unfollow(agent, follow_uri);
    free(follow_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: unfollow failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("unfollowed %s\n", actor);
    wf_agent_free(agent);
    return 0;
}

static int cmd_resolve(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *handle_or_did = argv[1];

    if (wf_syntax_did_is_valid(handle_or_did)) {
        printf("%s\n", handle_or_did);
        return 0;
    }

    wf_xrpc_client *client = wf_xrpc_client_new("https://bsky.social");
    if (!client) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        return 1;
    }

    char *did = NULL;
    wf_status s = wf_handle_resolve(client, handle_or_did, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve '%s' (status %d)\n",
                handle_or_did, (int)s);
        wf_xrpc_client_free(client);
        return 1;
    }

    printf("%s\n", did);
    free(did);
    wf_xrpc_client_free(client);
    return 0;
}

/* ----------------------------------------------------------------- */
/* Dispatch                                                          */
/* ----------------------------------------------------------------- */

int main(int argc, char **argv) {
    if (argc < 2) {
        return usage_exit();
    }

    const char *cmd = argv[1];
    int rest = argc - 1; /* arguments after the program name */

    /* Re-base argv so handlers see <service> as argv[1] etc. */
    char **cargv = argv + 1;

    if (strcmp(cmd, "login") == 0) {
        return cmd_login(rest, cargv);
    }
    if (strcmp(cmd, "post") == 0) {
        return cmd_post(rest, cargv);
    }
    if (strcmp(cmd, "timeline") == 0) {
        return cmd_timeline(rest, cargv);
    }
    if (strcmp(cmd, "get-post") == 0) {
        return cmd_get_post(rest, cargv);
    }
    if (strcmp(cmd, "profile") == 0) {
        return cmd_profile(rest, cargv);
    }
    if (strcmp(cmd, "follow") == 0) {
        return cmd_follow(rest, cargv);
    }
    if (strcmp(cmd, "unfollow") == 0) {
        return cmd_unfollow(rest, cargv);
    }
    if (strcmp(cmd, "resolve") == 0) {
        return cmd_resolve(rest, cargv);
    }

    fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
    usage_stream(stderr);
    return 0;
}
