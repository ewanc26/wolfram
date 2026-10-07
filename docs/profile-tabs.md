# Profile tabs

`wolfram/profile_tab.h` has what a client needs for the tabs on a profile: `wf_profile_tab_name`, the `getAuthorFeed` filter for each (`wf_profile_tab_filter`: `posts_and_author_threads`, `posts_with_replies`, `posts_with_media`), the cycle order (`wf_profile_tab_next`, which skips Likes unless the profile is your own) and one call, `wf_agent_get_profile_tab_typed`, that fetches a page of whichever tab (Likes uses `getActorLikes`, which the server only answers for the viewer's own account). Tested in `test/test_profile_tab.c`; the fetch is a thin call over the author feed and likes functions, not separately run against a server.

The names, filters and cycle (`src/agent/profile_tab.c`) need no agent, so a client can link them alone; the fetch is `src/agent/profile_tab_fetch.c`.
