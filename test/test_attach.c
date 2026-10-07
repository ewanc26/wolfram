/*
 * test_attach.c -- wf_attach_mime and wf_attach_scan_images, on a scratch
 * folder. The upload half is in test_graph_writes.c, which has a mock PDS.
 */

#include "wolfram/attach.h"

#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *dir, const char *name, size_t size) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (size_t i = 0; i < size; i++) fputc('x', f);
    fclose(f);
}

int main(void) {
    WF_CHECK(strcmp(wf_attach_mime("a.jpg"), "image/jpeg") == 0);
    WF_CHECK(strcmp(wf_attach_mime("/x/y/A.JPEG"), "image/jpeg") == 0);
    WF_CHECK(strcmp(wf_attach_mime("pic.PnG"), "image/png") == 0);
    WF_CHECK(wf_attach_mime("pic.gif") == NULL);
    WF_CHECK(wf_attach_mime("pic.jpgx") == NULL);
    WF_CHECK(wf_attach_mime("png") == NULL);
    WF_CHECK(wf_attach_mime("") == NULL);
    WF_CHECK(wf_attach_mime(NULL) == NULL);

    char dir[] = "/tmp/wf_attach_XXXXXX";
    WF_CHECK(mkdtemp(dir) != NULL);
    write_file(dir, "b.png", 10);
    write_file(dir, "a.JPG", 1);
    write_file(dir, "c.jpeg", 5);
    write_file(dir, "notes.txt", 5);   /* wrong type */
    write_file(dir, ".hidden.png", 5); /* dotfile */
    write_file(dir, "empty.png", 0);   /* empty */
    write_file(dir, "huge.png", WF_ATTACH_MAX_BYTES + 1);
    write_file(dir, "exact.png",
               WF_ATTACH_MAX_BYTES); /* the limit is allowed */
    char sub[600];
    snprintf(sub, sizeof sub, "%s/dir.png", dir);
    mkdir(sub, 0777); /* a directory with an image name */
    write_file(dir, "toolongname-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.png",
               3);

    char names[8][32];
    int too_large = -1;
    int n = wf_attach_scan_images(dir, &names[0][0], sizeof names[0], 8,
                                  &too_large);
    WF_CHECK(n == 4);
    WF_CHECK(too_large == 1);
    WF_CHECK(strcmp(names[0], "a.JPG") == 0);
    WF_CHECK(strcmp(names[1], "b.png") == 0);
    WF_CHECK(strcmp(names[2], "c.jpeg") == 0);
    WF_CHECK(strcmp(names[3], "exact.png") == 0);

    /* The cap is honoured, and the count is optional. */
    WF_CHECK(wf_attach_scan_images(dir, &names[0][0], sizeof names[0], 2,
                                   NULL) == 2);

    /* Missing folder and bad arguments find nothing. */
    WF_CHECK(wf_attach_scan_images("/tmp/wf_attach_nope", &names[0][0],
                                   sizeof names[0], 8, &too_large) == 0);
    WF_CHECK(too_large == 0);
    WF_CHECK(wf_attach_scan_images(NULL, &names[0][0], sizeof names[0], 8,
                                   NULL) == 0);
    WF_CHECK(wf_attach_scan_images(dir, NULL, 32, 8, NULL) == 0);
    WF_CHECK(wf_attach_scan_images(dir, &names[0][0], sizeof names[0], 0,
                                   NULL) == 0);

    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
    if (system(cmd) != 0) return 1;

    /* The camera layout: images one folder down, named "folder/name". */
    char cam[] = "/tmp/wf_cam_XXXXXX";
    WF_CHECK(mkdtemp(cam) != NULL);
    char cam_sub[600];
    snprintf(cam_sub, sizeof cam_sub, "%s/100NIN01", cam);
    mkdir(cam_sub, 0777);
    write_file(cam, "top.jpg", 3);
    write_file(cam_sub, "IMG_0002.JPG", 3);
    write_file(cam_sub, "IMG_0001.JPG", 3);
    write_file(cam_sub, "notes.txt", 3);
    char deep[600];
    snprintf(deep, sizeof deep, "%s/100NIN01/deeper", cam);
    mkdir(deep, 0777);
    write_file(deep, "too_deep.jpg", 3); /* two levels down: not listed */
    char tree[8][64];
    int tn =
        wf_attach_scan_images_tree(cam, &tree[0][0], sizeof tree[0], 8, NULL);
    WF_CHECK(tn == 3);
    WF_CHECK(tn >= 3 && strcmp(tree[0], "100NIN01/IMG_0001.JPG") == 0);
    WF_CHECK(tn >= 3 && strcmp(tree[1], "100NIN01/IMG_0002.JPG") == 0);
    WF_CHECK(tn >= 3 && strcmp(tree[2], "top.jpg") == 0);
    WF_CHECK(wf_attach_scan_images_tree("/tmp/wf_no_such_dir_xyz", &tree[0][0],
                                        sizeof tree[0], 8, NULL) == 0);
    snprintf(cmd, sizeof cmd, "rm -rf %s", cam);
    (void)system(cmd);

    WF_TEST_SUMMARY();
}
