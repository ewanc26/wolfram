/*
 * chat_group.test.mjs -- the Node addon's chat_group.h contracts against the
 * shared vectors (test/vectors/chat_group.json): chatConvoIsGroup over getConvo
 * bodies, chatJoinLinkUrl, chatJoinLinkIsEnabled. The same helpers run through
 * the C suite (test/test_chat_typed.c); a rule that changes meaning must break
 * here first. Errors are asserted by `code`, never by text.
 */

import { strict as assert } from 'node:assert'
import { readFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { test } from 'node:test'
import { fileURLToPath } from 'node:url'

const root = new URL('../..', import.meta.url)
const addon = createRequire(import.meta.url)(
    process.env.WOLFRAM_NODE_ADDON ?? fileURLToPath(new URL('build/wolfram_core.node', root)),
)

const vectors = JSON.parse(
    readFileSync(fileURLToPath(new URL('test/vectors/chat_group.json', root))),
)

function throwsStatus(run, code) {
    try {
        run()
    } catch (e) {
        assert.equal(e.code, code, `expected ${code}, got ${e.code}: ${e.message}`)
        return
    }
    assert.fail(`expected ${code}, but the call returned`)
}

test('chatConvoIsGroup runs the is_group vectors', () => {
    for (const c of vectors.is_group) {
        if (c.error) {
            throwsStatus(() => addon.chatConvoIsGroup(c.convo_text), c.error)
        } else {
            assert.equal(addon.chatConvoIsGroup(c.convo_text), c.expect, c.name)
        }
    }
})

test('chatJoinLinkUrl runs the join_link_url vectors', () => {
    for (const c of vectors.join_link_url) {
        assert.equal(addon.chatJoinLinkUrl(c.code), c.expect, c.name)
    }
})

test('chatJoinLinkIsEnabled runs the join_link_enabled vectors', () => {
    for (const c of vectors.join_link_enabled) {
        assert.equal(addon.chatJoinLinkIsEnabled(c.status), c.expect, c.name)
    }
})
