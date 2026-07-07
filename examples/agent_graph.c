/*
 * agent_graph.c — agent API social graph example.
 *
 * Demonstrates:
 *   1. Agent login with wf_agent
 *   2. Profile lookup with wf_agent_get_profile
 *   3. Relationship query with wf_agent_get_relationships
 *
 * Usage:
 *   agent_graph <service-url> <handle-or-email> <password> <target-did-or-handle>
 */

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/agent.h"

static void print_profile(const wf_agent_profile *p) {
    if (!p) return;
    printf("Profile: @%s", p->handle ? p->handle : "?");
    if (p->display_name && p->display_name[0])
        printf(" (%s)", p->display_name);
    printf("\n");
    if (p->description)
        printf("  Bio: %s\n", p->description);
    printf("  DID: %s\n", p->did);
    printf("  Posts: %d | Followers: %d | Following: %d\n",
           p->posts_count, p->followers_count, p->follows_count);
}

static void print_relationship(const wf_response *res) {
    if (!res || !res->body) return;
    cJSON *root = cJSON_ParseWithLength(res->body, res->body_len);
    if (!root) return;

    cJSON *rel = cJSON_GetObjectItemCaseSensitive(root, "relationships");
    if (cJSON_IsArray(rel)) {
        cJSON *item;
        cJSON_ArrayForEach(item, rel) {
            cJSON *did = cJSON_GetObjectItemCaseSensitive(item, "did");
            cJSON *following = cJSON_GetObjectItemCaseSensitive(item, "following");
            cJSON *followed_by = cJSON_GetObjectItemCaseSensitive(item, "followedBy");
            const char *d = did && cJSON_IsString(did) ? did->valuestring : "?";
            printf("  %s: following=%s followed_by=%s\n", d,
                   cJSON_IsString(following) ? "yes" : "no",
                   cJSON_IsString(followed_by) ? "yes" : "no");
        }
    }
    cJSON_Delete(root);
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <service-url> <handle-or-email> <password> <target-did-or-handle>\n", argv[0]);
        return 1;
    }

    const char *service_url = argv[1];
    const char *identifier  = argv[2];
    const char *password    = argv[3];
    const char *target      = argv[4];

    wf_agent *agent = wf_agent_new(service_url);
    if (!agent) { fprintf(stderr, "failed to create agent\n"); return 1; }

    wf_status status = wf_agent_login(agent, identifier, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_agent_free(agent);
        return 1;
    }

    wf_session_data sd = {0};
    wf_agent_get_session_data(agent, &sd);
    printf("Logged in as %s (%s)\n\n", sd.handle, sd.did);

    /* Target must be a DID (resolve with describe_server or identity API) */
    const char *target_did = target;

    /* Get the target's profile */
    {
        wf_agent_profile prof = {0};
        status = wf_agent_get_profile(agent, target_did, &prof);
        if (status == WF_OK) {
            print_profile(&prof);
        } else {
            printf("get_profile failed: %d\n", (int)status);
        }
        wf_agent_profile_free(&prof);
    }

    /* Get the relationship between logged-in user and target */
    {
        wf_response res = {0};
        const char *actors[2] = {sd.did, target_did};
        status = wf_agent_get_relationships(agent, sd.did, actors, 2, &res);
        if (status == WF_OK || status == WF_ERR_HTTP) {
            printf("\nRelationships:\n");
            print_relationship(&res);
        } else {
            printf("\nget_relationships failed: %d\n", (int)status);
        }
        wf_response_free(&res);
    }

    wf_agent_session_data_free(&sd);
    wf_agent_free(agent);
    return 0;
}
