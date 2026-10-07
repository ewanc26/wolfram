/* test_profile_tab.c -- names, filters and the cycle of profile tabs. */

#include "wolfram/profile_tab.h"

#include "test.h"

#include <string.h>

int main(void) {
    WF_CHECK(strcmp(wf_profile_tab_name(WF_PROFILE_TAB_POSTS), "Posts") == 0);
    WF_CHECK(strcmp(wf_profile_tab_name(WF_PROFILE_TAB_REPLIES), "Replies") ==
             0);
    WF_CHECK(strcmp(wf_profile_tab_name(WF_PROFILE_TAB_MEDIA), "Media") == 0);
    WF_CHECK(strcmp(wf_profile_tab_name(WF_PROFILE_TAB_LIKES), "Likes") == 0);
    WF_CHECK(strcmp(wf_profile_tab_name(99), "Posts") == 0);

    WF_CHECK(strcmp(wf_profile_tab_filter(WF_PROFILE_TAB_POSTS),
                    "posts_and_author_threads") == 0);
    WF_CHECK(strcmp(wf_profile_tab_filter(WF_PROFILE_TAB_REPLIES),
                    "posts_with_replies") == 0);
    WF_CHECK(strcmp(wf_profile_tab_filter(WF_PROFILE_TAB_MEDIA),
                    "posts_with_media") == 0);
    WF_CHECK(wf_profile_tab_filter(WF_PROFILE_TAB_LIKES) == NULL);
    WF_CHECK(strcmp(wf_profile_tab_filter(-1), "posts_and_author_threads") ==
             0);

    /* Someone else's profile has no Likes; your own does. */
    WF_CHECK(wf_profile_tab_next(WF_PROFILE_TAB_POSTS, 0) ==
             WF_PROFILE_TAB_REPLIES);
    WF_CHECK(wf_profile_tab_next(WF_PROFILE_TAB_MEDIA, 0) ==
             WF_PROFILE_TAB_POSTS);
    WF_CHECK(wf_profile_tab_next(WF_PROFILE_TAB_MEDIA, 1) ==
             WF_PROFILE_TAB_LIKES);
    WF_CHECK(wf_profile_tab_next(WF_PROFILE_TAB_LIKES, 1) ==
             WF_PROFILE_TAB_POSTS);
    WF_CHECK(wf_profile_tab_next(WF_PROFILE_TAB_LIKES, 0) ==
             WF_PROFILE_TAB_POSTS);
    WF_CHECK(wf_profile_tab_next(-3, 1) == WF_PROFILE_TAB_POSTS);

    WF_CHECK(wf_agent_get_profile_tab_typed(NULL, "a", 0, 10, NULL, NULL) ==
             WF_ERR_INVALID_ARG);
    WF_TEST_SUMMARY();
}
