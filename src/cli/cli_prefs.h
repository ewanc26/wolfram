#ifndef CLI_PREFS_H
#define CLI_PREFS_H

int cmd_preferences(int argc, char **argv);
int cmd_register_push(int argc, char **argv);
int cmd_unregister_push(int argc, char **argv);
int cmd_update_profile(int argc, char **argv);
int cmd_put_actor_status(int argc, char **argv);
int cmd_upload_blob(int argc, char **argv);
int cmd_apply_writes(int argc, char **argv);
int cmd_send_interactions(int argc, char **argv);

#endif
