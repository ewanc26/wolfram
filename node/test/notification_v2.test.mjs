/*
 * notification_v2.test.mjs -- the Node addon's notification_v2_typed.h
 * contracts against the shared vectors (test/vectors/notification_v2.json):
 * notifV2PreferencesParse / notifV2PreferencesBuild over the 13-slot
 * putPreferencesV2 preferences object, notifV2ParsePutActivitySubscription and
 * notifV2ParseListActivitySubscriptions over their response bodies. The same
 * shapes run through the C suite (test/test_notification_v2_prefs.c,
 * test/test_notification_v2_typed.c); a slot or field that changes meaning
 * must break here first. Errors are asserted by `code`, never by text.
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
    readFileSync(fileURLToPath(new URL('test/vectors/notification_v2.json', root))),
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

test('notifV2PreferencesParse runs the preferences_parse vectors', () => {
    for (const c of vectors.preferences_parse) {
        if (c.error) {
            throwsStatus(() => addon.notifV2PreferencesParse(c.json_text), c.error)
        } else {
            assert.deepEqual(
                addon.notifV2PreferencesParse(c.json_text),
                c.expect,
                c.name,
            )
        }
    }
})

test('notifV2PreferencesBuild runs the preferences_build vectors', () => {
    for (const c of vectors.preferences_build) {
        if (c.error) {
            throwsStatus(() => addon.notifV2PreferencesBuild(c.input), c.error)
        } else {
            // The built text is compared as parsed JSON, not byte-for-byte:
            // key order is the C serializer's, not part of the contract.
            assert.deepEqual(
                JSON.parse(addon.notifV2PreferencesBuild(c.input)),
                c.expect,
                c.name,
            )
        }
    }
})

test('notifV2ParsePutActivitySubscription runs the put_activity_subscription vectors', () => {
    for (const c of vectors.put_activity_subscription) {
        if (c.error) {
            throwsStatus(
                () => addon.notifV2ParsePutActivitySubscription(c.json_text),
                c.error,
            )
        } else {
            assert.deepEqual(
                addon.notifV2ParsePutActivitySubscription(c.json_text),
                c.expect,
                c.name,
            )
        }
    }
})

test('notifV2ParseListActivitySubscriptions runs the list_activity_subscriptions vectors', () => {
    for (const c of vectors.list_activity_subscriptions) {
        if (c.error) {
            throwsStatus(
                () => addon.notifV2ParseListActivitySubscriptions(c.json_text),
                c.error,
            )
        } else {
            assert.deepEqual(
                addon.notifV2ParseListActivitySubscriptions(c.json_text),
                c.expect,
                c.name,
            )
        }
    }
})
