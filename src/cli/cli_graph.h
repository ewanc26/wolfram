#ifndef WOLFRAM_CLI_GRAPH_H
#define WOLFRAM_CLI_GRAPH_H

#include <stddef.h>

int cmd_follows(int argc, char **argv);
int cmd_followers(int argc, char **argv);
int cmd_blocks(int argc, char **argv);
int cmd_mutes(int argc, char **argv);
int cmd_list(int argc, char **argv);
int cmd_lists(int argc, char **argv);
int cmd_list_create(int argc, char **argv);
int cmd_list_update(int argc, char **argv);
int cmd_list_delete(int argc, char **argv);
int cmd_list_add_item(int argc, char **argv);
int cmd_list_remove_item(int argc, char **argv);
int cmd_mute_list(int argc, char **argv);
int cmd_unmute_list(int argc, char **argv);
int cmd_block_list(int argc, char **argv);
int cmd_unblock_list(int argc, char **argv);
int cmd_mute_thread(int argc, char **argv);
int cmd_unmute_thread(int argc, char **argv);
int cmd_get_list_blocks(int argc, char **argv);
int cmd_get_list_mutes(int argc, char **argv);
int cmd_get_suggested_follows(int argc, char **argv);
int cmd_starter_pack(int argc, char **argv);

#endif /* WOLFRAM_CLI_GRAPH_H */
