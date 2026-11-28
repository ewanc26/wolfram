#!/usr/bin/env python3
"""Write the seed corpus for the libFuzzer harnesses in test/fuzz/.

Seeds come from the repository's existing test fixtures and vectors
(test/fixtures/, test/vectors/) plus small hand-built frames in the wire
shapes the decoders accept. Python's standard library only: the CBOR encoder
below is independent of Wolfram's own, so a seed never inherits a Wolfram
encoder bug.

    python3 test/fuzz/make_seeds.py            # (re)write test/fuzz/corpus/

Files are named seed-<what>; inputs fuzzing finds and that are worth keeping
are added beside them as crash-<what> (see docs/fuzzing.md). Running
this script rewrites only seed-* files.
"""

import base64
import hashlib
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIXTURES = os.path.join(ROOT, "test", "fixtures")
CORPUS = os.path.join(ROOT, "test", "fuzz", "corpus")


# ---- minimal DAG-CBOR encoder ------------------------------------------------

class Link:
    """A CID, encoded as tag 42 over a 0x00-prefixed binary CID."""

    def __init__(self, cid_bytes):
        self.cid = cid_bytes


def _head(major, n):
    if n < 24:
        return bytes([major << 5 | n])
    if n < 0x100:
        return bytes([major << 5 | 24, n])
    if n < 0x10000:
        return bytes([major << 5 | 25]) + struct.pack(">H", n)
    if n < 0x100000000:
        return bytes([major << 5 | 26]) + struct.pack(">I", n)
    return bytes([major << 5 | 27]) + struct.pack(">Q", n)


def cbor(v):
    if v is None:
        return b"\xf6"
    if v is True:
        return b"\xf5"
    if v is False:
        return b"\xf4"
    if isinstance(v, int):
        return _head(0, v) if v >= 0 else _head(1, -1 - v)
    if isinstance(v, bytes):
        return _head(2, len(v)) + v
    if isinstance(v, str):
        b = v.encode()
        return _head(3, len(b)) + b
    if isinstance(v, list):
        return _head(4, len(v)) + b"".join(cbor(x) for x in v)
    if isinstance(v, dict):
        # DAG-CBOR key order: shorter keys first, then bytewise.
        keys = sorted(v, key=lambda k: (len(k.encode()), k.encode()))
        return _head(5, len(keys)) + b"".join(cbor(k) + cbor(v[k]) for k in keys)
    if isinstance(v, Link):
        return b"\xd8\x2a" + cbor(b"\x00" + v.cid)
    raise TypeError(type(v))


def cid_of(block):
    """CIDv1, dag-cbor codec, sha2-256."""
    return bytes([0x01, 0x71, 0x12, 0x20]) + hashlib.sha256(block).digest()


def cid_from_string(s):
    assert s[0] == "b", s
    body = s[1:].upper()
    return base64.b32decode(body + "=" * (-len(body) % 8))


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def car(roots, blocks):
    header = cbor({"version": 1, "roots": [Link(r) for r in roots]})
    out = varint(len(header)) + header
    for cid, data in blocks:
        out += varint(len(cid) + len(data)) + cid + data
    return out


def frame(header, body):
    return cbor(header) + cbor(body)


# ---- seed sets ---------------------------------------------------------------

def load(name):
    with open(os.path.join(FIXTURES, name), "rb") as f:
        return f.read()


def b64(s):
    return base64.b64decode(s + "=" * (-len(s) % 4))


def repo_seeds():
    """A tiny valid repo: one record, one MST node, one commit."""
    record = cbor({"$type": "app.bsky.feed.post", "text": "hello",
                   "createdAt": "2024-01-01T00:00:00.000Z"})
    rec_cid = cid_of(record)
    key = b"app.bsky.feed.post/3jzfcijpj2z2a"
    node = cbor({"l": None, "e": [{"p": 0, "k": key, "v": Link(rec_cid), "t": None}]})
    node_cid = cid_of(node)
    commit = cbor({"did": "did:plc:abcdefghijklmnopqrstuvwx", "version": 3,
                   "data": Link(node_cid), "rev": "3jzfcijpj2z2a", "prev": None,
                   "sig": bytes(64)})
    commit_cid = cid_of(commit)
    blocks = [(commit_cid, commit), (node_cid, node), (rec_cid, record)]
    return record, node, commit, commit_cid, blocks


def seeds():
    out = {}

    def add(harness, name, data):
        out.setdefault(harness, {})["seed-" + name] = data

    # fuzz_cbor: the DAG-CBOR interop vectors.
    for vec in json.loads(load("dag-cbor-vectors.json")):
        add("fuzz_cbor", vec["name"], bytes.fromhex(vec["cbor"]))

    record, node, commit, commit_cid, blocks = repo_seeds()
    add("fuzz_cbor", "record", record)

    # fuzz_car: the CAR interop fixture and a CAR built from a commit.
    for i, fx in enumerate(json.loads(load("car-file-fixtures.json"))):
        add("fuzz_car", "fixture-%d" % i, b64(fx["car"]))
        for j, blk in enumerate(fx["blocks"]):
            add("fuzz_mst_node", "car-fixture-%d-block-%d" % (i, j), b64(blk["bytes"]))
    add("fuzz_car", "repo", car([commit_cid], blocks))
    add("fuzz_car", "no-roots", car([], blocks[1:]))

    # fuzz_mst_node / fuzz_commit
    add("fuzz_mst_node", "leaf-node", node)
    left = cbor({"l": Link(cid_of(node)), "e": [
        {"p": 0, "k": b"app.bsky.feed.like/3k", "v": Link(cid_of(record)), "t": None},
        {"p": 18, "k": b"zz", "v": Link(cid_of(record)), "t": Link(cid_of(node))}]})
    add("fuzz_mst_node", "two-entry-node", left)
    add("fuzz_commit", "v3", commit)
    add("fuzz_commit", "v2-prev", cbor({
        "did": "did:web:example.com", "version": 2, "data": Link(cid_of(node)),
        "rev": "3k", "prev": Link(commit_cid), "sig": bytes(range(64))}))

    # fuzz_firehose_frame: one frame per subscribeRepos message type.
    repo_car = car([commit_cid], blocks)
    hdr = lambda t: {"op": 1, "t": t}
    add("fuzz_firehose_frame", "commit", frame(hdr("#commit"), {
        "seq": 42, "rebase": False, "tooBig": False,
        "repo": "did:plc:abcdefghijklmnopqrstuvwx", "commit": Link(commit_cid),
        "rev": "3jzfcijpj2z2a", "since": None, "blocks": repo_car,
        "ops": [{"action": "create", "path": "app.bsky.feed.post/3jzfcijpj2z2a",
                 "cid": Link(cid_of(record)), "prev": None}],
        "blobs": [], "prevData": Link(cid_of(node)),
        "time": "2024-01-01T00:00:00.000Z"}))
    add("fuzz_firehose_frame", "sync", frame(hdr("#sync"), {
        "seq": 43, "did": "did:plc:abcdefghijklmnopqrstuvwx", "blocks": repo_car,
        "rev": "3jzfcijpj2z2a", "time": "2024-01-01T00:00:00.000Z"}))
    add("fuzz_firehose_frame", "identity", frame(hdr("#identity"), {
        "seq": 44, "did": "did:plc:abcdefghijklmnopqrstuvwx",
        "time": "2024-01-01T00:00:00.000Z", "handle": "alice.example.com"}))
    add("fuzz_firehose_frame", "account", frame(hdr("#account"), {
        "seq": 45, "did": "did:plc:abcdefghijklmnopqrstuvwx",
        "time": "2024-01-01T00:00:00.000Z", "active": False, "status": "takendown"}))
    add("fuzz_firehose_frame", "info", frame(hdr("#info"), {
        "name": "OutdatedCursor", "message": "cursor too old"}))
    label = {"ver": 1, "src": "did:plc:labeler", "uri": "at://did:plc:x/app.bsky.feed.post/1",
             "cid": "bafyreie5cvv4h45feadgeuwhbcutmh6t2ceseocckahdoe6uat64zmz454",
             "val": "spam", "neg": False, "cts": "2024-01-01T00:00:00.000Z",
             "exp": "2025-01-01T00:00:00.000Z", "sig": bytes(64)}
    add("fuzz_firehose_frame", "labels", frame(hdr("#labels"), {"seq": 46, "labels": [label]}))
    add("fuzz_firehose_frame", "error", frame({"op": -1}, {
        "error": "FutureCursor", "message": "cursor in the future"}))

    # fuzz_label_frame (CBOR) and fuzz_label_message (JSON)
    add("fuzz_label_frame", "labels", frame(hdr("#labels"), {"seq": 7, "labels": [label, dict(label, neg=True)]}))
    add("fuzz_label_frame", "info", frame(hdr("#info"), {"name": "OutdatedCursor"}))
    add("fuzz_label_frame", "error", frame({"op": -1}, {"error": "ConsumerTooSlow"}))
    jlabel = dict(label, sig={"$bytes": base64.b64encode(bytes(64)).decode().rstrip("=")})
    add("fuzz_label_message", "labels", json.dumps(
        {"$type": "com.atproto.label.subscribeLabels#labels", "seq": 7, "labels": [jlabel]}).encode())
    add("fuzz_label_message", "info", json.dumps(
        {"$type": "com.atproto.label.subscribeLabels#info", "name": "OutdatedCursor",
         "message": "old"}).encode())

    # fuzz_chat_mod_frame (CBOR) and fuzz_chat_mod_event (JSON)
    mod = {"convoId": "convo-1", "rev": "rev-1", "createdAt": "2024-01-01T00:00:00.000Z",
           "actorDid": "did:plc:alice", "subjectDid": "did:plc:bob", "seq": 9}
    add("fuzz_chat_mod_frame", "first-message", frame(
        {"op": 1, "t": "#eventConvoFirstMessage"}, mod))
    add("fuzz_chat_mod_frame", "error", frame({"op": -1}, {"error": "InvalidRequest", "message": "no"}))
    add("fuzz_chat_mod_event", "first-message", json.dumps(
        dict(mod, **{"$type": "chat.bsky.moderation.subscribeModEvents#eventConvoFirstMessage"})).encode())

    # fuzz_jetstream: legacy, info and v2 shapes.
    js = {"did": "did:plc:abcdefghijklmnopqrstuvwx", "time_us": 1725911162329308}
    add("fuzz_jetstream", "commit-create", json.dumps(dict(js, kind="commit", commit={
        "rev": "3l3qo2vutsw2b", "operation": "create", "collection": "app.bsky.feed.like",
        "rkey": "3l3qo2vuowo2b", "record": {"$type": "app.bsky.feed.like",
        "createdAt": "2024-09-09T19:46:02.102Z", "subject": {
            "cid": "bafyreidc6sydkkbchcyg62v77wbhzvb2mvytlmsychqgwf2xojjtirmzj4",
            "uri": "at://did:plc:wa7b35aakoll7hugkrjtf3xf/app.bsky.feed.post/3l3pte3p2e325"}},
        "cid": "bafyreidwaivazkwu67xztlmuobx35hs2lnfh3kolmgfmucldvhd3sgzcqi"})).encode())
    add("fuzz_jetstream", "commit-delete", json.dumps(dict(js, kind="commit", commit={
        "rev": "3l3qo2vutsw2b", "operation": "delete", "collection": "app.bsky.graph.follow",
        "rkey": "3l3qo2vuowo2b"})).encode())
    add("fuzz_jetstream", "identity", json.dumps(dict(js, kind="identity", identity={
        "did": js["did"], "handle": "alice.example.com", "seq": 1409752997,
        "time": "2024-09-05T06:11:04.870Z"})).encode())
    add("fuzz_jetstream", "account", json.dumps(dict(js, kind="account", account={
        "active": False, "did": js["did"], "seq": 1409753013,
        "time": "2024-09-05T06:11:04.870Z", "status": "deactivated"})).encode())
    add("fuzz_jetstream", "info", json.dumps({"kind": "info", "did": "",
        "name": "OutdatedCursor", "message": "old"}).encode())
    add("fuzz_jetstream", "v2-frame", json.dumps({"op": 1, "t": "#commit", "seq": 5,
        "body": dict(js, kind="commit", commit={"rev": "3l", "operation": "create",
                     "collection": "app.bsky.feed.post", "rkey": "3l",
                     "record": {"text": "hi"}, "cid": "bafy"})}).encode())

    # fuzz_lex_decode: each JSON response fixture against its decoder.
    lex = {
        "actorFeeds.json": "app_bsky_feed_get_actor_feeds_main_output",
        "actorLikes.json": "app_bsky_feed_get_actor_likes_main_output",
        "blockActorList.json": "app_bsky_graph_get_list_main_output",
        "blocks.json": "app_bsky_graph_get_blocks_main_output",
        "chat_convo.json": "chat_bsky_convo_get_convo_main_output",
        "chat_convos.json": "chat_bsky_convo_list_convos_main_output",
        "chat_group_convo.json": "chat_bsky_convo_get_convo_main_output",
        "chat_messages.json": "chat_bsky_convo_get_messages_main_output",
        "chat_notification_prefs.json": "chat_bsky_notification_get_preferences_main_output",
        "chat_send_message.json": "chat_bsky_convo_send_message_main_output",
        "describe_feed_generator.json": "app_bsky_feed_describe_feed_generator_main_output",
        "feed.json": "app_bsky_feed_get_feed_main_output",
        "feed_skeleton.json": "app_bsky_feed_get_feed_skeleton_main_output",
        "follows.json": "app_bsky_graph_get_follows_main_output",
        "knownFollowers.json": "app_bsky_graph_get_known_followers_main_output",
        "lexcall_unread.json": "app_bsky_notification_get_unread_count_main_output",
        "likes.json": "app_bsky_feed_get_likes_main_output",
        "listItems.json": "app_bsky_graph_get_list_main_output",
        "listNotifications.json": "app_bsky_notification_list_notifications_main_output",
        "lists.json": "app_bsky_graph_get_lists_main_output",
        "muteActorList.json": "app_bsky_graph_get_list_main_output",
        "mutes.json": "app_bsky_graph_get_mutes_main_output",
        "posts.json": "app_bsky_feed_get_posts_main_output",
        "profiles.json": "app_bsky_actor_get_profiles_main_output",
        "quotes.json": "app_bsky_feed_get_quotes_main_output",
        "relationships.json": "app_bsky_graph_get_relationships_main_output",
        "report.json": "com_atproto_moderation_create_report_main_output",
        "repostedBy.json": "app_bsky_feed_get_reposted_by_main_output",
        "thread.json": "app_bsky_feed_get_post_thread_main_output",
        "timeline.json": "app_bsky_feed_get_timeline_main_output",
    }
    for fixture, decoder in lex.items():
        add("fuzz_lex_decode", fixture[:-5], decoder.encode() + b"\n" + load(fixture))
    return out


def main():
    for harness, files in sorted(seeds().items()):
        d = os.path.join(CORPUS, harness)
        os.makedirs(d, exist_ok=True)
        for name in os.listdir(d):
            if name.startswith("seed-"):
                os.remove(os.path.join(d, name))
        for name, data in sorted(files.items()):
            with open(os.path.join(d, name), "wb") as f:
                f.write(data)
        print("%-22s %3d seeds" % (harness, len(files)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
