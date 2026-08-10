/*
 * cli_video.h — the `video` subcommand handler declaration.
 *
 * Split out of main.c so the video upload/status subcommands live in their
 * own translation unit. Not part of any installed API -- this is the CLI
 * demo program, not the SDK.
 */

#ifndef WOLFRAM_CLI_VIDEO_H
#define WOLFRAM_CLI_VIDEO_H

int cmd_video(int argc, char **argv);

#endif /* WOLFRAM_CLI_VIDEO_H */
