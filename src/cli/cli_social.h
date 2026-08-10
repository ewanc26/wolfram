#ifndef WOLFRAM_CLI_SOCIAL_H
#define WOLFRAM_CLI_SOCIAL_H

/* `profile` / `follow` / `unfollow` / `like` / `unlike` / `repost` /
 * `delete-repost` / `mute` / `unmute` / `block` / `unblock` / `resolve`
 * subcommand handlers, dispatched from main.c's argv switch. Not part of
 * any installed API -- this is the CLI demo program, not the SDK. */

/* wolfram profile <service> <actor> */
int cmd_profile(int argc, char **argv);

/* wolfram follow <service> <handle> <password> <actor> */
int cmd_follow(int argc, char **argv);

/* wolfram unfollow <service> <handle> <password> <actor> */
int cmd_unfollow(int argc, char **argv);

/* wolfram like <service> <handle> <password> <at-uri> */
int cmd_like(int argc, char **argv);

/* wolfram unlike <service> <handle> <password> <like-at-uri> */
int cmd_unlike(int argc, char **argv);

/* wolfram repost <service> <handle> <password> <at-uri> */
int cmd_repost(int argc, char **argv);

/* wolfram delete-repost <service> <handle> <password> <repost-at-uri> */
int cmd_delete_repost(int argc, char **argv);

/* wolfram mute <service> <handle> <password> <actor> */
int cmd_mute(int argc, char **argv);

/* wolfram unmute <service> <handle> <password> <actor> */
int cmd_unmute(int argc, char **argv);

/* wolfram block <service> <handle> <password> <actor> */
int cmd_block(int argc, char **argv);

/* wolfram unblock <service> <handle> <password> <actor> */
int cmd_unblock(int argc, char **argv);

/* wolfram resolve <service> <handle-or-did> */
int cmd_resolve(int argc, char **argv);

#endif /* WOLFRAM_CLI_SOCIAL_H */
