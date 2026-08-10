/*
 * cli_social.c — the `profile` / `follow` / `unfollow` / `like` / `unlike` /
 * `repost` / `delete-repost` / `mute` / `unmute` / `block` / `unblock` /
 * `resolve` subcommands, split out of main.c as their own self-contained
 * concern.
 */

#include "cli_social.h"
#include "main_internal.h"

#include "wolfram/agent.h"
#include "wolfram/graph_typed.h"
#include "wolfram/graph_social_typed.h"
#include "wolfram/graph_write.h"
#include "wolfram/identity_typed.h"
#include "wolfram/label.h"
#include "wolfram/syntax.h"

#include <cJSON.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cmd_profile(int argc, char **argv) {
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

    if (g_json) {
        cJSON *j = cJSON_CreateObject();
        if (j) {
            if (prof.did) cJSON_AddStringToObject(j, "did", prof.did);
            if (prof.handle) cJSON_AddStringToObject(j, "handle", prof.handle);
            if (prof.display_name)
                cJSON_AddStringToObject(j, "displayName", prof.display_name);
            if (prof.description)
                cJSON_AddStringToObject(j, "description", prof.description);
            if (prof.avatar_cid)
                cJSON_AddStringToObject(j, "avatar", prof.avatar_cid);
            cJSON_AddNumberToObject(j, "followersCount", prof.followers_count);
            cJSON_AddNumberToObject(j, "followsCount", prof.follows_count);
            cJSON_AddNumberToObject(j, "postsCount", prof.posts_count);
            char *out = cJSON_PrintUnformatted(j);
            cJSON_Delete(j);
            if (out) {
                printf("%s\n", out);
                free(out);
            }
        }
        wf_agent_profile_free(&prof);
        wf_agent_free(agent);
        return 0;
    }

    printf("%s\n", prof.display_name ? prof.display_name : "(no display name)");
    if (prof.description) {
        printf("%s\n", prof.description);
    }
    wf_agent_profile_free(&prof);
    wf_agent_free(agent);
    return 0;
}

int cmd_follow(int argc, char **argv) {
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

int cmd_unfollow(int argc, char **argv) {
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
            cJSON *rels =
                cJSON_GetObjectItemCaseSensitive(root, "relationships");
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

int cmd_like(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    char *cid = NULL;
    wf_status s = resolve_post_cid(agent, at_uri, &cid);
    if (s != WF_OK || !cid) {
        fprintf(stderr, "error: could not resolve CID for %s (status %d)\n",
                at_uri, (int)s);
        free(cid);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result out = {0};
    s = wf_agent_like(agent, at_uri, cid, &out);
    free(cid);
    if (s != WF_OK) {
        fprintf(stderr, "error: like failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }

    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

int cmd_unlike(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *like_uri = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_status s = wf_agent_unlike(agent, like_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: unlike failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("unliked %s\n", like_uri);
    wf_agent_free(agent);
    return 0;
}

int cmd_repost(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *at_uri = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    char *cid = NULL;
    wf_status s = resolve_post_cid(agent, at_uri, &cid);
    if (s != WF_OK || !cid) {
        fprintf(stderr, "error: could not resolve CID for %s (status %d)\n",
                at_uri, (int)s);
        free(cid);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result out = {0};
    s = wf_agent_repost(agent, at_uri, cid, &out);
    free(cid);
    if (s != WF_OK) {
        fprintf(stderr, "error: repost failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }

    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

int cmd_delete_repost(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *repost_uri = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    wf_status s = wf_agent_delete_repost(agent, repost_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: delete-repost failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("deleted repost %s\n", repost_uri);
    wf_agent_free(agent);
    return 0;
}

int cmd_mute(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    s = wf_agent_mute(agent, did);
    free(did);
    if (s != WF_OK) {
        fprintf(stderr, "error: mute failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("muted %s\n", actor);
    wf_agent_free(agent);
    return 0;
}

int cmd_unmute(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) {
        return 1;
    }

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    s = wf_agent_unmute(agent, did);
    free(did);
    if (s != WF_OK) {
        fprintf(stderr, "error: unmute failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }

    printf("unmuted %s\n", actor);
    wf_agent_free(agent);
    return 0;
}

int cmd_block(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) return 1;

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_agent_post_result out = {0};
    s = wf_agent_block(agent, did, &out);
    free(did);
    if (s != WF_OK) {
        fprintf(stderr, "error: block failed (status %d)\n", (int)s);
        wf_agent_post_result_free(&out);
        wf_agent_free(agent);
        return 1;
    }
    printf("%s\n", out.uri ? out.uri : "(no uri returned)");
    wf_agent_post_result_free(&out);
    wf_agent_free(agent);
    return 0;
}

int cmd_unblock(int argc, char **argv) {
    if (argc < 5) {
        usage_stream(stderr);
        return 0;
    }
    const char *service = argv[1];
    const char *handle = argv[2];
    const char *password = argv[3];
    const char *actor = argv[4];

    wf_agent *agent = agent_login_or_err(service, handle, password);
    if (!agent) return 1;

    char *did = NULL;
    wf_status s = resolve_actor_to_did(agent, actor, &did);
    if (s != WF_OK || !did) {
        fprintf(stderr, "error: could not resolve actor '%s' (status %d)\n",
                actor, (int)s);
        wf_agent_free(agent);
        return 1;
    }

    wf_response res = {0};
    s = wf_agent_get_blocks(agent, 100, NULL, &res);
    if (s != WF_OK && s != WF_ERR_HTTP) {
        fprintf(stderr, "error: getBlocks failed (status %d)\n", (int)s);
        wf_response_free(&res);
        free(did);
        wf_agent_free(agent);
        return 1;
    }

    char *block_uri = NULL;
    if (res.body) {
        cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
        if (root) {
            cJSON *blocks = cJSON_GetObjectItemCaseSensitive(root, "blocks");
            if (cJSON_IsArray(blocks)) {
                for (int i = 0; i < cJSON_GetArraySize(blocks); ++i) {
                    cJSON *b = cJSON_GetArrayItem(blocks, i);
                    cJSON *subj =
                        cJSON_GetObjectItemCaseSensitive(b, "subject");
                    cJSON *sdid =
                        subj ? cJSON_GetObjectItemCaseSensitive(subj, "did")
                             : NULL;
                    cJSON *uri = cJSON_GetObjectItemCaseSensitive(b, "uri");
                    if (cJSON_IsString(sdid) && cJSON_IsString(uri) &&
                        strcmp(sdid->valuestring, did) == 0) {
                        block_uri = strdup(uri->valuestring);
                        break;
                    }
                }
            }
            cJSON_Delete(root);
        }
    }
    wf_response_free(&res);
    free(did);

    if (!block_uri) {
        printf("(not currently blocking %s)\n", actor);
        wf_agent_free(agent);
        return 0;
    }

    s = wf_agent_unblock(agent, block_uri);
    free(block_uri);
    if (s != WF_OK) {
        fprintf(stderr, "error: unblock failed (status %d)\n", (int)s);
        wf_agent_free(agent);
        return 1;
    }
    printf("unblocked %s\n", actor);
    wf_agent_free(agent);
    return 0;
}

int cmd_resolve(int argc, char **argv) {
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
