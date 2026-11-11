/*
 * ageassurance.test.mjs -- the Node addon's ageassurance_typed.h parsers
 * against the shared vectors (test/vectors/ageassurance.json):
 * ageassuranceParseBegin / ageassuranceParseConfig / ageassuranceParseState.
 * The same bodies run through the C suite (test/test_ageassurance_typed.c); a
 * field that changes meaning must break here first. Errors are asserted by
 * `code`, never by text.
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
    readFileSync(fileURLToPath(new URL('test/vectors/ageassurance.json', root))),
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

test('ageassuranceParseBegin runs the begin vectors', () => {
    for (const c of vectors.begin) {
        if (c.error) {
            throwsStatus(() => addon.ageassuranceParseBegin(c.json_text), c.error)
        } else {
            assert.deepEqual(addon.ageassuranceParseBegin(c.json_text), c.expect, c.name)
        }
    }
})

test('ageassuranceParseConfig runs the config vectors', () => {
    for (const c of vectors.config) {
        if (c.error) {
            throwsStatus(() => addon.ageassuranceParseConfig(c.json_text), c.error)
        } else {
            assert.deepEqual(addon.ageassuranceParseConfig(c.json_text), c.expect, c.name)
        }
    }
})

test('ageassuranceParseState runs the state vectors', () => {
    for (const c of vectors.state) {
        if (c.error) {
            throwsStatus(() => addon.ageassuranceParseState(c.json_text), c.error)
        } else {
            assert.deepEqual(addon.ageassuranceParseState(c.json_text), c.expect, c.name)
        }
    }
})
