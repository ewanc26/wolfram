#!/usr/bin/env python3
"""Selftest for tools/release-check.py. Every rule gets a passing and a failing case."""
import contextlib
import importlib.util
import io
import os
import pathlib
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent


def load():
    spec = importlib.util.spec_from_file_location(
        "release_check", HERE / "release-check.py"
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


release = load()


class TagAndVersion(unittest.TestCase):
    def test_tag_and_version_agree(self):
        version, problems = release.resolve_version("v1.2.3", "1.2.3")
        self.assertEqual((version, problems), ("1.2.3", []))

    def test_version_defaults_to_the_tag(self):
        version, problems = release.resolve_version("v1.2.3", "")
        self.assertEqual((version, problems), ("1.2.3", []))

    def test_tag_must_have_the_v_prefix_and_three_parts(self):
        for tag in ("1.2.3", "v1.2", "v1.2.3.4", "v1.2.3-rc1", "vevil"):
            with self.subTest(tag=tag):
                version, problems = release.resolve_version(tag, "")
                self.assertEqual(version, "")
                self.assertTrue(problems)

    def test_version_must_match_the_tag(self):
        version, problems = release.resolve_version("v1.2.3", "1.2.4")
        self.assertEqual(version, "1.2.4")
        self.assertIn("names version 1.2.3, not 1.2.4", problems[0])

    def test_version_must_be_three_numeric_parts(self):
        version, problems = release.resolve_version("v1.2.3", "1.2.x")
        self.assertTrue(version)
        self.assertTrue(problems)


class Changelog(unittest.TestCase):
    def write(self, text):
        handle = tempfile.NamedTemporaryFile("w", suffix=".md", delete=False)
        handle.write(text)
        handle.close()
        self.addCleanup(lambda: pathlib.Path(handle.name).unlink(missing_ok=True))
        return handle.name

    def test_section_with_an_entry_passes(self):
        path = self.write("## [1.2.3] - 2026-10-30\n\n### Fixed\n\n- A thing. (#1)\n")
        self.assertEqual(release.changelog_problems(path, "1.2.3"), [])

    def test_missing_section_fails(self):
        path = self.write("## [Unreleased]\n\n- A thing.\n")
        self.assertIn("has no `## [1.2.3]` section", release.changelog_problems(path, "1.2.3")[0])

    def test_empty_section_fails(self):
        path = self.write("## [1.2.3] - 2026-10-30\n\n## [1.2.2] - 2026-10-29\n\n- Old.\n")
        self.assertIn("empty", release.changelog_problems(path, "1.2.3")[0])

    def test_headings_without_content_fail(self):
        path = self.write("## [1.2.3]\n\n### Fixed\n\n")
        self.assertIn("no content outside its headings", release.changelog_problems(path, "1.2.3")[0])

    def test_missing_file_fails(self):
        self.assertIn("cannot read", release.changelog_problems("/no/such/CHANGELOG.md", "1.2.3")[0])


class Ancestry(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.remote = os.path.join(self.tmp.name, "remote.git")
        self.work = os.path.join(self.tmp.name, "work")
        self.git(self.tmp.name, "init", "--bare", "-q", self.remote)
        self.git(self.tmp.name, "clone", "-q", self.remote, self.work)
        self.git(self.work, "config", "user.email", "test@example.invalid")
        self.git(self.work, "config", "user.name", "test")
        pathlib.Path(self.work, "CHANGELOG.md").write_text(
            "## [1.2.3] - 2026-10-30\n\n- A thing. (#1)\n", encoding="utf-8"
        )
        self.git(self.work, "add", "CHANGELOG.md")
        self.git(self.work, "commit", "-q", "-m", "release: 1.2.3")
        self.git(self.work, "branch", "-M", "main")
        self.git(self.work, "push", "-q", "-u", "origin", "main")

    def git(self, cwd, *args):
        return subprocess.run(
            ["git", *args], cwd=cwd, check=True, capture_output=True, text=True
        )

    def run_check(self, *extra):
        old = os.getcwd()
        os.chdir(self.work)
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                return release.main(
                    [
                        "--tag",
                        "v1.2.3",
                        "--version",
                        "1.2.3",
                        "--changelog",
                        "CHANGELOG.md",
                        "--sha",
                        "HEAD",
                        "--skip-checks",
                        *extra,
                    ]
                )
        finally:
            os.chdir(old)

    def test_commit_on_main_passes(self):
        self.assertEqual(self.run_check(), 0)

    def test_commit_off_main_is_refused(self):
        self.git(self.work, "switch", "-q", "-c", "side")
        pathlib.Path(self.work, "side").write_text("x", encoding="utf-8")
        self.git(self.work, "add", "side")
        self.git(self.work, "commit", "-q", "-m", "side")
        self.assertEqual(self.run_check("--sha", "HEAD"), 1)


class Checks(unittest.TestCase):
    def run_(self, status, conclusion="", name="CI gate"):
        return {"name": name, "status": status, "conclusion": conclusion}

    def test_success(self):
        state, _ = release.evaluate_checks(
            [self.run_("completed", "success")], ["CI gate"]
        )
        self.assertEqual(state, "success")

    def test_failure(self):
        state, detail = release.evaluate_checks(
            [self.run_("completed", "failure")], ["CI gate"]
        )
        self.assertEqual(state, "failure")
        self.assertIn("CI gate", detail)

    def test_pending(self):
        state, detail = release.evaluate_checks(
            [self.run_("in_progress")], ["CI gate"]
        )
        self.assertEqual(state, "pending")
        self.assertIn("running", detail)

    def test_missing_is_pending_until_the_deadline(self):
        state, detail = release.evaluate_checks([], ["host-tests"])
        self.assertEqual(state, "pending")
        self.assertIn("not created yet", detail)

    def test_one_successful_run_is_enough(self):
        state, _ = release.evaluate_checks(
            [
                self.run_("completed", "failure"),
                self.run_("completed", "success"),
            ],
            ["CI gate"],
        )
        self.assertEqual(state, "success")

    def test_every_required_name_is_checked(self):
        state, detail = release.evaluate_checks(
            [self.run_("completed", "success", "CI gate")],
            ["CI gate", "host-tests"],
        )
        self.assertEqual(state, "pending")
        self.assertIn("host-tests", detail)


if __name__ == "__main__":
    unittest.main()
