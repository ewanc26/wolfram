#ifndef WOLFRAM_CLI_POST_H
#define WOLFRAM_CLI_POST_H

/* `post` / `reply` / `delete` / `timeline` / `get-post` subcommand handlers,
 * dispatched from main.c's argv switch. Not part of any installed API -- this
 * is the CLI demo program, not the SDK. */

int cmd_post(int argc, char **argv);
int cmd_delete(int argc, char **argv);
int cmd_reply(int argc, char **argv);
int cmd_timeline(int argc, char **argv);
int cmd_get_post(int argc, char **argv);

#endif /* WOLFRAM_CLI_POST_H */
