/*
 * post_display.test.mjs -- the Node addon's post_display contract against the
 * shared vectors (test/vectors/post_display.json). The same models run through
 * the C suite (test/test_post_display.c) and through consumers' bindings; a
 * facet rule or embed shape that changes meaning must break here first. The
 * vectors were emitted from the C itself; errors are asserted by `code`,
 * never by text.
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
    readFileSync(fileURLToPath(new URL('test/vectors/post_display.json', root))),
    /* json_text keys hold raw JSON text, not values */
)

function throwsStatus(run, code) {
    try {
        run()
    } catch (e) {
        assert.equal(e.code, code, `expected ${code}, got ${e.code}: ${e.message}`)
        return
    }
    assert.fail(`expected a throw with code ${code}`)
}

function display(c) {
    return addon.postDisplay(c.record_text, c.embed_text)
}

test('postDisplay runs the shared display vectors', () => {
    for (const c of vectors.display) {
        if (c.expect === 'parse_error') {
            throwsStatus(() => display(c), 'WF_ERR_PARSE')
        } else {
            assert.deepEqual(display(c), c.expect, c.name)
        }
    }
})

test('postDisplay takes bytes or text alike, and an absent embed', () => {
    const record = '{"text":"hi","facets":[]}'
    assert.deepEqual(addon.postDisplay(Buffer.from(record)), addon.postDisplay(record))
    assert.equal(addon.postDisplay(Buffer.from('null')).text, '')
})

test('postDisplay wipes nothing it was not given and frees what it filled', () => {
    // A post that fills owned data (facet targets, images, quote) twice in a
    // row: a use-after-free or a double free in the filling path would crash
    // here rather than slip past CI.
    const record = JSON.stringify({
        text: 'a #b',
        createdAt: '2026-01-01T00:00:00Z',
        facets: [{ index: { byteStart: 2, byteEnd: 4 }, features: [{ $type: 'app.bsky.richtext.facet#tag', tag: 'b' }] }],
    })
    const embed = JSON.stringify({
        $type: 'app.bsky.embed.record#view',
        record: { $type: 'app.bsky.embed.record#viewRecord', uri: 'at://x', author: { handle: 'h' }, value: { text: 'q' } },
    })
    const first = addon.postDisplay(record, embed)
    const second = addon.postDisplay(record, embed)
    assert.deepEqual(second, first)
    assert.equal(first.quote.uri, 'at://x')
})
