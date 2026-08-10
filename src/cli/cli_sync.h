#ifndef WOLFRAM_CLI_SYNC_H
#define WOLFRAM_CLI_SYNC_H

#include <stddef.h>

int cmd_sync_get_blob(int argc, char **argv);
int cmd_sync_get_blocks(int argc, char **argv);
int cmd_sync_get_record(int argc, char **argv);
int cmd_sync_list_blobs(int argc, char **argv);
int cmd_import_repo(int argc, char **argv);
int cmd_list_missing_blobs(int argc, char **argv);

#endif /* WOLFRAM_CLI_SYNC_H */
