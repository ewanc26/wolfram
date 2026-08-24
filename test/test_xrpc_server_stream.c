/* Streaming XRPC procedures must never accumulate a complete request body. */

#define _POSIX_C_SOURCE 200809L

#include "wolfram/xrpc.h"
#include "wolfram/xrpc_server.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct test_context {
    pthread_mutex_t mutex;
    size_t expected_size;
    size_t max_chunk;
    size_t completed_bytes;
    unsigned int completed;
    unsigned int aborted;
} test_context;

typedef struct upload_state {
    test_context *test;
    FILE *file;
    size_t received;
} upload_state;

static wf_status stream_begin(void *ctx, const wf_xrpc_request *request,
                              void **out_stream_ctx,
                              wf_xrpc_response *response) {
    test_context *test = ctx;
    if (request->body || request->body_len != 0 ||
        !request->has_content_length) {
        wf_xrpc_response_set_error(response, 500, "BadRequestState",
                                   "streaming request exposed a body buffer");
        return WF_OK;
    }
    cJSON *part =
        request->params
            ? cJSON_GetObjectItemCaseSensitive(request->params, "partNumber")
            : NULL;
    if (!cJSON_IsString(part) || strcmp(part->valuestring, "1") != 0) {
        wf_xrpc_response_set_error(response, 400, "InvalidPartNumber",
                                   "partNumber must be 1");
        return WF_OK;
    }
    if (request->content_length != test->expected_size) {
        wf_xrpc_response_set_error(response, 400, "PartSizeMismatch",
                                   "Content-Length does not match");
        return WF_OK;
    }

    upload_state *state = calloc(1, sizeof(*state));
    if (!state) return WF_ERR_ALLOC;
    state->test = test;
    state->file = tmpfile();
    if (!state->file) {
        free(state);
        return WF_ERR_INTERNAL;
    }
    *out_stream_ctx = state;
    return WF_OK;
}

static wf_status stream_write(void *ctx, void *stream_ctx,
                              const unsigned char *data, size_t data_len,
                              wf_xrpc_response *response) {
    (void)ctx;
    upload_state *state = stream_ctx;
    if (fwrite(data, 1, data_len, state->file) != data_len) {
        wf_xrpc_response_set_error(response, 500, "WriteFailed",
                                   "temporary write failed");
        return WF_OK;
    }
    state->received += data_len;
    pthread_mutex_lock(&state->test->mutex);
    if (data_len > state->test->max_chunk) state->test->max_chunk = data_len;
    pthread_mutex_unlock(&state->test->mutex);
    return WF_OK;
}

static wf_status stream_finish(void *ctx, void *stream_ctx,
                               wf_xrpc_response *response) {
    (void)ctx;
    upload_state *state = stream_ctx;
    if (state->received != state->test->expected_size) {
        wf_xrpc_response_set_error(response, 400, "PartSizeMismatch",
                                   "received byte count does not match");
        return WF_OK;
    }
    char body[96];
    int length =
        snprintf(body, sizeof(body), "{\"sizeBytes\":%zu,\"streamed\":true}",
                 state->received);
    wf_xrpc_response_set_body(response, body, (size_t)length);
    return WF_OK;
}

static void stream_cleanup(void *ctx, void *stream_ctx, bool completed) {
    (void)ctx;
    upload_state *state = stream_ctx;
    pthread_mutex_lock(&state->test->mutex);
    if (completed) {
        state->test->completed++;
        state->test->completed_bytes = state->received;
    } else {
        state->test->aborted++;
    }
    pthread_mutex_unlock(&state->test->mutex);
    fclose(state->file);
    free(state);
}

static int send_interrupted_request(uint16_t port, size_t declared_size) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return 0;
    }
    char headers[512];
    int length =
        snprintf(headers, sizeof(headers),
                 "POST /xrpc/io.example.stream?partNumber=1 HTTP/1.1\r\n"
                 "Host: 127.0.0.1\r\nContent-Type: application/octet-stream\r\n"
                 "Content-Length: %zu\r\nConnection: close\r\n\r\nabc",
                 declared_size);
    ssize_t sent = send(fd, headers, (size_t)length, 0);
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return sent == length;
}

int main(void) {
    test_context test = {0};
    test.expected_size = 1024 * 1024;
    if (pthread_mutex_init(&test.mutex, NULL) != 0) return 1;

    wf_xrpc_server *server = wf_xrpc_server_start("127.0.0.1", 0, 2);
    if (!server) return 1;
    const wf_xrpc_streaming_procedure_handler handler = {
        .begin = stream_begin,
        .write = stream_write,
        .finish = stream_finish,
        .cleanup = stream_cleanup,
    };
    if (wf_xrpc_server_register_streaming_procedure(server, "io.example.stream",
                                                    &handler, &test) != WF_OK)
        return 1;

    char base[96];
    snprintf(base, sizeof(base), "http://127.0.0.1:%u",
             (unsigned int)wf_xrpc_server_port(server));
    wf_xrpc_client *client = wf_xrpc_client_new(base);
    unsigned char *body = malloc(test.expected_size);
    if (!client || !body) return 1;
    memset(body, 0xa5, test.expected_size);

    const wf_xrpc_param params[] = {{"partNumber", "1"}};
    wf_response response = {0};
    wf_status status = wf_xrpc_upload_blob_params(
        client, "io.example.stream", params, 1, body, test.expected_size,
        "application/octet-stream", &response);
    int failed = 0;
    if (status != WF_OK || response.status != 200 || !response.body ||
        !strstr(response.body, "\"streamed\":true")) {
        fprintf(stderr, "stream request failed: status=%d http=%ld body=%s\n",
                (int)status, response.status,
                response.body ? response.body : "(null)");
        failed = 1;
    }
    wf_response_free(&response);

    pthread_mutex_lock(&test.mutex);
    if (test.completed != 1 || test.completed_bytes != test.expected_size ||
        test.max_chunk == 0 || test.max_chunk >= test.expected_size) {
        fprintf(stderr,
                "stream was not bounded: complete=%u bytes=%zu chunk=%zu\n",
                test.completed, test.completed_bytes, test.max_chunk);
        failed = 1;
    }
    pthread_mutex_unlock(&test.mutex);

    test.expected_size++;
    status = wf_xrpc_upload_blob_params(client, "io.example.stream", params, 1,
                                        body, test.expected_size - 1,
                                        "application/octet-stream", &response);
    if (status != WF_ERR_HTTP || response.status != 400 || !response.body ||
        !strstr(response.body, "PartSizeMismatch")) {
        fprintf(stderr, "length mismatch was accepted: %d/%ld %s\n",
                (int)status, response.status,
                response.body ? response.body : "(null)");
        failed = 1;
    }
    wf_response_free(&response);

    if (!send_interrupted_request(wf_xrpc_server_port(server),
                                  test.expected_size)) {
        fprintf(stderr, "failed to send interrupted request\n");
        failed = 1;
    }
    for (int attempt = 0; attempt < 100; attempt++) {
        pthread_mutex_lock(&test.mutex);
        unsigned int aborted = test.aborted;
        pthread_mutex_unlock(&test.mutex);
        if (aborted > 0) break;
        struct timespec delay = {.tv_nsec = 10 * 1000 * 1000};
        nanosleep(&delay, NULL);
    }
    pthread_mutex_lock(&test.mutex);
    if (test.aborted != 1) {
        fprintf(stderr, "interrupted stream was not cleaned up\n");
        failed = 1;
    }
    pthread_mutex_unlock(&test.mutex);

    free(body);
    wf_xrpc_client_free(client);
    wf_xrpc_server_free(server);
    pthread_mutex_destroy(&test.mutex);
    if (!failed) printf("PASS: streaming XRPC procedure\n");
    return failed;
}
