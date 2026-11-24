#!/usr/bin/env python3
"""Source-integrity and real ELF dependency provenance regression tests."""
from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools/dependencies"
sys.path.insert(0, str(TOOLS))
import verify_sources as sources
import audit_build as build_audit


class SourceFixture(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="vsgraysim_dependency_tools_")
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name).resolve()
        self.source = self.repo / "third_party/example"
        self.source.mkdir(parents=True)
        (self.source / "core.cpp").write_text("int engine_version() { return 1; }\n")
        (self.source / "LICENSE").write_text("Test fixture license\n")
        self.manifest = self.repo / "third_party/manifests/example.json"
        self.lock_path = self.repo / "third_party/upstream.lock.json"
        self.lock = {
            "schema": sources.LOCK_SCHEMA, "version": 1,
            "dependencies": [{"name": "example", "source_directory": "third_party/example", "source_manifest": "third_party/manifests/example.json", "repository": "https://example.invalid/engine.git", "upstream_commit": "a" * 40, "upstream_version": "1.0", "archive_sha256": "b" * 64}],
        }
        sources.atomic_json(self.lock_path, self.lock)
        sources.initialize_manifests(self.repo, sources.load_lock(self.repo, self.lock_path))

    def run_tool(self, *arguments):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return sources.main(["--repo", str(self.repo), *arguments])

    def test_verify_is_read_only_and_detects_content_and_added_files(self):
        original_manifest, original_lock = self.manifest.read_bytes(), self.lock_path.read_bytes()
        self.assertEqual(self.run_tool(), 0)
        (self.source / "core.cpp").write_text("int engine_version() { return 2; }\n")
        self.assertEqual(self.run_tool(), 1)
        self.assertEqual(self.run_tool("--allow-local-changes"), 0)
        (self.source / "local_patch.cpp").write_text("// reviewed developer addition\n")
        self.assertEqual(self.run_tool(), 1)
        self.assertEqual(self.run_tool("--allow-local-changes"), 0)
        self.assertEqual(self.manifest.read_bytes(), original_manifest)
        self.assertEqual(self.lock_path.read_bytes(), original_lock)

    def test_missing_files_fail_even_in_developer_mode(self):
        (self.source / "LICENSE").unlink()
        self.assertEqual(self.run_tool(), 1)
        self.assertEqual(self.run_tool("--allow-local-changes"), 1)

    def test_deliberate_refresh_retains_import_provenance_and_records_reason(self):
        old_tree = sources.read_json(self.manifest)["tree_sha256"]
        (self.source / "core.cpp").write_text("int engine_version() { return 7; }\n")
        self.assertEqual(self.run_tool("--refresh", "example"), 2)
        self.assertEqual(self.run_tool("--record-local-changes", "example", "--reason", "Fix allocator alignment"), 0)
        updated = sources.read_json(self.lock_path)["dependencies"][0]
        original = self.lock["dependencies"][0]
        for field in ("repository", "upstream_commit", "upstream_version", "archive_sha256"):
            self.assertEqual(updated[field], original[field])
        self.assertEqual(updated["local_changes"][0]["previous_tree_sha256"], old_tree)
        self.assertEqual(updated["local_changes"][0]["reason"], "Fix allocator alignment")
        self.assertNotEqual(updated["local_changes"][0]["tree_sha256"], old_tree)
        self.assertEqual(self.run_tool(), 0)

    def test_initializer_never_overwrites_a_differing_manifest(self):
        original = self.manifest.read_bytes()
        (self.source / "core.cpp").write_text("// unreviewed mutation\n")
        self.assertEqual(self.run_tool("--initialize-manifests"), 2)
        self.assertEqual(self.manifest.read_bytes(), original)

    def test_lock_and_manifest_paths_cannot_escape_repository(self):
        for field in ("source_directory", "source_manifest"):
            with self.subTest(field=field):
                lock = json.loads(json.dumps(self.lock))
                lock["dependencies"][0][field] = "../outside"
                sources.atomic_json(self.lock_path, lock)
                self.assertEqual(self.run_tool(), 2)
        sources.atomic_json(self.lock_path, self.lock)
        manifest = sources.read_json(self.manifest)
        manifest["files"]["../outside"] = "c" * 64
        manifest["tree_sha256"] = sources.tree_digest(manifest["files"])
        sources.atomic_json(self.manifest, manifest)
        self.assertEqual(self.run_tool(), 2)

    def test_external_symlinks_are_rejected_in_every_mode(self):
        outside = self.repo / "outside.h"
        outside.write_text("// outside the dependency\n")
        (self.source / "escaped.h").symlink_to(outside)
        self.assertEqual(self.run_tool(), 2)
        self.assertEqual(self.run_tool("--allow-local-changes"), 2)
        self.assertEqual(self.run_tool("--refresh", "example", "--reason", "unsafe"), 2)

    def test_internal_symlinks_are_manifested_without_following_directory_cycles(self):
        (self.source / "alias.cpp").symlink_to("core.cpp")
        (self.source / "cycle").symlink_to(".", target_is_directory=True)
        self.assertEqual(self.run_tool("--refresh", "example", "--reason", "Add source aliases"), 0)
        manifest = sources.read_json(self.manifest)
        self.assertEqual(manifest["files"]["alias.cpp"], {"type": "symlink", "target": "core.cpp"})
        self.assertEqual(manifest["files"]["cycle"], {"type": "symlink", "target": "."})
        self.assertEqual(self.run_tool(), 0)

    def test_corrupt_manifest_digest_is_not_a_developer_edit(self):
        manifest = sources.read_json(self.manifest)
        manifest["files"]["core.cpp"] = "d" * 64
        sources.atomic_json(self.manifest, manifest)
        self.assertEqual(self.run_tool(), 2)
        self.assertEqual(self.run_tool("--allow-local-changes"), 2)

    def test_post_configure_source_changes_are_rechecked_by_build_audit(self):
        initial = sources.verify_dependency(self.repo, self.lock["dependencies"][0])
        report = {"verification": {"schema": "vsgraysim.source.verification", "version": 1, "ok": True, "dependencies": [initial]}}
        self.assertTrue(build_audit.verify_current_sources(self.repo, report)["strict_clean"])
        (self.source / "core.cpp").write_text("// changed after CMake configured\n")
        with self.assertRaises(build_audit.AuditError):
            build_audit.verify_current_sources(self.repo, report)
        development = build_audit.verify_current_sources(self.repo, report, True)
        self.assertFalse(development["strict_clean"])
        self.assertEqual(development["modified_components"], ["example"])
        self.assertEqual(development["changed_since_configuration"], ["example"])
        # Reviewing the patch does not retroactively replace configure provenance.
        self.assertEqual(self.run_tool("--refresh", "all", "--reason", "Review after-configure patch"), 0)
        with self.assertRaisesRegex(build_audit.AuditError, "reconfigure"):
            build_audit.verify_current_sources(self.repo, report)


class LinkProvenance(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="vsgraysim_link_audit_")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.build = self.root / "build"
        self.build.mkdir()
        self.library = self.build / "engine/libvsg.so.17"
        self.library.parent.mkdir()
        self.library.write_bytes(b"local artifact")
        self.artifacts = {self.library.resolve()}

    def test_absolute_and_relative_link_inputs_resolve_to_declared_artifact(self):
        link = self.library.parent / "libvsg.so"
        link.symlink_to(self.library.name)
        self.assertEqual(build_audit.audit_link_tokens([str(link)], self.build, self.build, self.artifacts), 1)
        self.assertEqual(build_audit.audit_link_tokens(["engine/libvsg.so"], self.build, self.build, self.artifacts), 1)

    def test_soname_and_output_are_not_library_inputs(self):
        args = ["-shared", "-Wl,-soname,libvsg.so.17", "-o", str(self.library)]
        self.assertEqual(build_audit.audit_link_tokens(args, self.build, self.build, self.artifacts), 0)
        with self.assertRaises(build_audit.AuditError):
            build_audit.audit_link_tokens(args + ["-Wl,-l:libvsg.so.17"], self.build, self.build, self.artifacts)

    def test_same_basename_from_external_prefix_and_symlink_escape_fail(self):
        foreign = self.root / "installed/libvsg.so.17"
        foreign.parent.mkdir()
        foreign.write_bytes(b"preinstalled engine")
        with self.assertRaises(build_audit.AuditError):
            build_audit.audit_link_tokens([str(foreign)], self.build, self.build, self.artifacts)
        link = self.library.parent / "libvsg.so"
        link.symlink_to(foreign)
        with self.assertRaises(build_audit.AuditError):
            build_audit.audit_link_tokens([str(link)], self.build, self.build, self.artifacts)

    def test_response_files_cannot_hide_external_engine(self):
        response = self.build / "libraries.rsp"
        response.write_text("/opt/old-rocky/lib/librocky.so.52\n")
        with self.assertRaises(build_audit.AuditError):
            build_audit.audit_link_tokens(["@libraries.rsp"], self.build, self.build, self.artifacts)
        with self.assertRaises(build_audit.AuditError):
            build_audit.audit_link_tokens(["-lvsg"], self.build, self.build, self.artifacts)

    def test_unresolved_runtime_library_is_a_failure(self):
        with self.assertRaises(build_audit.AuditError):
            build_audit.parse_ldd("librocky.so.52 => not found\n", self.build / "app/example")

    def test_stale_runtime_fallback_prefix_is_rejected_before_it_is_loaded(self):
        prefix = self.root / "old-install/lib"
        prefix.mkdir(parents=True)
        (prefix / "librocky.so.52").write_bytes(b"old engine")
        with self.assertRaisesRegex(build_audit.AuditError, "preinstalled engine"):
            build_audit.audit_runtime_search_paths(["$ORIGIN/../engine:" + str(prefix)], self.build / "app/example", self.build)
        with self.assertRaises(build_audit.AuditError):
            build_audit.audit_runtime_search_paths([".:$ORIGIN"], self.build / "app/example", self.build)


@unittest.skipUnless(sys.platform == "linux" and all(shutil.which(tool) for tool in ("cc", "readelf", "ldd")), "requires Linux ELF build tools")
class RealRuntimeProvenance(unittest.TestCase):
    def test_real_loader_closure_detects_foreign_library_despite_local_report(self):
        with tempfile.TemporaryDirectory(prefix="vsgraysim_elf_audit_") as temporary:
            root = Path(temporary).resolve()
            build = root / "build"
            local = build / "engine/libvsg.so"
            foreign = root / "installed/libvsg.so"
            local.parent.mkdir(parents=True)
            foreign.parent.mkdir()
            (build / "app").mkdir()
            code = root / "engine.c"
            code.write_text("int engine_version(void) { return 17; }\n")
            main = root / "main.c"
            main.write_text("extern int engine_version(void); int main(void) { return engine_version() == 17 ? 0 : 1; }\n")
            for path in (local, foreign):
                subprocess.run(["cc", "-shared", "-fPIC", str(code), "-Wl,-soname,libvsg.so", "-o", str(path)], check=True, capture_output=True)
            executable = build / "app/consumer"
            dependencies = [{"artifact": str(local)}]

            def link_against(directory):
                subprocess.run(["cc", str(main), "-L" + str(directory), "-lvsg", "-Wl,-rpath," + str(directory), "-o", str(executable)], check=True, capture_output=True)

            link_against(local.parent)
            result = build_audit.elf_dependencies(build, dependencies)
            consumer = next(item for item in result if item["binary"] == str(executable))
            self.assertEqual(consumer["managed_libraries"], [{"soname": "libvsg.so", "path": str(local)}])
            link_against(foreign.parent)
            with self.assertRaisesRegex(build_audit.AuditError, "preinstalled engine"):
                build_audit.elf_dependencies(build, dependencies)
            # An absolute SONAME bypasses RPATH. The real loader resolution must
            # still be inspected, rather than approving an engine basename.
            subprocess.run(["cc", "-shared", "-fPIC", str(code), "-Wl,-soname," + str(foreign), "-o", str(foreign)], check=True, capture_output=True)
            subprocess.run(["cc", str(main), str(foreign), "-o", str(executable)], check=True, capture_output=True)
            with self.assertRaisesRegex(build_audit.AuditError, "outside this build"):
                build_audit.elf_dependencies(build, dependencies)


if __name__ == "__main__":
    unittest.main()
