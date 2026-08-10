#ifndef CLI_SERVER_H
#define CLI_SERVER_H

int cmd_create_account(int argc, char **argv);
int cmd_app_password(int argc, char **argv);
int cmd_invite_codes(int argc, char **argv);
int cmd_activate(int argc, char **argv);
int cmd_deactivate(int argc, char **argv);
int cmd_check_status(int argc, char **argv);
int cmd_email(int argc, char **argv);
int cmd_password_reset(int argc, char **argv);
int cmd_reserve_signing_key(int argc, char **argv);
int cmd_get_service_auth(int argc, char **argv);
int cmd_request_account_delete(int argc, char **argv);
int cmd_request_email_update(int argc, char **argv);
int cmd_request_email_confirmation(int argc, char **argv);

#endif
