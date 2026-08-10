#ifndef WOLFRAM_CLI_AUTH_H
#define WOLFRAM_CLI_AUTH_H

/* `login` / `whoami` / `describe-server` subcommand handlers, dispatched from
 * main.c's argv switch. Not part of any installed API -- this is the CLI
 * demo program, not the SDK. */

int cmd_login(int argc, char **argv);
int cmd_whoami(int argc, char **argv);
int cmd_describe_server(int argc, char **argv);

#endif /* WOLFRAM_CLI_AUTH_H */
