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
 *   wolfram   resolve   <service> <handle-or-did>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>

#include "wolfram/agent.h"
#include "wolfram/identity.h"
#include "wolfram/syntax.h"
#include "wolfram/xrpc.h"
#include "wolfram/thread_typed.h"
#include "wolfram/moderation.h"

/* ----------------------------------------------------------------- */
/* Usage                                                             */
/* ----------------------------------------------------------------- */

static void usage_stream(FILE *out) {
    fprintf(out,
        "wolfram — a command-line client for the AT Protocol (via wolfram SDK)\n\n"
        "usage: wolfram <command> [args...]\n\n"
        "commands:\n"
        "  login         <service> <handle> <password>\n"
        "  post          <service> <handle> <password> <text...>\n"
        "  timeline      <service> <handle> <password> [pages]\n"
        "  get-post      <service> <at-uri>\n"
        "  profile       <service> <actor>\n"
        "  follow        <service> <handle> <password> <actor>\n"
        "  unfollow      <service> <handle> <password> <actor>\n"
        "  resolve       <service> <handle-or-did>\n"
        "  thread        <service> <handle> <password> <at-uri> [depth]\n"
        "  notifications <service> <handle> <password> [limit]\n"
        "  labels        <actor-or-did>\n"
        "  moderation    <service> <actor> [labeler-did]\n");
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
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle_or_did = argv[2];

    if (wf_syntax_did_is_valid(handle_or_did)) {
        printf("%s\n", handle_or_did);
        return 0;
    }

    wf_xrpc_client *client = wf_xrpc_client_new(service);
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

/* Recursively print a thread node and its replies (indented by depth). */
static void print_thread_node(const wf_agent_thread_node *node, int depth) {
    if (!node) {
        return;
    }
    for (int i = 0; i < depth; ++i) {
        fputc(' ', stdout);
        fputc(' ', stdout);
    }

    if (node->kind == WF_AGENT_THREAD_KIND_POST) {
        const char *handle = node->post.author.handle
                                 ? node->post.author.handle
                                 : (node->post.author.did
                                        ? node->post.author.did
                                        : "?");
        const char *did = node->post.author.did
                              ? node->post.author.did
                              : "?";
        const char *text = "";
        if (node->post.record) {
            cJSON *t =
                cJSON_GetObjectItemCaseSensitive(node->post.record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("[%s] %s: %s\n", did, handle, text);
    } else {
        printf("(not found/blocked: %s)\n",
               node->uri ? node->uri : "?");
    }

    for (size_t i = 0; i < node->replies_count; ++i) {
        print_thread_node(&node->replies[i], depth + 1);
    }
}

static int cmd_thread(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];
    int depth = (argc >= 6) ? atoi(argv[5]) : 6;

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

    wf_agent_thread thread = {0};
    s = wf_agent_get_post_thread_typed(agent, at_uri, depth, &thread);
    if (s != WF_OK) {
        fprintf(stderr, "error: getPostThread failed (status %d)\n", (int)s);
        wf_agent_thread_free(&thread);
        wf_agent_free(agent);
        return 1;
    }

    print_thread_node(&thread.root, 0);
    wf_agent_thread_free(&thread);
    wf_agent_free(agent);
    return 0;
}

static int cmd_notifications(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

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

    wf_agent_notification_list list = {0};
    s = wf_agent_list_notifications_typed(agent, limit, NULL, &list);
    if (s != WF_OK) {
        fprintf(stderr, "error: listNotifications failed (status %d)\n",
                (int)s);
        wf_agent_notification_list_free(&list);
        wf_agent_free(agent);
        return 1;
    }

    for (size_t i = 0; i < list.notification_count; ++i) {
        const wf_agent_notification *n = &list.notifications[i];
        const char *author =
            n->author.handle
                ? n->author.handle
                : (n->author.did ? n->author.did : "?");
        const char *did = n->author.did ? n->author.did : "?";
        const char *text = "";
        if (n->record) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(n->record, "text");
            if (cJSON_IsString(t) && t->valuestring) {
                text = t->valuestring;
            }
        }
        printf("reason=%s author=%s (%s) read=%d\n  %s\n",
               n->reason ? n->reason : "?", author, did, n->is_read, text);
    }
    if (list.notification_count == 0) {
        printf("(no notifications)\n");
    }

    wf_agent_notification_list_free(&list);
    wf_agent_free(agent);
    return 0;
}

static int cmd_labels(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *actor = argv[1];

    char *did = NULL;
    if (wf_syntax_did_is_valid(actor)) {
        did = strdup(actor);
        if (!did) {
            fprintf(stderr, "error: out of memory\n");
            return 1;
        }
    } else {
        wf_xrpc_client *client = wf_xrpc_client_new("https://bsky.social");
        if (!client) {
            fprintf(stderr, "error: failed to create XRPC client\n");
            return 1;
        }
        wf_status s = wf_handle_resolve(client, actor, &did);
        wf_xrpc_client_free(client);
        if (s != WF_OK || !did) {
            fprintf(stderr, "error: could not resolve '%s' (status %d)\n",
                    actor, (int)s);
            return 1;
        }
    }

    /* No high-level queryLabels wrapper exists; query the labeler service
     * directly via raw XRPC using the actor's DID as the URI prefix. */
    wf_xrpc_client *client = wf_xrpc_client_new("https://bsky.social");
    if (!client) {
        fprintf(stderr, "error: failed to create XRPC client\n");
        free(did);
        return 1;
    }

    char *uri_pattern = malloc(strlen(did) + 8);
    if (!uri_pattern) {
        fprintf(stderr, "error: out of memory\n");
        free(did);
        wf_xrpc_client_free(client);
        return 1;
    }
    sprintf(uri_pattern, "at://%s/*", did);

    wf_xrpc_param params[] = {{"uriPatterns", uri_pattern}};
    wf_response res = {0};
    wf_status s = wf_xrpc_query_params(client, "com.atproto.label.queryLabels",
                                       params, 1, &res);
    free(uri_pattern);
    free(did);

    if (s != WF_OK && s != WF_ERR_HTTP) {
        fprintf(stderr, "error: queryLabels failed (status %d)\n", (int)s);
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

static int cmd_moderation(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    /* argv[3] (labeler-did) is accepted but not required by the wrapper. */

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    char *did = NULL;
    wf_status rs = resolve_actor_to_did(agent, actor, &did);
    if (rs != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)rs);
        wf_agent_free(agent);
        return 1;
    }

    wf_mod_decision *decision = NULL;
    wf_status s = wf_agent_moderate_profile(agent, did, &decision);
    free(did);
    if (s != WF_OK || !decision) {
        fprintf(stderr, "error: moderation failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_mod_ui ui = {0};
    s = wf_mod_decision_ui(decision, WF_MOD_CTX_PROFILE_VIEW, &ui);
    if (s != WF_OK) {
        fprintf(stderr, "error: decision UI failed (status %d)\n", (int)s);
        wf_mod_decision_free(decision);
        wf_agent_free(agent);
        return 1;
    }

    printf("moderation for %s: alerts=%zu blurs=%zu informs=%zu\n",
           decision->did ? decision->did : actor, ui.alert_count,
           ui.blur_count, ui.inform_count);

    wf_mod_ui_free(&ui);
    wf_mod_decision_free(decision);
    wf_agent_free(agent);
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

    /* --help / -h / help print usage to stdout and exit 0 — without touching
     * the network. Genuinely unknown commands still print to stderr below. */
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 ||
        strcmp(cmd, "help") == 0) {
        return usage_exit();
    }

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
    if (strcmp(cmd, "thread") == 0) {
        return cmd_thread(rest, cargv);
    }
    if (strcmp(cmd, "notifications") == 0) {
        return cmd_notifications(rest, cargv);
    }
    if (strcmp(cmd, "labels") == 0) {
        return cmd_labels(rest, cargv);
    }
    if (strcmp(cmd, "moderation") == 0) {
        return cmd_moderation(rest, cargv);
    }

    fprintf(stderr, "error: unknown command '%s'\n\n", cmd);
    usage_stream(stderr);
    return 0;
}
