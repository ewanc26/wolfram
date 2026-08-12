#ifndef WOLFRAM_CLI_EXPLORE_H
#define WOLFRAM_CLI_EXPLORE_H

/* `browse` / `identity` subcommand handlers -- an at:// repo explorer and a
 * DID/handle inspector, the CLI equivalent of what a tool like PDSls
 * (pdsls.dev) gives you in a browser. Read-only and anonymous: no login is
 * needed to browse a public repo or inspect a public identity, matching the
 * underlying XRPC endpoints (describeRepo/listRecords/getRecord/resolveDid
 * are all public). Dispatched from main.c's argv switch. Not part of any
 * installed API -- this is the CLI demo program, not the SDK. */

/* wolfram browse <service> <handle-or-did> [collection] [rkey]
 *   [--limit N] [--cursor C] [--json]
 *
 * No collection: prints the repo's DID, handle, and collection list
 * (describeRepo). Collection only: lists that collection's records
 * (listRecords), paginated. Collection + rkey: prints the full record
 * (getRecord). */
int cmd_browse(int argc, char **argv);

/* wolfram identity <service> <handle-or-did> [--plc-directory URL] [--json]
 *
 * Resolves to a DID, fetches its DID document, and prints every
 * alsoKnownAs handle with a live bidirectional verification check
 * (does the handle's DNS TXT / well-known actually point back at this
 * DID?), every verification method, and every service endpoint. For a
 * did:plc subject, also fetches the account's current PLC operation to
 * show its rotation keys -- those are a PLC-log concept, not part of the
 * resolved (did:core-shaped) DID document itself. */
int cmd_identity(int argc, char **argv);

#endif /* WOLFRAM_CLI_EXPLORE_H */
