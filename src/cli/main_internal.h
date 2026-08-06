#ifndef WOLFRAM_CLI_MAIN_INTERNAL_H
#define WOLFRAM_CLI_MAIN_INTERNAL_H

/* Cross-cutting helpers `cmd_*` subcommand handlers share, split out of
 * main.c so a subcommand group split into its own file (e.g. cli_oauth.c)
 * can still reach them. Not part of any installed API -- this is the CLI
 * demo program, not the SDK. */

#include "wolfram/agent.h"

#include <stdio.h>

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

#endif /* WOLFRAM_CLI_MAIN_INTERNAL_H */
