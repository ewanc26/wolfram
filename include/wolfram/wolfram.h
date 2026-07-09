/**
 * wolfram.h — top-level umbrella header for wolfram, a C SDK for the
 * AT Protocol.
 *
 * Pull this in for the common case. Individual modules (xrpc, identity,
 * crypto, repo) can be included directly if you only need one piece.
 */

#ifndef WOLFRAM_H
#define WOLFRAM_H

#include "wolfram/version.h"
#include "wolfram/xrpc.h"
#include "wolfram/identity.h"
#include "wolfram/crypto.h"
#include "wolfram/repo.h"
#include "wolfram/session.h"
#include "wolfram/agent.h"
#include "wolfram/websocket.h"
#include "wolfram/jetstream.h"
#include "wolfram/sync.h"
#include "wolfram/syntax.h"
#include "wolfram/validate.h"
#include "wolfram/oauth.h"
#include "wolfram/richtext.h"
#include "wolfram/label.h"
#include "wolfram/sync_subscribe.h"
#include "wolfram/sync_verify.h"
#include "wolfram/moderation.h"

/* Typed, owned parsers + agent wrappers for individual lexicon namespaces.
 * Each is self-contained (includes agent.h) and kept separate from the
 * higher-level agent API so applications can pull in only what they use. */
#include "wolfram/contact_typed.h"
#include "wolfram/admin_typed.h"
#include "wolfram/notification_prefs_typed.h"
#include "wolfram/labeler_typed.h"
#include "wolfram/feed_gen_typed.h"
#include "wolfram/identity_typed.h"
#include "wolfram/graph_social_typed.h"
#include "wolfram/actor_status_typed.h"
#include "wolfram/notification_typed.h"
#include "wolfram/notification_v2_typed.h"
#include "wolfram/ozone_typed.h"
#include "wolfram/bsky_agent.h"

#endif /* WOLFRAM_H */
