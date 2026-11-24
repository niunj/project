#!/usr/bin/env python3
"""Verify vendored source snapshots without downloading or changing them.

The archive digest and upstream commit identify the import. The separate file
manifest identifies the reviewed local source tree, including deliberate patches.
Updating that manifest is an explicit, reason-bearing operation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile

LOCK_SCHEMA = "vsgraysim.third_party.lock"
MANIFEST_SCHEMA = "vsgraysim.source.manifest"
SHA256 = re.compile(r"^[0-9a-f]{64}$")
COMMIT = re.compile(r"^[0-9a-f]{40}(?:[0-9a-f]{24})?$")


class SourceError(ValueError):
    pass


def within(path: Path, parent: Path) -> bool:
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def relative_path(value: object, field: str) -> PurePosixPath:
    if not isinstance(value, str) or not value or "\\" in value:
        raise SourceError(f"{field}: expected a nonempty POSIX relative path")
    path = PurePosixPath(value)
    if path.is_absolute() or re.match(r"^[A-Za-z]:", value) or any(
        component in ("", ".", "..") for component in value.split("/")
    ):
        raise SourceError(f"{field}: unsafe relative path {value!r}")
    return path


def repository_path(repo: Path, value: object, field: str) -> Path:
    path = repo / relative_path(value, field)
    if not within(path.resolve(), repo):
        raise SourceError(f"{field}: path escapes repository: {value}")
    return path


def read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise SourceError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise SourceError(f"{path}: expected a JSON object")
    return value


def atomic_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix="." + path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as output:
            json.dump(value, output, indent=2, sort_keys=True, ensure_ascii=False)
            output.write("\n")
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def load_lock(repo: Path, lock_path: Path) -> dict:
    lock = read_json(lock_path)
    if lock.get("schema") != LOCK_SCHEMA or lock.get("version") != 1:
        raise SourceError("unsupported dependency lock schema/version")
    dependencies = lock.get("dependencies")
    if not isinstance(dependencies, list) or not dependencies:
        raise SourceError("dependency lock must contain nonempty dependencies")
    names, directories, manifests = set(), set(), set()
    vendor_root = (repo / "third_party").resolve()
    for entry in dependencies:
        if not isinstance(entry, dict):
            raise SourceError("dependency entries must be objects")
        name = entry.get("name")
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", name) or name in names:
            raise SourceError(f"invalid or duplicate dependency name: {name!r}")
        names.add(name)
        source = repository_path(repo, entry.get("source_directory"), f"{name}.source_directory")
        manifest = repository_path(repo, entry.get("source_manifest"), f"{name}.source_manifest")
        if not within(source.resolve(), vendor_root) or source.resolve() == vendor_root:
            raise SourceError(f"{name}: source must be a directory inside third_party")
        if source.is_symlink() or not source.is_dir():
            raise SourceError(f"{name}: source directory missing or symlinked: {source}")
        if within(manifest.resolve(), source.resolve()):
            raise SourceError(f"{name}: manifest must be outside its own source tree")
        if source.resolve() in directories or manifest.resolve() in manifests:
            raise SourceError(f"{name}: duplicate source directory or manifest")
        directories.add(source.resolve())
        manifests.add(manifest.resolve())
        digest = entry.get("archive_sha256")
        if not isinstance(digest, str) or not SHA256.fullmatch(digest):
            raise SourceError(f"{name}: archive_sha256 must be a real SHA-256 digest")
        commit = entry.get("upstream_commit")
        if commit is None:
            if entry.get("source_origin") != "local_snapshot":
                raise SourceError(f"{name}: unknown commit requires source_origin=local_snapshot")
        elif not isinstance(commit, str) or not COMMIT.fullmatch(commit):
            raise SourceError(f"{name}: upstream_commit must identify an exact commit")
        if not isinstance(entry.get("repository"), str) or not entry["repository"]:
            raise SourceError(f"{name}: repository provenance is missing")
    return lock


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_digest(files: dict) -> str:
    return hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")).hexdigest()


def snapshot(source: Path) -> dict:
    root = source.resolve()
    files = {}

    def visit(directory: Path) -> None:
        try:
            children = sorted(directory.iterdir(), key=lambda child: child.name)
        except OSError as error:
            raise SourceError(f"cannot enumerate source directory {directory}: {error}") from error
        for child in children:
            if child.name == ".git":
                continue
            name = child.relative_to(source).as_posix()
            relative_path(name, "source filename")
            if child.is_symlink():
                target = os.readlink(child)
                if Path(target).is_absolute() or re.match(r"^[A-Za-z]:", target):
                    raise SourceError(f"source symlink must be relocatable and relative: {name} -> {target}")
                if not within(child.resolve(), root):
                    raise SourceError(f"source symlink escapes dependency: {name} -> {target}")
                if not child.exists():
                    raise SourceError(f"source symlink is dangling: {name} -> {target}")
                files[name] = {"type": "symlink", "target": target}
            elif child.is_dir():
                visit(child)
            elif child.is_file():
                files[name] = hash_file(child)
            else:
                raise SourceError(f"unsupported source filesystem entry: {name}")

    visit(source)
    if not files:
        raise SourceError(f"source tree is empty: {source}")
    return files


def load_manifest(path: Path, name: str) -> dict:
    manifest = read_json(path)
    if manifest.get("schema") != MANIFEST_SCHEMA or manifest.get("version") != 1 or manifest.get("name") != name:
        raise SourceError(f"{name}: manifest identity/schema/version mismatch")
    files = manifest.get("files")
    if not isinstance(files, dict) or not files:
        raise SourceError(f"{name}: manifest files must be nonempty")
    for filename, entry in files.items():
        relative_path(filename, f"{name}.manifest filename")
        if isinstance(entry, str) and SHA256.fullmatch(entry):
            continue
        if isinstance(entry, dict) and set(entry) == {"type", "target"} and entry["type"] == "symlink" and isinstance(entry["target"], str):
            continue
        raise SourceError(f"{name}: invalid file digest or symlink entry: {filename}")
    if manifest.get("tree_sha256") != tree_digest(files):
        raise SourceError(f"{name}: manifest tree_sha256 is corrupt")
    return manifest


def verify_dependency(repo: Path, entry: dict, allow_changes: bool = False) -> dict:
    name = entry["name"]
    source = repository_path(repo, entry["source_directory"], f"{name}.source_directory")
    manifest = load_manifest(repository_path(repo, entry["source_manifest"], f"{name}.source_manifest"), name)
    actual = snapshot(source)
    expected = manifest["files"]
    missing = sorted(set(expected) - set(actual))
    added = sorted(set(actual) - set(expected))
    modified = sorted(filename for filename in set(actual) & set(expected) if actual[filename] != expected[filename])
    dirty = bool(missing or added or modified)
    return {
        "name": name, "source_directory": entry["source_directory"],
        "status": "dirty" if dirty else "clean", "ok": not missing and (allow_changes or not dirty),
        "missing": missing, "added": added, "modified": modified,
        "expected_tree_sha256": manifest["tree_sha256"], "actual_tree_sha256": tree_digest(actual),
        "file_count": len(actual),
    }


def initialize_manifests(repo: Path, lock: dict) -> None:
    pending = []
    for entry in lock["dependencies"]:
        path = repository_path(repo, entry["source_manifest"], "source_manifest")
        if path.exists():
            result = verify_dependency(repo, entry)
            if not result["ok"]:
                raise SourceError(f"{entry['name']}: existing manifest differs; use --record-local-changes with a reason")
            continue
        files = snapshot(repository_path(repo, entry["source_directory"], "source_directory"))
        pending.append((path, {"schema": MANIFEST_SCHEMA, "version": 1, "name": entry["name"], "files": files, "tree_sha256": tree_digest(files)}))
    for path, value in pending:
        atomic_json(path, value)


def record_changes(repo: Path, lock_path: Path, lock: dict, name: str, reason: str) -> None:
    if not reason.strip():
        raise SourceError("recording local changes requires a nonempty --reason")
    selected = [entry for entry in lock["dependencies"] if name == "all" or entry["name"] == name]
    if not selected:
        raise SourceError(f"unknown dependency {name!r}")
    pending = []
    for entry in selected:
        path = repository_path(repo, entry["source_manifest"], "source_manifest")
        old = load_manifest(path, entry["name"])
        files = snapshot(repository_path(repo, entry["source_directory"], "source_directory"))
        new_digest = tree_digest(files)
        if new_digest == old["tree_sha256"]:
            continue
        history = entry.setdefault("local_changes", [])
        if not isinstance(history, list):
            raise SourceError(f"{entry['name']}: local_changes must be an array")
        history.append({"reason": reason.strip(), "previous_tree_sha256": old["tree_sha256"], "tree_sha256": new_digest})
        pending.append((path, dict(old, files=files, tree_sha256=new_digest)))
    # Import commit/archive metadata stays intact. Both files are replaced only
    # after validation; an interrupted update fails closed on the next verify.
    for path, new_manifest in pending:
        atomic_json(path, new_manifest)
    if pending:
        atomic_json(lock_path, lock)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--lock", type=Path, default=Path("third_party/upstream.lock.json"))
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--initialize-manifests", action="store_true", help="explicitly initialize missing manifests; never overwrite a differing existing one")
    actions.add_argument("--record-local-changes", "--refresh", metavar="NAME", help="explicitly record reviewed local source edits for NAME (or all), retaining upstream provenance")
    parser.add_argument("--reason", default="")
    parser.add_argument("--allow-local-changes", action="store_true", help="report modified/added files as dirty without failing; missing/unsafe files still fail")
    parser.add_argument("--json", action="store_true", help="emit only the machine-readable verification report on stdout")
    parser.add_argument("--json-output", type=Path, help="write a verification report (does not modify source locks)")
    args = parser.parse_args(argv)
    try:
        repo = args.repo.resolve()
        lock_path = args.lock if args.lock.is_absolute() else repo / args.lock
        if not within(lock_path.resolve(), repo):
            raise SourceError("lock path must stay within the repository")
        lock = load_lock(repo, lock_path)
        if args.initialize_manifests:
            initialize_manifests(repo, lock)
        if args.record_local_changes:
            record_changes(repo, lock_path, lock, args.record_local_changes, args.reason)
        results = [verify_dependency(repo, entry, args.allow_local_changes) for entry in lock["dependencies"]]
        report = {"schema": "vsgraysim.source.verification", "version": 1, "ok": all(item["ok"] for item in results), "dirty": any(item["status"] == "dirty" for item in results), "allow_local_changes": args.allow_local_changes, "modified_components": [item["name"] for item in results if item["status"] == "dirty"], "dependencies": results}
        report["strict_clean"] = report["ok"] and not report["dirty"]
        if args.json_output:
            atomic_json(args.json_output, report)
        if args.json:
            print(json.dumps(report, sort_keys=True))
        else:
            for result in results:
                print(f"{result['status'].upper()}: {result['name']} ({result['file_count']} files, {result['actual_tree_sha256']})")
                for kind in ("missing", "modified", "added"):
                    for filename in result[kind][:20]:
                        print(f"  {kind}: {filename}")
                    if len(result[kind]) > 20:
                        print(f"  ... {len(result[kind]) - 20} more {kind} files")
        return 0 if report["ok"] else 1
    except (SourceError, OSError) as error:
        print(f"SOURCE VERIFICATION FAILED: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
