/*
 * pagination.c — cursor-based pagination convenience layer for the agent API.
 *
 * Provides a generic raw-response iterator plus typed paged wrappers for the
 * most-used read endpoints. Cursor extraction prefers a typed `cursor` field
 * when a typed parser is available, and otherwise falls back to extracting the
 * top-level "cursor" field from the raw JSON response via wf_response_cursor.
 *
 * Ownership rules (see agent.h for the full contract):
 *   - out_last_cursor (when requested) is a heap copy the caller frees.
 *   - responses/lists handed to on_page are borrows, freed by the iterator.
 */

#include "wolfram/agent.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

wf_status wf_response_cursor(const wf_response *resp, char **out_cursor) {
    if (!out_cursor) {
        return WF_ERR_INVALID_ARG;
    }
    *out_cursor = NULL;
    if (!resp) {
        return WF_ERR_INVALID_ARG;
    }
    if (!resp->body) {
        return WF_OK;
    }

    cJSON *root = cJSON_ParseWithLength(resp->body, resp->body_len);
    if (!root) {
        return WF_ERR_PARSE;
    }

    char *result = NULL;
    cJSON *cursor = cJSON_GetObjectItemCaseSensitive(root, "cursor");
    if (cJSON_IsString(cursor) && cursor->valuestring) {
        size_t len = strlen(cursor->valuestring);
        result = (char *)malloc(len + 1);
        if (result) {
            memcpy(result, cursor->valuestring, len + 1);
        } else {
            cJSON_Delete(root);
            return WF_ERR_ALLOC;
        }
    }

    cJSON_Delete(root);
    *out_cursor = result;
    return WF_OK;
}

wf_status wf_agent_page(wf_agent *agent,
                        wf_agent_page_call_fn call,
                        int limit, int max_pages,
                        wf_agent_page_cb on_page, void *ud,
                        char **out_last_cursor) {
    if (out_last_cursor) {
        *out_last_cursor = NULL;
    }
    if (!agent || !call || !on_page) {
        return WF_ERR_INVALID_ARG;
    }

    char *cursor = NULL;
    int page = 0;
    wf_status status = WF_OK;

    /* `call` has the signature (agent, limit, cursor, out, ud); adapt it to
     * the (agent, cursor, out, ud) shape expected by wf_agent_page_loop. */
    for (;;) {
        if (max_pages > 0 && page >= max_pages) {
            break;
        }
        wf_response resp = {0};
        status = call(agent, limit, cursor, &resp, ud);
        if (status != WF_OK) {
            wf_response_free(&resp);
            free(cursor);
            return status;
        }

        char *next_cursor = NULL;
        status = wf_response_cursor(&resp, &next_cursor);
        if (status != WF_OK) {
            wf_response_free(&resp);
            free(cursor);
            free(next_cursor);
            return status;
        }

        status = on_page(agent, cursor, &resp, ud);
        wf_response_free(&resp);

        free(cursor);
        cursor = NULL;

        if (status != WF_OK) {
            free(next_cursor);
            return status;
        }
        if (!next_cursor) {
            if (out_last_cursor) {
                *out_last_cursor = NULL;
            }
            return WF_OK;
        }
        cursor = next_cursor;
        page++;
    }

    if (out_last_cursor) {
        *out_last_cursor = cursor;
    } else {
        free(cursor);
    }
    cursor = NULL;
    return WF_OK;
}

/* Typed paged wrappers --------------------------------------------------- */

wf_status wf_agent_get_timeline_paged(wf_agent *agent, int limit, int max_pages,
                                      wf_agent_timeline_page_cb on_page,
                                      void *ud, char **out_last_cursor) {
    if (out_last_cursor) {
        *out_last_cursor = NULL;
    }
    if (!agent || !on_page) {
        return WF_ERR_INVALID_ARG;
    }

    char *cursor = NULL;
    int page = 0;
    wf_status status = WF_OK;

    for (;;) {
        if (max_pages > 0 && page >= max_pages) {
            break;
        }
        wf_agent_feed_list feed = {0};
        /* limit<=0 defers to the endpoint default; the typed wrapper requires
         * limit>=0, passing 0 is accepted by the underlying read. */
        status = wf_agent_get_timeline_typed(agent, limit, cursor, &feed);
        if (status != WF_OK) {
            wf_agent_feed_list_free(&feed);
            free(cursor);
            return status;
        }

        char *next_cursor = feed.cursor;
        feed.cursor = NULL; /* transfer ownership out of the list */

        status = on_page(agent, cursor, &feed, ud);
        wf_agent_feed_list_free(&feed);

        free(cursor);
        cursor = NULL;

        if (status != WF_OK) {
            free(next_cursor);
            return status;
        }
        if (!next_cursor) {
            if (out_last_cursor) {
                *out_last_cursor = NULL;
            }
            return WF_OK;
        }
        cursor = next_cursor;
        page++;
    }

    if (out_last_cursor) {
        *out_last_cursor = cursor;
    } else {
        free(cursor);
    }
    cursor = NULL;
    return WF_OK;
}

wf_status wf_agent_get_author_feed_paged(wf_agent *agent, const char *actor,
                                         int limit, int max_pages,
                                         wf_agent_author_feed_page_cb on_page,
                                         void *ud, char **out_last_cursor) {
    if (out_last_cursor) {
        *out_last_cursor = NULL;
    }
    if (!agent || !actor || !on_page) {
        return WF_ERR_INVALID_ARG;
    }

    char *cursor = NULL;
    int page = 0;
    wf_status status = WF_OK;

    for (;;) {
        if (max_pages > 0 && page >= max_pages) {
            break;
        }
        wf_agent_feed_list feed = {0};
        status = wf_agent_get_author_feed_typed(agent, actor, limit, cursor,
                                                NULL, &feed);
        if (status != WF_OK) {
            wf_agent_feed_list_free(&feed);
            free(cursor);
            return status;
        }

        char *next_cursor = feed.cursor;
        feed.cursor = NULL;

        status = on_page(agent, cursor, &feed, ud);
        wf_agent_feed_list_free(&feed);

        free(cursor);
        cursor = NULL;

        if (status != WF_OK) {
            free(next_cursor);
            return status;
        }
        if (!next_cursor) {
            if (out_last_cursor) {
                *out_last_cursor = NULL;
            }
            return WF_OK;
        }
        cursor = next_cursor;
        page++;
    }

    if (out_last_cursor) {
        *out_last_cursor = cursor;
    } else {
        free(cursor);
    }
    cursor = NULL;
    return WF_OK;
}

wf_status wf_agent_list_notifications_paged(wf_agent *agent, int limit,
                                            int max_pages,
                                            wf_agent_notifications_page_cb on_page,
                                            void *ud, char **out_last_cursor) {
    if (out_last_cursor) {
        *out_last_cursor = NULL;
    }
    if (!agent || !on_page) {
        return WF_ERR_INVALID_ARG;
    }

    char *cursor = NULL;
    int page = 0;
    wf_status status = WF_OK;

    for (;;) {
        if (max_pages > 0 && page >= max_pages) {
            break;
        }
        wf_agent_notification_list list = {0};
        status = wf_agent_list_notifications_typed(agent, limit, cursor, &list);
        if (status != WF_OK) {
            wf_agent_notification_list_free(&list);
            free(cursor);
            return status;
        }

        char *next_cursor = list.cursor;
        list.cursor = NULL;

        status = on_page(agent, cursor, &list, ud);
        wf_agent_notification_list_free(&list);

        free(cursor);
        cursor = NULL;

        if (status != WF_OK) {
            free(next_cursor);
            return status;
        }
        if (!next_cursor) {
            if (out_last_cursor) {
                *out_last_cursor = NULL;
            }
            return WF_OK;
        }
        cursor = next_cursor;
        page++;
    }

    if (out_last_cursor) {
        *out_last_cursor = cursor;
    } else {
        free(cursor);
    }
    cursor = NULL;
    return WF_OK;
}

wf_status wf_agent_list_records_paged(wf_agent *agent, const char *collection,
                                      int limit, int max_pages,
                                      wf_agent_records_page_cb on_page,
                                      void *ud, char **out_last_cursor) {
    if (out_last_cursor) {
        *out_last_cursor = NULL;
    }
    if (!agent || !collection || !on_page) {
        return WF_ERR_INVALID_ARG;
    }

    char *cursor = NULL;
    int page = 0;
    wf_status status = WF_OK;

    for (;;) {
        if (max_pages > 0 && page >= max_pages) {
            break;
        }
        wf_response resp = {0};
        status = wf_agent_list_records(agent, collection, limit, cursor, &resp);
        if (status != WF_OK) {
            wf_response_free(&resp);
            free(cursor);
            return status;
        }

        char *next_cursor = NULL;
        status = wf_response_cursor(&resp, &next_cursor);
        if (status != WF_OK) {
            wf_response_free(&resp);
            free(cursor);
            free(next_cursor);
            return status;
        }

        status = on_page(agent, cursor, &resp, ud);
        wf_response_free(&resp);

        free(cursor);
        cursor = NULL;

        if (status != WF_OK) {
            free(next_cursor);
            return status;
        }
        if (!next_cursor) {
            if (out_last_cursor) {
                *out_last_cursor = NULL;
            }
            return WF_OK;
        }
        cursor = next_cursor;
        page++;
    }

    if (out_last_cursor) {
        *out_last_cursor = cursor;
    } else {
        free(cursor);
    }
    cursor = NULL;
    return WF_OK;
}
