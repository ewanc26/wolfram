/*
 * cli_feed.c — the `feed` / `get-likes` / `get-reposted-by` / `get-quotes` /
 * `get-actor-likes` / `get-actor-feeds` / `describe-feed` /
 * `get-suggested-feeds` / `get-suggestions` / `known-followers` /
 * `relationships` / `get-profiles` / `unread-count` subcommands, split out
 * of main.c as their own self-contained concern.
 */

#include "cli_feed.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/feed_typed.h"
#include "wolfram/feedgen_typed.h"
#include "wolfram/unspecced_typed.h"
#include "wolfram/graph_social_typed.h"
#include "wolfram/notification_typed.h"
#include "wolfram/actor_status_typed.h"
#include "wolfram/ageassurance_typed.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_feed(int argc, char **argv) {
    if (argc < 2) {
        usage_stream(stderr);
        return 0;
    }
    const char *sub = argv[1];

    /* wolfram feed get <service> <handle> <password> --feed <generator-uri>
     *   [--limit N] [--cursor C] */
    if (strcmp(sub, "get") == 0) {
        const char *feed = NULL, *cursor = NULL;
        int limit = 50;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--feed") == 0 && i + 1 < argc)
                feed = argv[++i];
            else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
                cursor = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !feed) {
            fprintf(
                stderr,
                "error: usage: wolfram feed get <service> <handle> "
                "<password> --feed <generator-uri> [--limit N] [--cursor C]\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;
        if (g_json) {
            wf_response res = {0};
            wf_status s = wf_agent_get_feed(agent, feed, limit, cursor, &res);
            return finish_agent_response(agent, s, &res);
        }
        wf_agent_feed_view_list list = {0};
        wf_status s =
            wf_agent_get_feed_typed(agent, feed, limit, cursor, &list);
        if (s != WF_OK) {
            fprintf(stderr, "error: getFeed failed (status %d)\n", (int)s);
            wf_agent_feed_view_list_free(&list);
            wf_agent_free(agent);
            return 1;
        }
        for (size_t i = 0; i < list.item_count; ++i) {
            const wf_agent_post_view *post = &list.items[i].post;
            const char *author =
                post->author.handle
                    ? post->author.handle
                    : (post->author.did ? post->author.did : "?");
            const char *text = "";
            if (post->record) {
                cJSON *t =
                    cJSON_GetObjectItemCaseSensitive(post->record, "text");
                if (cJSON_IsString(t) && t->valuestring) text = t->valuestring;
            }
            printf("%s: %s\n", author, text);
        }
        if (list.cursor) printf("cursor: %s\n", list.cursor);
        if (list.item_count == 0) printf("(empty feed)\n");
        wf_agent_feed_view_list_free(&list);
        wf_agent_free(agent);
        return 0;
    }

    /* wolfram feed author <service> <handle> <password> --actor <handle-or-did>
     *   [--limit N] [--cursor C] */
    if (strcmp(sub, "author") == 0) {
        const char *actor = NULL, *cursor = NULL;
        int limit = 50;
        const char *pos[3];
        int pi = 0;
        for (int i = 2; i < argc; ++i) {
            if (strcmp(argv[i], "--actor") == 0 && i + 1 < argc)
                actor = argv[++i];
            else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
                limit = atoi(argv[++i]);
            else if (strcmp(argv[i], "--cursor") == 0 && i + 1 < argc)
                cursor = argv[++i];
            else if (pi < 3)
                pos[pi++] = argv[i];
        }
        if (pi < 3 || !actor) {
            fprintf(stderr,
                    "error: usage: wolfram feed author <service> <handle> "
                    "<password> --actor <handle-or-did> [--limit N] [--cursor "
                    "C]\n");
            return 1;
        }
        wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
        if (!agent) return 1;
        if (g_json) {
            wf_response res = {0};
            wf_status s = wf_agent_get_author_feed(agent, actor, limit, cursor,
                                                   NULL, false, &res);
            return finish_agent_response(agent, s, &res);
        }
        wf_agent_feed_list list = {0};
        wf_status s = wf_agent_get_author_feed_typed(agent, actor, limit,
                                                     cursor, NULL, &list);
        if (s != WF_OK) {
            fprintf(stderr, "error: getAuthorFeed failed (status %d)\n",
                    (int)s);
            wf_agent_feed_list_free(&list);
            wf_agent_free(agent);
            return 1;
        }
        for (size_t i = 0; i < list.item_count; ++i) {
            const wf_agent_post_view *post = &list.items[i].post;
            const char *author =
                post->author.handle
                    ? post->author.handle
                    : (post->author.did ? post->author.did : "?");
            const char *text = "";
            if (post->record) {
                cJSON *t =
                    cJSON_GetObjectItemCaseSensitive(post->record, "text");
                if (cJSON_IsString(t) && t->valuestring) text = t->valuestring;
            }
            printf("%s: %s\n", author, text);
        }
        if (list.cursor) printf("cursor: %s\n", list.cursor);
        if (list.item_count == 0) printf("(empty feed)\n");
        wf_agent_feed_list_free(&list);
        wf_agent_free(agent);
        return 0;
    }

    fprintf(stderr, "error: unknown feed subcommand '%s' (try get/author)\n",
            sub);
    return 1;
}

/* wolfram get-likes <service> <handle> <password> <at-uri> [limit] */
int cmd_get_likes(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];
    int limit = (argc >= 6) ? atoi(argv[5]) : 50;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_get_likes(agent, at_uri, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram get-reposted-by <service> <handle> <password> <at-uri> [limit] */
int cmd_get_reposted_by(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];
    int limit = (argc >= 6) ? atoi(argv[5]) : 50;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_get_reposted_by(agent, at_uri, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram get-quotes <service> <handle> <password> <at-uri> [limit] */
int cmd_get_quotes(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];
    int limit = (argc >= 6) ? atoi(argv[5]) : 50;

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_get_quotes(agent, at_uri, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram get-actor-likes <service> <actor> [limit] */
int cmd_get_actor_likes(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_actor_likes(agent, actor, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram get-actor-feeds <service> <actor> [limit] */
int cmd_get_actor_feeds(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_actor_feeds(agent, actor, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram describe-feed <service> <handle> <password> [--feed <generator-uri>]
 */
int cmd_describe_feed(int argc, char **argv) {
    const char *feed = NULL;
    const char *pos[3];
    int pi = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--feed") == 0 && i + 1 < argc)
            feed = argv[++i];
        else if (pi < 3)
            pos[pi++] = argv[i];
    }
    if (pi < 3) {
        usage_stream(stderr);
        return 0;
    }

    wf_agent *agent = agent_login_or_err(pos[0], pos[1], pos[2]);
    if (!agent) return 1;

    if (feed) {
        wf_response res = {0};
        wf_status s = wf_agent_get_feed_generator(agent, feed, &res);
        return finish_agent_response(agent, s, &res);
    }

    wf_response res = {0};
    wf_status s = wf_agent_describe_feed_generator(agent, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram get-suggested-feeds <service> <handle> <password> */
int cmd_get_suggested_feeds(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_get_suggested_feeds(agent, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram get-suggestions <service> <handle> <password> [limit] */
int cmd_get_suggestions(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    int limit = (argc >= 5) ? atoi(argv[4]) : 50;

    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    wf_response res = {0};
    wf_status s = wf_agent_get_suggestions(agent, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram known-followers <service> <actor> [limit] */
int cmd_known_followers(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    int limit = (argc >= 4) ? atoi(argv[3]) : 50;

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_known_followers(agent, actor, limit, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram relationships <service> <actor> <other-did>... */
int cmd_relationships(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *actor = argv[2];
    /* argv[3..] are the other DIDs */
    int n_others = argc - 3;
    const char **others = (const char **)(argv + 3);

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s =
        wf_agent_get_relationships(agent, actor, others, n_others, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram get-profiles <service> <actor>... */
int cmd_get_profiles(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    int n_actors = argc - 2;
    const char **actors = (const char **)(argv + 2);

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_profiles(agent, actors, n_actors, 0, NULL, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram unread-count <service> <handle> <password> */
int cmd_unread_count(int argc, char **argv) {
    if (argc < 4) {
        usage_stream(stderr);
        return 0;
    }
    wf_agent *agent = agent_login_or_err(argv[1], argv[2], argv[3]);
    if (!agent) return 1;

    int count = 0;
    wf_status s = wf_agent_get_unread_count_typed(agent, &count);
    if (s != WF_OK) {
        fprintf(stderr, "error: getUnreadCount failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("%d\n", count);
    wf_agent_free(agent);
    return 0;
}

/* wolfram get-actor-status <service> <actor> */
int cmd_get_actor_status(int argc, char **argv) {
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

    wf_actor_status_view status = {0};
    wf_status s = wf_agent_get_actor_status(agent, actor, &status);
    if (s != WF_OK) {
        fprintf(stderr, "error: getActorStatus failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("actor: %s\n", actor);
    printf("status: %s\n", status.status ? status.status : "(none)");
    printf("createdAt: %s\n", status.created_at ? status.created_at : "(none)");
    if (status.has_duration_minutes)
        printf("durationMinutes: %lld\n", (long long)status.duration_minutes);
    printf("isActive: %d\n", status.has_is_active ? status.is_active : 0);
    printf("isDisabled: %d\n", status.has_is_disabled ? status.is_disabled : 0);
    printf("expiresAt: %s\n", status.expires_at ? status.expires_at : "(none)");
    printf("uri: %s\n", status.uri ? status.uri : "(none)");
    printf("cid: %s\n", status.cid ? status.cid : "(none)");

    wf_actor_status_view_free(&status);
    wf_agent_free(agent);
    return 0;
}

/* wolfram get-feed-generators <service> <feed-uri>... */
int cmd_get_feed_generators(int argc, char **argv) {
    if (argc < 3) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    int n_feeds = argc - 2;
    const char **feeds = (const char **)(argv + 2);

    wf_agent *agent = wf_agent_new(service);
    if (!agent) {
        fprintf(stderr, "error: failed to create agent\n");
        return 1;
    }

    wf_response res = {0};
    wf_status s = wf_agent_get_feed_generators(agent, feeds, n_feeds, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram get-suggested-follows-by-actor <service> <actor> */
int cmd_get_suggested_follows_by_actor(int argc, char **argv) {
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

    wf_response res = {0};
    wf_status s = wf_agent_get_suggested_follows_by_actor(agent, actor, &res);
    return finish_agent_response(agent, s, &res);
}

/* wolfram age-assurance <service> <handle> <password>
 * <begin|get-config|get-state> */
int cmd_age_assurance(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *sub = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    if (strcmp(sub, "begin") == 0) {
        wf_ageassurance_begin out = {0};
        wf_status s = wf_agent_begin_ageassurance(agent, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: ageAssurance.begin failed (status %d)\n",
                    (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("status: %s\n", out.status ? out.status : "(none)");
        printf("access: %s\n", out.access ? out.access : "(none)");
        printf("lastInitiatedAt: %s\n",
               out.last_initiated_at ? out.last_initiated_at : "(none)");
        wf_ageassurance_begin_free(&out);
    } else if (strcmp(sub, "get-config") == 0) {
        wf_ageassurance_config out = {0};
        wf_status s = wf_agent_get_ageassurance_config(agent, &out);
        if (s != WF_OK) {
            fprintf(stderr,
                    "error: ageAssurance.getConfig failed (status %d)\n",
                    (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("required: %s\n",
               out.regions && cJSON_IsTrue(out.regions) ? "true" : "false");
        if (out.regions && cJSON_IsArray(out.regions)) {
            int n = cJSON_GetArraySize(out.regions);
            printf("regions (%d):\n", n);
            for (int i = 0; i < n; ++i) {
                cJSON *r = cJSON_GetArrayItem(out.regions, i);
                printf("  %s\n", cJSON_IsObject(r) ? "(object)" : "(unknown)");
            }
        }
        wf_ageassurance_config_free(&out);
    } else if (strcmp(sub, "get-state") == 0) {
        wf_ageassurance_state out = {0};
        wf_status s = wf_agent_get_ageassurance_state(agent, &out);
        if (s != WF_OK) {
            fprintf(stderr, "error: ageAssurance.getState failed (status %d)\n",
                    (int)s);
            wf_agent_free(agent);
            return 1;
        }
        printf("status: %s\n", out.state.status ? out.state.status : "(none)");
        printf("access: %s\n", out.state.access ? out.state.access : "(none)");
        printf("lastInitiatedAt: %s\n", out.state.last_initiated_at
                                            ? out.state.last_initiated_at
                                            : "(none)");
        printf("accountCreatedAt: %s\n", out.metadata.account_created_at
                                             ? out.metadata.account_created_at
                                             : "(none)");
        wf_ageassurance_state_free(&out);
    } else {
        fprintf(stderr,
                "error: unknown age-assurance subcommand '%s' (try begin, "
                "get-config, get-state)\n",
                sub);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_free(agent);
    return 0;
}
