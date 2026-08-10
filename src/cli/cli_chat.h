#ifndef WOLFRAM_CLI_CHAT_H
#define WOLFRAM_CLI_CHAT_H

/* `convos` / `messages` / `chat` / `groups` / `reactions` subcommand
 * handlers, dispatched from main.c's argv switch. Not part of any installed
 * API -- this is the CLI demo program, not the SDK. */

int cmd_convos(int argc, char **argv);
int cmd_messages(int argc, char **argv);
int cmd_chat(int argc, char **argv);
int cmd_groups(int argc, char **argv);
int cmd_reactions(int argc, char **argv);

#endif /* WOLFRAM_CLI_CHAT_H */
