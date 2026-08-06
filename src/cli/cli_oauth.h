#ifndef WOLFRAM_CLI_OAUTH_H
#define WOLFRAM_CLI_OAUTH_H

/* `oauth-login` / `oauth-callback` subcommand handlers, dispatched from
 * main.c's argv switch. Not part of any installed API -- this is the CLI
 * demo program, not the SDK. */

/* wolfram oauth-login <service> <handle> [client-id] [redirect-uri]
 * [--state-file <path>] */
int cmd_oauth_login(int argc, char **argv);

/* wolfram oauth-callback <service> --url <redirect> --state <state>
 * [--state-file <path>] [--client-id <id>] [--redirect-uri <uri>]
 * [--session <path>] */
int cmd_oauth_callback(int argc, char **argv);

#endif /* WOLFRAM_CLI_OAUTH_H */
