# Validation (`validate.h`)

`validate.h` provides runtime validation of JSON values and records against
AT Protocol lexicon schemas. Build a `wf_lexicon_registry` from one or more
lexicon JSON documents, then validate either a full **record** (`wf_validate_record`)
or a value against a **specific definition** (`wf_validate_value`).

Supported schema types: `object`, `record`, `query` (parameters),
`procedure` (input schema), `params`, `blob`, `ref`, `array`, `string`
(including format validation: `datetime`, `did`, `uri`, `at-uri`, `nsid`,
`record-key`, `cid`, …), `integer`, `boolean`, `unknown`, and `union`.

## Building a schema registry

A registry is a set of loaded lexicon documents. Load each document's JSON
(with its length) via `wf_lexicon_registry_load`. References between lexicons
(e.g. a post's `facets` referencing `app.bsky.richtext.facet`) resolve as long
as the referenced lexicon is also loaded.

```c
#include "wolfram/validate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read a whole file into a heap buffer. Returns NULL on failure. */
static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    if (fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f); buf[n] = '\0'; *out_len = n; return buf;
}

wf_lexicon_registry *registry = wf_lexicon_registry_new();
if (!registry) { /* allocation failure */ }

/* Load the lexicons you need. (These ship under lexicons/ in the repo.) */
size_t len = 0;
char *post_lex = read_file("lexicons/app/bsky/feed/post.json", &len);
wf_status st = wf_lexicon_registry_load(registry, post_lex, len);
free(post_lex);
if (st != WF_OK) { /* malformed lexicon JSON */ }

/* If you intend to validate posts WITH facets, also load the facet lexicon: */
char *facet_lex = read_file("lexicons/app/bsky/richtext/facet.json", &len);
wf_lexicon_registry_load(registry, facet_lex, len);
free(facet_lex);
```

## Validating a record (`wf_validate_record`)

`wf_validate_record` validates a JSON object as an instance of the `main`
definition of the named lexicon (which must be a `record` or `object` type).

```c
const char *valid_post =
    "{\"text\":\"hello world\",\"createdAt\":\"2024-01-01T00:00:00Z\"}";

wf_validate_result res = wf_validate_record(
    registry, "app.bsky.feed.post",
    valid_post, strlen(valid_post));

if (res.success) {
    printf("post is valid\n");
} else {
    /* Walk the linked list of errors. */
    for (const wf_validate_error *e = res.errors; e; e = e->next) {
        printf("  at %s: %s\n", e->path ? e->path : "<root>", e->message);
    }
}
wf_validate_result_free(&res);   // frees the error list
```

A record missing a required field is rejected, and `res.success == 0` with a
non-NULL `res.errors` list:

```c
const char *missing_field = "{\"text\":\"hello world\"}";  // no createdAt
wf_validate_result res = wf_validate_record(
    registry, "app.bsky.feed.post", missing_field, strlen(missing_field));
/* res.success == 0; res.errors->path == "record/createdAt" (or similar) */
wf_validate_result_free(&res);
```

Type mismatches are caught too (e.g. `text` as a number instead of a string).

## Validating a scalar / nested value (`wf_validate_value`)

`wf_validate_value` validates against an arbitrary **named definition** inside a
lexicon, identified by `lexicon_id` and `def_id`. Use it for query parameters,
procedure inputs, or any non-`record` definition.

```c
/* Validate an AT-URI string against com.atproto.repo.strongRef's `main`
 * defined type (an object with `uri` + `cid`). Here we validate the object. */
const char *strong_ref =
    "{\"uri\":\"at://did:plc:abc/app.bsky.feed.post/xyz\","
     "\"cid\":\"bafyre...cid\"}";

wf_validate_result r = wf_validate_value(
    registry, "com.atproto.repo.strongRef", "main",
    strong_ref, strlen(strong_ref));
/* r.success == 1 for a well-formed strongRef */
wf_validate_result_free(&r);

/* A malformed `uri` (not an at-uri) yields a failure with a format error. */
const char *bad = "{\"uri\":\"not-an-at-uri\",\"cid\":\"bafyre...cid\"}";
wf_validate_result r2 = wf_validate_value(
    registry, "com.atproto.repo.strongRef", "main", bad, strlen(bad));
/* r2.success == 0 */
wf_validate_result_free(&r2);
```

## Interpreting the result

`wf_validate_result` is a **value struct** (not a pointer) returned by copy:

- `success` — `1` if valid, `0` otherwise.
- `errors` — head of a singly linked list of `wf_validate_error`; `NULL` when
  valid. Each node has `path` (a JSON-path-ish string such as
  `record/text` or `record/createdAt`) and `message` (human-readable).

Always call `wf_validate_result_free(&res)` when you are done — it releases the
entire error chain. The registry is freed separately with
`wf_lexicon_registry_free(registry)`.

## Function reference

Self-contained examples for the two validation entry points. Both return a
`wf_validate_result` **by value**; always call `wf_validate_result_free` to
release the error chain, and free the registry with
`wf_lexicon_registry_free`.

### `wf_validate_record`

```c
#include "wolfram/validate.h"

wf_lexicon_registry *registry = wf_lexicon_registry_new();
if (!registry) { /* allocation failure */ }

/* Load the post lexicon (and facet lexicon if you validate facets). */
size_t len = 0;
char *post_lex = read_file("lexicons/app/bsky/feed/post.json", &len);
wf_lexicon_registry_load(registry, post_lex, len);
free(post_lex);

const char *post = "{\"text\":\"hello\",\"createdAt\":\"2024-01-01T00:00:00Z\"}";
wf_validate_result res = wf_validate_record(registry, "app.bsky.feed.post",
                                            post, strlen(post));
if (res.success) {
    printf("post is valid\n");
} else {
    for (const wf_validate_error *e = res.errors; e; e = e->next)
        printf("  at %s: %s\n", e->path ? e->path : "<root>", e->message);
}
wf_validate_result_free(&res);
wf_lexicon_registry_free(registry);
```

### `wf_validate_value`

```c
/* Validate an app.bsky.richtext.facet against its named definition `main`. */
const char *facet =
    "{\"index\":{\"byteStart\":0,\"byteEnd\":4},"
     "\"features\":[{\"$type\":\"app.bsky.richtext.facet#link\","
     "\"uri\":\"https://example.com\"}]}";

wf_validate_result res = wf_validate_value(
    registry, "app.bsky.richtext.facet", "main", facet, strlen(facet));
if (!res.success) {
    for (const wf_validate_error *e = res.errors; e; e = e->next)
        printf("  %s: %s\n", e->path ? e->path : "<root>", e->message);
}
wf_validate_result_free(&res);
```
