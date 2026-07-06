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

#endif /* WOLFRAM_H */
