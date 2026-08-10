#ifndef WOLFRAM_CLI_FEED_H
#define WOLFRAM_CLI_FEED_H

/* `feed` / `get-likes` / `get-reposted-by` / `get-quotes` /
 * `get-actor-likes` / `get-actor-feeds` / `describe-feed` /
 * `get-suggested-feeds` / `get-suggestions` / `known-followers` /
 * `relationships` / `get-profiles` / `unread-count` / `get-actor-status` /
 * `get-feed-generators` / `get-suggested-follows-by-actor` /
 * `age-assurance` subcommand handlers, dispatched from main.c's argv switch.
 * Not part of any installed API -- this is the CLI demo program, not the SDK. */

int cmd_feed(int argc, char **argv);
int cmd_get_likes(int argc, char **argv);
int cmd_get_reposted_by(int argc, char **argv);
int cmd_get_quotes(int argc, char **argv);
int cmd_get_actor_likes(int argc, char **argv);
int cmd_get_actor_feeds(int argc, char **argv);
int cmd_describe_feed(int argc, char **argv);
int cmd_get_suggested_feeds(int argc, char **argv);
int cmd_get_suggestions(int argc, char **argv);
int cmd_known_followers(int argc, char **argv);
int cmd_relationships(int argc, char **argv);
int cmd_get_profiles(int argc, char **argv);
int cmd_unread_count(int argc, char **argv);
int cmd_get_actor_status(int argc, char **argv);
int cmd_get_feed_generators(int argc, char **argv);
int cmd_get_suggested_follows_by_actor(int argc, char **argv);
int cmd_age_assurance(int argc, char **argv);

#endif /* WOLFRAM_CLI_FEED_H */
