/*
 * update.test.mjs -- the Node addon's update contract against Wolfram's
 * shared vectors (test/vectors/update/, test/vectors/ed25519.json), the same
 * bytes the C suite (test/test_update.c, test/test_ed25519.c) and Platinum's
 * TypeScript port run. Nothing here asserts message text: errors are thrown
 * with a `code` that is the wf_status name, and only that is asserted.
 *
 * The addon is loaded from build/wolfram_core.node (the CMake module target)
 * unless WOLFRAM_NODE_ADDON points at another build of it. Run from the repo
 * root with the vectors in place:
 *
 *   node --test node/test/
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

const vectors = (name) =>
    JSON.parse(readFileSync(fileURLToPath(new URL(`test/vectors/${name}`, new URL('../..', import.meta.url)))))

function throwsStatus(run, code) {
    try {
        run()
    } catch (e) {
        assert.equal(e.code, code, `expected ${code}, got ${e.code}: ${e.message}`)
        return
    }
    assert.fail(`expected a throw with code ${code}`)
}

/** The JS-facing policy is camelCase; the shared vectors use Wolfram's C
 * names. Only set keys are carried over. */
function policyOf(v) {
    const policy = {}
    if ('policy' in v) {
        if ('max_size' in v.policy) policy.maxSize = v.policy.max_size
        if ('app' in v.policy) policy.app = v.policy.app
        if ('url_prefix' in v.policy) policy.urlPrefix = v.policy.url_prefix
    }
    return Object.keys(policy).length > 0 ? policy : undefined
}

test('compareVersions runs the semver precedence vectors', () => {
    const { compare, invalid } = vectors('update/versions.json')
    for (const v of compare) {
        const got = Math.sign(addon.compareVersions(v.a, v.b))
        assert.equal(got, v.sign + 0, `${v.a} vs ${v.b}`)
        // Antisymmetry, as the C suite checks.
        const got2 = Math.sign(addon.compareVersions(v.b, v.a))
        assert.equal(got2, -v.sign + 0, `${v.b} vs ${v.a}`)
        assert.equal(addon.versionValid(v.a), true)
    }
    for (const bad of invalid) {
        assert.throws(() => addon.compareVersions(bad, '1.0.0'), undefined, JSON.stringify(bad))
        throwsStatus(() => addon.compareVersions(bad, '1.0.0'), 'WF_ERR_PARSE')
        assert.equal(addon.versionValid(bad), false)
    }
})

test('sha256Hex runs the FIPS 180-4 known-answer vectors', () => {
    for (const v of vectors('update/sha256.json').vectors) {
        const input =
            'input_hex' in v
                ? Buffer.from(v.input_hex, 'hex')
                : Buffer.alloc(v.repeat.count, parseInt(v.repeat.byte_hex, 16))
        assert.equal(addon.sha256Hex(input), v.sha256, v.name)
    }
})

test('parseManifest runs the manifest vectors', () => {
    for (const v of vectors('update/manifest.json').vectors) {
        const body = JSON.stringify(v.manifest)
        if (v.expect === 'ok') {
            const manifest = addon.parseManifest(body, policyOf(v))
            assert.equal(manifest.app, v.manifest.app, v.name)
            assert.equal(manifest.version, v.manifest.version, v.name)
            assert.equal(manifest.asset.size, v.manifest.asset.size, v.name)
            assert.equal(manifest.asset.sha256, v.manifest.asset.sha256.toLowerCase(), v.name)
            if ('has_signature' in v) {
                assert.equal(manifest.hasSignature, v.has_signature, v.name)
            } else {
                assert.equal(manifest.hasSignature, false, v.name)
            }
        } else {
            throwsStatus(
                () => addon.parseManifest(body, policyOf(v)),
                v.expect === 'parse_error' ? 'WF_ERR_PARSE' : 'WF_ERR_VALIDATION',
            )
        }
    }
})

test('parseManifest rejects what the bridge itself rejects', () => {
    const ok = JSON.stringify(vectors('update/manifest.json').vectors[0].manifest)
    throwsStatus(() => addon.parseManifest('not json'), 'WF_ERR_PARSE')
    throwsStatus(() => addon.parseManifest(ok, { urlPrefix: 'https://evil.example/' }), 'WF_ERR_VALIDATION')
    assert.throws(() => addon.parseManifest(ok, 'not an object'))
})

/** The update wrapper's file format: 128 hex characters, one trailing LF
 * allowed. Verify with and without it, as the C suite does. */
test('verifySignature runs the Ed25519 vectors through the update wrapper', () => {
    const { valid, invalid } = vectors('ed25519.json')
    for (const v of valid) {
        const message = Buffer.from(v.message_hex, 'hex')
        assert.equal(addon.verifySignature(message, v.signature, v.public), true, v.note)
        assert.equal(addon.verifySignature(message, `${v.signature}\n`, v.public), true, v.note)
    }
    for (const v of invalid) {
        const message = Buffer.from(v.message_hex, 'hex')
        throwsStatus(() => addon.verifySignature(message, v.signature, v.public), 'WF_ERR_VALIDATION')
    }
})

test('verifySignature rejects a malformed key with WF_ERR_PARSE', () => {
    const v = vectors('ed25519.json').valid[0]
    throwsStatus(
        () => addon.verifySignature(Buffer.from(v.message_hex, 'hex'), v.signature, '00'.repeat(31)),
        'WF_ERR_PARSE',
    )
})
