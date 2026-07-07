/*
 * timeline_moderation.c — apply the moderation decision engine to posts.
 *
 * Two modes:
 *   - Offline (no args): build a couple of sample posts with labels entirely
 *     in code and run them through wf_mod_decide_post, printing the computed
 *     blur/alert/filter decisions. No network is used.
 *   - Online (<service-url> <handle> <password>): log in, fetch the user's
 *     timeline, parse each post, and run the same decision engine over the
 *     real labels returned by the server.
 *
 * Usage:
 *   timeline_moderation                          # offline sample, exit 0
 *   timeline_moderation <service-url> <handle> <password>   # real timeline
 */

#include <cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wolfram/agent.h"
#include "wolfram/moderation.h"

/* ------------------------------------------------------------------ */
/* Shared decision printing                                             */
/* ------------------------------------------------------------------ */

static void print_decision(const char *title, const wf_mod_decision *dec) {
    wf_mod_ui ui;
    if (wf_mod_decision_ui(dec, WF_MOD_CTX_CONTENT_VIEW, &ui) != WF_OK) {
        printf("  %s: (failed to compute UI)\n", title);
        return;
    }
    printf("  %s\n", title);
    printf("    blocked : %s\n", wf_mod_decision_blocked(dec) ? "yes" : "no");
    printf("    muted   : %s\n", wf_mod_decision_muted(dec) ? "yes" : "no");
    printf("    blurs   : %zu\n", ui.blur_count);
    printf("    alerts  : %zu\n", ui.alert_count);
    printf("    informs : %zu\n", ui.inform_count);
    printf("    filters : %zu\n", ui.filter_count);
    wf_mod_ui_free(&ui);
}

/* ------------------------------------------------------------------ */
/* Label definitions and preferences (shared by both modes)             */
/* ------------------------------------------------------------------ */

static void populate_label_defs(wf_mod_label_def **out_defs,
                                size_t *out_count) {
    const struct {
        const char *id;
        const char *blurs;
        const char *severity;
        int adult;
        const char *def;
    } spec[] = {
        {"porn",    "content", "alert", 1, "hide"},
        {"sexual",  "content", "alert", 1, "hide"},
        {"gore",    "content", "alert", 1, "hide"},
        {"spam",    "none",    "alert", 0, "warn"},
        {" nudity", "media",   "inform", 0, "warn"},
    };

    size_t n = sizeof(spec) / sizeof(spec[0]);
    wf_mod_label_def *defs = malloc(n * sizeof(wf_mod_label_def));
    if (!defs) {
        *out_defs = NULL;
        *out_count = 0;
        return;
    }

    for (size_t i = 0; i < n; ++i) {
        wf_mod_interpret_label_def(&defs[i], spec[i].id, "did:plc:labeler",
                                   spec[i].blurs, spec[i].severity,
                                   spec[i].adult, spec[i].def);
    }
    *out_defs = defs;
    *out_count = n;
}

static void free_label_defs(wf_mod_label_def *defs, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        wf_mod_label_def_free(&defs[i]);
    }
    free(defs);
}

static wf_mod_opts build_opts(const char *me, wf_mod_label_def *defs,
                              size_t def_count) {
    wf_mod_prefs prefs;
    memset(&prefs, 0, sizeof(prefs));
    prefs.adult_content_enabled = 0; /* adult content OFF -> hide */

    prefs.labelers = malloc(sizeof(wf_mod_labeler_pref));
    prefs.labelers[0].did = strdup("did:plc:labeler");
    prefs.labelers[0].label_prefs = NULL;
    prefs.labelers[0].label_identifiers = NULL;
    prefs.labelers[0].label_count = 0;
    prefs.labeler_count = 1;

    wf_mod_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.user_did = me;
    opts.prefs = prefs;
    opts.label_defs = defs;
    opts.label_def_count = def_count;
    return opts;
}

static void free_opts(wf_mod_opts *opts) {
    if (opts->prefs.labelers) {
        free(opts->prefs.labelers[0].did);
        free(opts->prefs.labelers);
    }
}

/* ------------------------------------------------------------------ */
/* Offline sample path                                                  */
/* ------------------------------------------------------------------ */

static int run_offline_sample(void) {
    const char *me = "did:plc:me";
    const char *labeler = "did:plc:labeler";
    const char *author = "did:plc:author";

    wf_mod_label_def *defs = NULL;
    size_t def_count = 0;
    populate_label_defs(&defs, &def_count);
    if (!defs) {
        fprintf(stderr, "failed to allocate label defs\n");
        return 1;
    }

    wf_mod_opts opts = build_opts(me, defs, def_count);

    /* Sample post 1: clean post. */
    wf_mod_subject_post p1;
    memset(&p1, 0, sizeof(p1));
    p1.uri = "at://did:plc:author/app.bsky.feed.post/aaa";
    p1.author.did = author;
    p1.text = "Just setting up my bluesky!";
    wf_mod_decision d1;
    if (wf_mod_decision_init(&d1) != WF_OK ||
        wf_mod_decide_post(&d1, &p1, &opts) != WF_OK) {
        fprintf(stderr, "decide_post (clean) failed\n");
        free_opts(&opts);
        free_label_defs(defs, def_count);
        return 1;
    }
    print_decision("post 1 (clean):", &d1);
    wf_mod_decision_free(&d1);

    /* Sample post 2: porn-labeled content. */
    wf_mod_label labels2[1];
    memset(&labels2[0], 0, sizeof(labels2[0]));
    labels2[0].src = (char *)labeler;
    labels2[0].uri = (char *)"at://did:plc:author/app.bsky.feed.post/bbb";
    labels2[0].val = (char *)"porn";

    wf_mod_subject_post p2;
    memset(&p2, 0, sizeof(p2));
    p2.uri = "at://did:plc:author/app.bsky.feed.post/bbb";
    p2.author.did = author;
    p2.text = "sensitive content";
    p2.labels = labels2;
    p2.label_count = 1;
    wf_mod_decision d2;
    if (wf_mod_decision_init(&d2) != WF_OK ||
        wf_mod_decide_post(&d2, &p2, &opts) != WF_OK) {
        fprintf(stderr, "decide_post (porn) failed\n");
        free_opts(&opts);
        free_label_defs(defs, def_count);
        return 1;
    }
    print_decision("post 2 (porn label):", &d2);
    wf_mod_decision_free(&d2);

    /* Sample post 3: spam-labeled and from a blocked-by account. */
    wf_mod_label labels3[1];
    memset(&labels3[0], 0, sizeof(labels3[0]));
    labels3[0].src = (char *)labeler;
    labels3[0].uri = (char *)"at://did:plc:author/app.bsky.feed.post/ccc";
    labels3[0].val = (char *)"spam";

    wf_mod_subject_post p3;
    memset(&p3, 0, sizeof(p3));
    p3.uri = "at://did:plc:author/app.bsky.feed.post/ccc";
    p3.author.did = author;
    p3.author.viewer.blocked_by = "1";
    p3.text = "buy now!!! limited offer";
    p3.labels = labels3;
    p3.label_count = 1;
    wf_mod_decision d3;
    if (wf_mod_decision_init(&d3) != WF_OK ||
        wf_mod_decide_post(&d3, &p3, &opts) != WF_OK) {
        fprintf(stderr, "decide_post (spam) failed\n");
        free_opts(&opts);
        free_label_defs(defs, def_count);
        return 1;
    }
    print_decision("post 3 (spam + blocked-by):", &d3);
    wf_mod_decision_free(&d3);

    free_opts(&opts);
    free_label_defs(defs, def_count);
    printf("timeline_moderation: offline sample OK\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Online timeline path                                                */
/* ------------------------------------------------------------------ */

static const char *cjson_str(cJSON *obj, const char *key) {
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int run_online(const char *service_url, const char *handle,
                      const char *password) {
    wf_agent *agent = wf_agent_new(service_url);
    if (!agent) {
        fprintf(stderr, "failed to create agent\n");
        return 1;
    }

    wf_status status = wf_agent_login(agent, handle, password);
    if (status != WF_OK) {
        fprintf(stderr, "login failed: %d\n", (int)status);
        wf_agent_free(agent);
        return 1;
    }

    const char *me = wf_agent_get_did(agent);
    printf("Logged in as %s (%s)\n", wf_agent_get_handle(agent), me);

    wf_mod_label_def *defs = NULL;
    size_t def_count = 0;
    populate_label_defs(&defs, &def_count);
    if (!defs) {
        fprintf(stderr, "failed to allocate label defs\n");
        wf_agent_free(agent);
        return 1;
    }
    wf_mod_opts opts = build_opts(me, defs, def_count);

    wf_response res = {0};
    status = wf_agent_get_timeline_lex(agent, 25, NULL, &res);
    if (status != WF_OK) {
        fprintf(stderr, "getTimeline failed: %d\n", (int)status);
        free_opts(&opts);
        free_label_defs(defs, def_count);
        wf_response_free(&res);
        wf_agent_free(agent);
        return 1;
    }

    size_t moderated = 0;
    if (res.body && res.body_len) {
        cJSON *root = cJSON_ParseWithLength(res.body, res.body_len);
        if (root) {
            cJSON *feed = cJSON_GetObjectItemCaseSensitive(root, "feed");
            if (cJSON_IsArray(feed)) {
                for (int i = 0; i < cJSON_GetArraySize(feed); ++i) {
                    cJSON *item = cJSON_GetArrayItem(feed, i);
                    cJSON *post = cJSON_GetObjectItemCaseSensitive(item, "post");
                    if (!cJSON_IsObject(post)) {
                        continue;
                    }

                    wf_mod_subject_post sp;
                    memset(&sp, 0, sizeof(sp));
                    sp.uri = cjson_str(post, "uri");
                    sp.cid = cjson_str(post, "cid");

                    cJSON *author =
                        cJSON_GetObjectItemCaseSensitive(post, "author");
                    if (cJSON_IsObject(author)) {
                        sp.author.did = cjson_str(author, "did");
                        sp.author.handle = cjson_str(author, "handle");
                    }
                    cJSON *record =
                        cJSON_GetObjectItemCaseSensitive(post, "record");
                    if (cJSON_IsObject(record)) {
                        sp.text = cjson_str(record, "text");
                    }

                    /* Parse the labels array into wf_mod_label. */
                    cJSON *labels =
                        cJSON_GetObjectItemCaseSensitive(post, "labels");
                    if (cJSON_IsArray(labels)) {
                        int n = cJSON_GetArraySize(labels);
                        wf_mod_label *lbls = calloc((size_t)n, sizeof(wf_mod_label));
                        int got = 0;
                        for (int j = 0; j < n; ++j) {
                            cJSON *l = cJSON_GetArrayItem(labels, j);
                            const char *val = cjson_str(l, "val");
                            if (!val) {
                                continue;
                            }
                            lbls[got].src = (char *)cjson_str(l, "src");
                            lbls[got].uri = (char *)cjson_str(l, "uri");
                            lbls[got].val = (char *)val;
                            lbls[got].cts = (char *)cjson_str(l, "cts");
                            ++got;
                        }
                        sp.labels = lbls;
                        sp.label_count = (size_t)got;

                        wf_mod_decision dec;
                        if (wf_mod_decision_init(&dec) == WF_OK &&
                            wf_mod_decide_post(&dec, &sp, &opts) == WF_OK) {
                            char title[128];
                            snprintf(title, sizeof(title), "timeline[%d] %s",
                                     i, sp.uri ? sp.uri : "?");
                            print_decision(title, &dec);
                            ++moderated;
                            wf_mod_decision_free(&dec);
                        }
                        free(lbls);
                    } else {
                        wf_mod_decision dec;
                        if (wf_mod_decision_init(&dec) == WF_OK &&
                            wf_mod_decide_post(&dec, &sp, &opts) == WF_OK) {
                            char title[128];
                            snprintf(title, sizeof(title), "timeline[%d] %s",
                                     i, sp.uri ? sp.uri : "?");
                            print_decision(title, &dec);
                            ++moderated;
                            wf_mod_decision_free(&dec);
                        }
                    }
                }
            }
            cJSON_Delete(root);
        }
    }

    printf("timeline_moderation: moderated %zu posts\n", moderated);
    free_opts(&opts);
    free_label_defs(defs, def_count);
    wf_response_free(&res);
    wf_agent_free(agent);
    return 0;
}

static void print_usage(const char *prog) {
    printf("usage:\n");
    printf("  %s                                   # offline sample\n", prog);
    printf("  %s <service-url> <handle> <password> # moderate real timeline\n",
           prog);
    printf("  (no arguments: run offline sample, exit 0)\n");
}

int main(int argc, char **argv) {
    if (argc == 4) {
        return run_online(argv[1], argv[2], argv[3]);
    }

    if (argc == 1) {
        return run_offline_sample();
    }

    /* Anything else: explain usage and exit 0 (offline safe). */
    print_usage(argv[0]);
    return 0;
}
