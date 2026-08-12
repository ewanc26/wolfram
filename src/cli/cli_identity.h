#ifndef WOLFRAM_CLI_IDENTITY_H
#define WOLFRAM_CLI_IDENTITY_H

/* `resolve-did` / `check-handle` / `get-recommended-did-credentials` /
 * `rotate-handle` / `plc` subcommand handlers, dispatched from main.c's
 * argv switch. Not part of any installed API -- this is the CLI demo
 * program, not the SDK. */

/* wolfram resolve-did <service> <did> */
int cmd_resolve_did(int argc, char **argv);

/* wolfram check-handle <service> <handle> */
int cmd_check_handle(int argc, char **argv);

/* wolfram get-recommended-did-credentials <service> <handle> <password> */
int cmd_get_recommended_did_credentials(int argc, char **argv);

/* wolfram rotate-handle <service> <handle> <password> <new-handle>
 *   [--token <plc-token>] */
int cmd_rotate_handle(int argc, char **argv);

/* wolfram plc request-signature <service> <did>
 * wolfram plc sign <service> <token> [--rotation-key <key>]...
 *   [--also-known-as <handle>]...
 * wolfram plc submit <service> <operation-json>
 * wolfram plc log <did> [--plc-directory URL] [--json] -- full operation
 *   history, oldest first, with a diff of handles/rotation keys/PDS/signing
 *   key against the previous entry */
int cmd_plc(int argc, char **argv);

#endif /* WOLFRAM_CLI_IDENTITY_H */
