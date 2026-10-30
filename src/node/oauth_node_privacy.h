#ifndef WOLFRAM_OAUTH_NODE_PRIVACY_H
#define WOLFRAM_OAUTH_NODE_PRIVACY_H

/*
 * Public privacy notice for the hosted OAuth node. Served at /privacy and /.
 * Keep every claim in step with oauth_node.c: the node has no persistence,
 * keeps the library logger off and prints only its startup line.
 *
 * Split into two objects because ISO C only guarantees string literals up to
 * 4095 bytes. oauth_node.c joins them once at startup.
 */
static const char kOAuthNodePrivacyHtmlHead[] =
    "<!doctype html><html lang='en-GB'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Privacy notice — oauth.croft.click</title>"
    "<style>"
    "body{font:17px/1.6 system-ui,sans-serif;color:#111;background:#fff;"
    "max-width:46rem;margin:0 auto;padding:2rem 1.25rem 4rem}"
    "h1{font-size:2rem;line-height:1.2;margin:0 0 .25rem}"
    "h2{font-size:1.2rem;margin:2rem 0 .5rem}"
    ".lead{font-size:1.05rem}.meta{color:#666;margin-top:0}"
    "table{border-collapse:collapse;width:100%;margin:.75rem 0}"
    "th,td{border:1px solid #ddd;padding:.55rem;text-align:left;"
    "vertical-align:top}th{background:#f6f6f6}"
    "a{color:#0645ad}li{margin:.3rem 0}"
    "footer{border-top:1px solid #ddd;margin-top:2.5rem;padding-top:1rem;"
    "color:#666;font-size:.95rem}"
    "</style></head><body><main><h1>Privacy notice</h1>"
    "<p class='lead'>The OAuth node at <strong>oauth.croft.click</strong> "
    "helps "
    "console apps sign in to your AT Protocol account. It is designed to keep "
    "no personal data at rest.</p>"
    "<p class='meta'>Last updated: 29 October 2026</p>"

    "<h2>Who is responsible</h2>"
    "<!--email_off--><p>Ewan Croft, United Kingdom, is the data controller "
    "for this node. Contact "
    "<a href='mailto:contact@ewancroft.uk'>contact@ewancroft.uk</a> for any "
    "privacy question or request.</p><!--/email_off-->"

    "<h2>What this node does</h2>"
    "<p>It opens your PDS's own sign-in page, receives the OAuth callback and "
    "proxies requests from the client you are signing in. It does not host "
    "your profile, posts or messages; those stay on your PDS.</p>"

    "<h2>Data we process</h2><ul>"
    "<li>Your handle and DID, so the node can find your PDS and confirm the "
    "account that signs in.</li>"
    "<li>Your PDS URL and OAuth session (access token, refresh token and DPoP "
    "key), so the node can make requests for you.</li>"
    "<li>A short-lived pairing code, authorization URL and OAuth state while "
    "the browser sign-in is open.</li>"
    "<li>Network metadata needed to deliver a request, such as your IP address "
    "and user agent, in memory. The node does not log or store it.</li></ul>"
    "<p>We never ask for your PDS password. You enter it only on your PDS's "
    "page. We do not request special category data, and we do not use your "
    "data for advertising, profiling or automated decisions.</p>"

    "<h2>Why we use it</h2>"
    "<p>We process it because you asked us to connect your account and make "
    "requests for you (UK GDPR Article 6(1)(b)), and to keep the service "
    "secure and reliable (legitimate interests, Article 6(1)(f)).</p>"

    "<h2>Retention</h2>"
    "<p><strong>The node has no database, state file or volume.</strong> "
    "Everything below is held in process memory only.</p>";

static const char kOAuthNodePrivacyHtmlTail[] =
    "<table><thead><tr><th>Data</th><th>Retention</th></tr></thead><tbody>"
    "<tr><td>Pairing code, handle, DID, authorization URL and OAuth state</td>"
    "<td>About 10 minutes by default, until the sign-in completes or is "
    "cancelled, or until the node restarts.</td></tr>"
    "<tr><td>Handle, DID, PDS URL, access token, refresh token and DPoP "
    "key</td>"
    "<td>Up to 30 days, until the session expires or the node restarts.</td>"
    "</tr><tr><td>Node logs</td><td>None. The node prints only one startup "
    "line "
    "with no personal data.</td></tr>"
    "<tr><td>Cookies and analytics</td><td>None set by the node. There is no "
    "analytics, tracking pixel or advertising script.</td></tr>"
    "<tr><td>IP address and request metadata</td><td>Processed in memory to "
    "serve the request; not logged or stored by the node.</td></tr>"
    "</tbody></table>"
    "<p>Restarting the node, for example during a deploy, deletes every "
    "session immediately. There is nothing to export, back up or erase from "
    "disk.</p>"

    "<h2>Who else sees it</h2><ul>"
    "<li><strong>Cloudflare</strong> terminates TLS and carries traffic to "
    "this hostname. It processes connection metadata and requests under its "
    "own privacy notice, and may process data outside the UK under its "
    "transfer safeguards.</li>"
    "<li><strong>Your PDS and authorization server</strong> receive the OAuth "
    "request and hold your account data. Their privacy notice applies.</li>"
    "<li><strong>Slingshot</strong> (<code>slingshot.micocosm.blue</code>) "
    "resolves your handle and DID while starting the sign-in, with standard "
    "AT Protocol resolution (DNS and <code>bsky.social</code>) as a fallback. "
    "They see the handle or DID you supplied.</li></ul>"
    "<p>We do not sell or rent personal data, and we do not share it with ad "
    "networks or analytics providers.</p>"

    "<h2>Security</h2>"
    "<p>Traffic uses HTTPS. Sessions and keys stay in process memory, the "
    "container runs with a read-only root filesystem and no state volume, core "
    "dumps are disabled, and the library logger is off. No service can promise "
    "perfect security, but there is no stored credential database to steal.</p>"

    "<h2>Your rights</h2>"
    "<p>Under the UK GDPR you can ask for access to your personal data, "
    "correction, erasure, restriction of processing, object to processing "
    "based on legitimate interests, and data portability. Because the node "
    "does not store data, we usually cannot retrieve a record to correct or "
    "erase; we can confirm that no stored record is held, and a restart clears "
    "all live sessions. You can also complain to the Information "
    "Commissioner's Office at <a href='https://ico.org.uk/'>ico.org.uk</a> or "
    "on 0303 123 1113.</p>"

    "<h2>Children</h2>"
    "<p>This service is not directed at children under 13, the minimum age for "
    "an AT Protocol account.</p>"

    "<h2>Changes</h2>"
    "<p>If this notice changes materially, the new version and its date will "
    "be published here.</p>"

    "<footer>This notice covers <strong>oauth.croft.click</strong> only. The "
    "console app you use and your PDS have their own privacy notices.</footer>"
    "</main></body></html>";

#endif /* WOLFRAM_OAUTH_NODE_PRIVACY_H */
