# Notification Module

The notification functionality has been split into a dedicated source file `src/agent/notification.c`. It provides three public API functions declared in `include/wolfram/agent.h`:

- `wf_status wf_agent_list_notifications(wf_agent *agent, int limit, const char *cursor, wf_response *out);`
- `wf_status wf_agent_update_seen_notifications(wf_agent *agent, const char *seen_at);`
- `wf_status wf_agent_get_unread_count(wf_agent *agent, wf_response *out);`

These functions wrap the AT Protocol endpoints under `app.bsky.notification.*` and return raw JSON responses, mirroring the style of other Feed/Graph APIs.

## Internal Helpers

To avoid code duplication across modules, common internal helpers (`wf_agent_int_to_str`, `wf_agent_is_logged_in`, `wf_agent_sync_auth`, and the private `wf_agent` struct) are now defined in `src/agent/_internal.h`. Each module (`feed.c`, `graph.c`, `post.c`, `notification.c`) includes this header, and the duplicated static definitions have been removed.

## Build Integration

`_internal.h` is guarded against multiple inclusion, so adding `#include "_internal.h"` in each module incurs no overhead. No changes to `CMakeLists.txt` are required because the header is not a compiled source file.

## Usage Example

See the new example program `examples/notification_demo.c` for a quick illustration of listing notifications, marking them as seen, and fetching the unread count.
