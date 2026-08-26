#ifndef WOLFRAM_CLI_MAIN_INTERNAL_H
#define WOLFRAM_CLI_MAIN_INTERNAL_H

/* Cross-cutting helpers `cmd_*` subcommand handlers share, split out of
 * main.c so a subcommand group split into its own file (e.g. cli_oauth.c)
 * can still reach them. Not part of any installed API -- this is the CLI
 * demo program, not the SDK. */

#include "wolfram/agent.h"

#include <stdio.h>
#include <stdbool.h>

/* Print the full `wolfram <command> ...` usage summary to `out`. */
void usage_stream(FILE *out);

/* Print usage to stdout and return the CLI's "printed usage, no error"
 * exit code (0). */
int usage_exit(void);

/* Resolve `actor` (a handle or DID) to its DID via
 * com.atproto.identity.resolveHandle when it is not already a DID.
 * Heap-allocated; caller frees *out_did. */
wf_status resolve_actor_to_did(wf_agent *agent, const char *actor,
                               char **out_did);

/* Resolve an at:// post URI to its record CID via getPostThread. Heap
 * -allocated; caller frees *out_cid. */
wf_status resolve_post_cid(wf_agent *agent, const char *at_uri, char **out_cid);

/* Resolve the CID and thread-root URI/CID needed to build a reply's
 * `reply.parent`/`reply.root` refs. Heap-allocated; caller frees each
 * out param. */
wf_status resolve_post_for_reply(wf_agent *agent, const char *at_uri,
                                 char **out_cid, char **out_root_uri,
                                 char **out_root_cid);

/* Common tail for a `cmd_*` handler that just made one agent call and wants
 * to print `res` (or an error) and return the process's exit code. */
int finish_agent_response(wf_agent *agent, wf_status s, wf_response *res);

/* Read the entire text file `path` into a heap string (NUL terminated).
 * Returns NULL on failure. Caller frees. */
char *read_text_file(const char *path);

/* Global --json flag: when set, list/get commands print the raw JSON body
 * instead of human-readable text. */
extern bool g_json;

/* Create an agent, wiring the Wii U DRBG into curl TLS when built for
 * Wii U. Thin wrapper around wf_agent_new. Caller frees with wf_agent_free. */
wf_agent *cli_agent_new(const char *service);

/* Create an agent, login, and return it. Returns NULL on failure (error
 * already printed to stderr). Caller frees with wf_agent_free. */
wf_agent *agent_login_or_err(const char *service, const char *handle,
                             const char *password);

/* Print an agent-level failure to stderr, appending the server's XRPC error
 * message when one was captured. Returns the failure exit code. */
int cli_agent_error(const char *what, wf_status s, wf_agent *agent);

/* Format the current UTC time as an RFC 3339 timestamp (e.g. for record
 * createdAt fields). Writes at most `len` bytes into `buf`. */
void now_rfc3339(char *buf, size_t len);

/* Join argv[first..argc-1] into a single heap string (caller frees). */
char *join_args(int argc, char **argv, int first);

#endif /* WOLFRAM_CLI_MAIN_INTERNAL_H */
