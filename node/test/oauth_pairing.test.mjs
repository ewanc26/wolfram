/*
 * oauth_pairing.test.mjs -- the Node addon's pairing contract against the
 * shared vectors (test/vectors/oauth_pairing.json). The same vectors run
 * through the C suite (test/test_oauth_pairing.c) and through consumers'
 * bindings in their own tests; a status or shape that changes meaning must
 * break here first. Errors are asserted by `code`, never by text.
 *
 * The addon is loaded from build/wolfram_core.node unless
 * WOLFRAM_NODE_ADDON points at another build, as in update.test.mjs.
 */

import { strict as assert } from 'node:assert'
import { readFileSync } from 'node:fs'
import { createRequire } from 'node:module'
import { test } from 'node:test'
import { fileURLToPath } from 'node:url'

const addonURL = new URL('build/wolfram_core.node', new URL('../..', import.meta.url))
const addon = createRequire(import.meta.url)(
    process.env.WOLFRAM_NODE_ADDON ?? fileURLToPath(addonURL),
)

const vectors = JSON.parse(
    readFileSync(fileURLToPath(new URL('test/vectors/oauth_pairing.json',
        new URL('../..', import.meta.url)))),
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

const asText = (c) => ('body_text' in c ? c.body_text : JSON.stringify(c.body))

test('buildPairBegin runs the begin-body vectors', () => {
    for (const c of vectors.build_begin) {
        if (c.invalid) {
            throwsStatus(() => addon.buildPairBegin(c.handle), 'WF_ERR_INVALID_ARG')
            // A NUL is one of the control characters the contract rejects.
            throwsStatus(() => addon.buildPairBegin('a\u0000b'), 'WF_ERR_INVALID_ARG')
        } else {
            assert.equal(addon.buildPairBegin(c.handle), c.body, c.name)
        }
    }
})

test('parsePairBegin runs the begin-reply vectors', () => {
    for (const c of vectors.begin) {
        if (c.expect === 'parse_error') {
            throwsStatus(() => addon.parsePairBegin(asText(c)), 'WF_ERR_PARSE')
        } else {
            const e = c.expect
            assert.deepEqual(
                addon.parsePairBegin(asText(c)),
                { pairCode: e.pair_code, pairUrl: e.pair_url, expiresAt: e.expires_at },
                c.name,
            )
        }
    }
})

test('parsePairPoll runs the poll-reply vectors', () => {
    for (const c of vectors.poll) {
        if (c.expect === 'parse_error') {
            throwsStatus(() => addon.parsePairPoll(asText(c)), 'WF_ERR_PARSE')
        } else {
            // The poll vectors' member names are their own camelCase forms.
            assert.deepEqual(addon.parsePairPoll(asText(c)), c.expect, c.name)
        }
    }
})

test('the parsers take bytes or text alike', () => {
    assert.equal(addon.buildPairBegin('a.example'), '{"handle":"a.example"}')
    assert.deepEqual(
        addon.parsePairBegin(Buffer.from('{"pair_code":"abc","pair_url":"https://a.example/p"}')),
        { pairCode: 'abc', pairUrl: 'https://a.example/p', expiresAt: 0 },
    )
    assert.deepEqual(
        addon.parsePairPoll(Buffer.from('{"status":"pending"}')),
        { state: 'pending' },
    )
})
