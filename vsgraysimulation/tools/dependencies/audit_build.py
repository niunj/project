#!/usr/bin/env python3
"""Audit a source-managed engine build using provenance and actual link inputs.

The audit verifies local source and artifact paths, compilation commands, link
commands, and (on Linux) the loader-resolved ELF dependency closure. It does not
claim byte-identical output across different compilers, SDKs, or machines.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys

from verify_sources import SourceError, atomic_json, load_lock, read_json, within, verify_dependency

REQUIRED_TARGETS = {"vsg", "vsgXchange", "vsgImGui", "vsgbCollision", "vsgbDynamics", "rocky"}
_MANAGED_NAMES = (
    r"vsgXchange|vsgImGui|vsgbCollision|vsgbDynamics|vsg|rocky|assimp|"
    r"BulletDynamics|BulletCollision|BulletSoftBody|BulletWorldImporter|BulletFileLoader|"
    r"ConvexDecomposition|LinearMath|usd_(?:usd|usdGeom|sdf|tf|gf|vt)"
)
MANAGED_LIBRARY = re.compile(rf"^(?:lib)?(?:{_MANAGED_NAMES})(?:d)?(?:\.a|\.so(?:\..*)?|\.dylib|\.lib|\.dll)$")
MANAGED_LFLAG = re.compile(rf"^-l(?:{_MANAGED_NAMES})(?:d)?$")
BUNDLED_LIBRARY = re.compile(r"^lib(?:gdal|geos(?:_c)?|IlmImf[^.]*|Iex[^.]*|Half[^.]*|Imath[^.]*|IlmThread[^.]*|z|zip|tbb|usd_\w+)\.so(?:\..*)?$")
LEGACY_OSG_LIBRARY = re.compile(r"^lib(?:osg\w*|OpenThreads)\.so(?:\..*)?$")



class AuditError(ValueError):
    pass


def absolute_path(value, field: str) -> Path:
    if not isinstance(value, str) or not Path(value).is_absolute():
        raise AuditError(f"{field}: expected an absolute path")
    return Path(value).resolve()


def managed_library(path: Path) -> bool:
    return bool(MANAGED_LIBRARY.fullmatch(path.name))


def require_local_library(path: Path, build: Path, context: str, artifacts=None) -> None:
    resolved = path.resolve()
    if not within(resolved, build):
        raise AuditError(f"{context}: managed engine library resolves outside this build: {path} -> {resolved}")
    if artifacts is not None and resolved not in artifacts:
        raise AuditError(f"{context}: managed library is not a declared source-built artifact: {resolved}")


def validate_report(repo: Path, build: Path, report: dict, require_artifacts: bool = True) -> list:
    if report.get("schema") != "vsgraysim.dependencies" or report.get("version") != 1 or report.get("mode") != "vendored":
        raise AuditError("dependency report is not a supported vendored build report")
    lock = load_lock(repo, repo / "third_party/upstream.lock.json")
    sources = {(repo / entry["source_directory"]).resolve() for entry in lock["dependencies"]}
    dependencies = report.get("dependencies")
    if not isinstance(dependencies, list) or not dependencies:
        raise AuditError("dependency report has no source-built targets")
    seen, checked = set(), []
    for entry in dependencies:
        if not isinstance(entry, dict) or entry.get("managed") is not True:
            raise AuditError("all dependency report targets must explicitly be managed source builds")
        target = entry.get("target")
        if not isinstance(target, str) or not target or target in seen:
            raise AuditError(f"invalid or duplicate target in dependency report: {target!r}")
        seen.add(target)
        source = absolute_path(entry.get("source_dir"), f"{target}.source_dir")
        binary = absolute_path(entry.get("binary_dir"), f"{target}.binary_dir")
        artifact = absolute_path(entry.get("artifact"), f"{target}.artifact")
        if source not in sources:
            raise AuditError(f"{target}: source directory is absent from the source lock: {source}")
        if not within(binary, build) or not within(artifact, binary):
            raise AuditError(f"{target}: binary directory/artifact is outside its declared local build directory")
        if require_artifacts and not artifact.is_file():
            raise AuditError(f"{target}: build artifact is missing: {artifact}")
        checked.append(dict(entry, source_dir=str(source), binary_dir=str(binary), artifact=str(artifact)))
    required = REQUIRED_TARGETS
    if report.get("rocky_enabled") is False:
        cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
        if "VSGRAYSIM_WITH_ROCKY:BOOL=OFF" not in cache.splitlines() or "rocky" in seen:
            raise AuditError("voxel-only report disagrees with configured Rocky backend")
        required = required - {"rocky"}
    missing = required - seen
    if missing:
        raise AuditError("dependency report omitted engine targets: " + ", ".join(sorted(missing)))
    return checked


def compile_sources(build: Path, dependencies: list, partial: bool = False) -> int:
    databases: dict[Path, tuple[set, int]] = {}
    for dependency in dependencies:
        path = absolute_path(dependency.get("compile_database", str(build / "compile_commands.json")),
                             f"{dependency['target']}.compile_database")
        if not within(path, build):
            raise AuditError(f"compilation database is outside this build: {path}")
        if path not in databases:
            if partial and not path.is_file() and not Path(dependency["artifact"]).is_file():
                continue
            try:
                commands = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError) as error:
                raise AuditError(f"cannot read {path}; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON: {error}") from error
            if not isinstance(commands, list) or not commands:
                raise AuditError(f"{path}: compilation database is empty or invalid")
            sources = set()
            for command in commands:
                if not isinstance(command, dict) or not isinstance(command.get("file"), str):
                    raise AuditError(f"{path}: invalid compilation database entry")
                directory = Path(command.get("directory", str(build)))
                source = Path(command["file"])
                sources.add((source if source.is_absolute() else directory / source).resolve())
            databases[path] = (sources, len(commands))
        root = Path(dependency["source_dir"])
        if not any(within(source, root) for source in databases[path][0]):
            raise AuditError(f"{dependency['target']}: no compilation command consumes its locked source tree: {root}")
    return sum(count for _, count in databases.values())


def expand_responses(tokens: list, directory: Path, depth=0) -> list:
    if depth > 8:
        raise AuditError("link response file nesting is excessive")
    expanded = []
    for token in tokens:
        if token.startswith("@"):
            path = Path(token[1:])
            if not path.is_absolute():
                path = directory / path
            try:
                content = shlex.split(path.read_text(encoding="utf-8"))
            except (OSError, ValueError) as error:
                raise AuditError(f"cannot inspect link response file {path}: {error}") from error
            expanded.extend(expand_responses(content, directory, depth + 1))
        else:
            expanded.append(token)
    return expanded


def audit_link_tokens(tokens: list, directory: Path, build: Path, artifacts: set) -> int:
    count = 0
    arguments = []
    for token in expand_responses(tokens, directory):
        if token.startswith("-Wl,"):
            arguments.extend(token[4:].split(","))
        elif token != "-Xlinker":
            arguments.append(token)
    # -soname names the shared object being produced. It is not an input
    # relative to the link working directory (which can differ from -o).
    args = iter(arguments)
    for token in args:
        if token in ("-soname", "--soname", "-o"):
            value = next(args, None)
            if not value:
                raise AuditError(f"missing operand for {token}")
            path = Path(value)
            if managed_library(path) and (token == "-o" or "/" in value):
                require_local_library(directory / path, build, token, artifacts)
            continue
        if MANAGED_LFLAG.fullmatch(token) or (token.startswith("-l:") and managed_library(Path(token[3:]))):
            raise AuditError(f"unqualified engine link input {token}; provenance requires an explicit local target/artifact")
        path = Path(token)
        if managed_library(path):
            require_local_library(directory / path, build, "link input", artifacts)
            count += 1
    return count


def link_commands(build: Path, dependencies: list, strict_artifacts: bool = True) -> tuple[int, int]:
    artifacts = {Path(entry["artifact"]).resolve() for entry in dependencies} if strict_artifacts else None
    files = sorted(build.rglob("link.txt"))
    if not files:
        # Ninja has no link.txt; its generated graph still contains the actual
        # linker inputs. Shell tokenization does not apply to Ninja syntax, so
        # require compile/link evidence via a Makefiles build for this audit.
        raise AuditError("no link.txt found; this audit currently requires the Unix Makefiles build preset")
    references = 0
    for path in files:
        try:
            tokens = shlex.split(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            raise AuditError(f"cannot inspect {path}: {error}") from error
        # CMakeFiles/<target>.dir/link.txt is executed in its containing build dir.
        references += audit_link_tokens(tokens, path.parent.parent.parent, build, artifacts)
    if references == 0:
        raise AuditError("link commands contain no declared local engine artifacts")
    return len(files), references


def run_readonly(command: list) -> str:
    environment = dict(os.environ)
    for name in ("LD_LIBRARY_PATH", "LD_PRELOAD", "LD_AUDIT"):
        environment.pop(name, None)
    environment["LC_ALL"] = "C"
    try:
        result = subprocess.run(command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise AuditError(f"cannot run {command[0]}: {error}") from error
    if result.returncode:
        raise AuditError(f"{command[0]} failed for {command[-1]}: {result.stderr.strip() or result.stdout.strip()}")
    return result.stdout


def parse_ldd(output: str, binary: Path) -> list:
    resolved = []
    for line in output.splitlines():
        if "=> not found" in line:
            raise AuditError(f"{binary}: unresolved runtime dependency: {line.strip()}")
        match = re.match(r"\s*(\S+)\s+=>\s+(/.*?)\s+\(0x[0-9a-fA-F]+\)", line)
        if match:
            resolved.append((match[1], Path(match[2])))
            continue
        direct = re.match(r"\s*(/.*?)\s+\(0x[0-9a-fA-F]+\)", line)
        if direct:
            resolved.append((direct[1], Path(direct[1])))
    return resolved


def audit_runtime_search_paths(rpaths: list, binary: Path, build: Path) -> None:
    for group in rpaths:
        for value in group.split(":"):
            if not value:
                # An empty search entry means cwd, which cannot be pinned by a
                # build report. CMake-generated strict builds need no such path.
                raise AuditError(f"{binary}: empty runtime search path depends on process working directory")
            expanded = value.replace("${ORIGIN}", str(binary.parent)).replace("$ORIGIN", str(binary.parent))
            if "$" in expanded:
                continue  # Remaining loader tokens are checked via actual ldd resolution.
            directory = Path(expanded)
            if not directory.is_absolute():
                raise AuditError(f"{binary}: relative runtime search path is not reproducible: {value}")
            if within(directory.resolve(), build) or not directory.is_dir():
                continue
            for candidate in directory.iterdir():
                if managed_library(candidate):
                    raise AuditError(f"{binary}: runtime search path contains preinstalled engine artifacts: {directory} ({candidate.name})")


def elf_candidates(build: Path, dependencies: list) -> list:
    candidates = {Path(entry["artifact"]) for entry in dependencies}
    for pattern in ("app/*", "env_modules/env_*", "gui/sim_*", "tests/sim_*"):
        candidates.update(build.glob(pattern))
    result = []
    for path in sorted(candidates):
        if not path.is_file() or path.is_symlink():
            continue
        try:
            with path.open("rb") as file:
                if file.read(4) == b"\x7fELF":
                    result.append(path)
        except OSError as error:
            raise AuditError(f"cannot inspect built binary {path}: {error}") from error
    return result


def elf_dependencies(build: Path, dependencies: list) -> list:
    if sys.platform != "linux":
        raise AuditError("ELF runtime audit is supported on Linux; use --report-only on another platform")
    if not shutil.which("readelf") or not shutil.which("ldd"):
        raise AuditError("runtime audit requires readelf and ldd")
    artifacts = {Path(entry["artifact"]).resolve() for entry in dependencies}
    binaries = elf_candidates(build, dependencies)
    if not binaries:
        raise AuditError("no built ELF files found")
    results = []
    for binary in binaries:
        dynamic = run_readonly(["readelf", "-d", str(binary)])
        needed = re.findall(r"\(NEEDED\).*?\[(.*?)\]", dynamic)
        if not needed:
            continue
        rpaths = re.findall(r"\((?:RPATH|RUNPATH)\).*?\[(.*?)\]", dynamic)
        audit_runtime_search_paths(rpaths, binary, build)
        resolved = parse_ldd(run_readonly(["ldd", str(binary)]), binary)
        managed = []
        project_zlib = "libz.so.1" in needed or any(
            name.startswith(("libgdal.so", "libassimp.so", "libIlmImf-", "libzip.so"))
            for name, _ in resolved
        )
        for name, path in resolved:
            if LEGACY_OSG_LIBRARY.fullmatch(name):
                raise AuditError(f"{binary}: obsolete OpenSceneGraph runtime dependency: {name}")
            # Qt exercises its own system zlib in targets that never load the
            # project data/asset libraries. Those Qt dependencies are external.
            if (build / "third_party/base/.complete").is_file() and BUNDLED_LIBRARY.fullmatch(name):
                if name != "libz.so.1" or project_zlib:
                    provider = (build / "third_party/install/openusd" if name.startswith("libusd_")
                                else build / "third_party/base")
                    require_local_library(path, provider, f"bundled dependency of {binary}")
            if managed_library(Path(name)) or managed_library(path):
                require_local_library(path, build, f"runtime dependency of {binary}", artifacts)
                managed.append({"soname": name, "path": str(path.resolve())})
        # Every managed DT_NEEDED entry must have an explicit loader resolution.
        for name in needed:
            if managed_library(Path(name)) and not any(item["soname"] == name for item in managed):
                raise AuditError(f"{binary}: cannot prove runtime resolution of {name}")
        results.append({"binary": str(binary), "needed": needed, "rpath": rpaths, "managed_libraries": managed})
    if not results:
        raise AuditError("no dynamically linked ELF files were inspected")
    return results


def verify_current_sources(repo: Path, report: dict, allow_changes: bool = False) -> dict:
    lock = load_lock(repo, repo / "third_party/upstream.lock.json")
    results = [verify_dependency(repo, entry, allow_changes) for entry in lock["dependencies"]]
    for result in results:
        if not result["ok"]:
            raise AuditError(f"{result['name']}: current vendor source differs from its reviewed manifest (missing={len(result['missing'])}, modified={len(result['modified'])}, added={len(result['added'])})")
    configured = report.get("verification")
    if not isinstance(configured, dict) or configured.get("schema") != "vsgraysim.source.verification" or configured.get("version") != 1 or configured.get("ok") is not True or not isinstance(configured.get("dependencies"), list):
        raise AuditError("dependency report lacks configure-time source verification; reconfigure this build")
    by_name = {entry.get("name"): entry for entry in configured["dependencies"] if isinstance(entry, dict)}
    mismatches = [entry["name"] for entry in results if (
        by_name.get(entry["name"], {}).get("actual_tree_sha256") != entry["actual_tree_sha256"] or
        by_name.get(entry["name"], {}).get("expected_tree_sha256") != entry["expected_tree_sha256"]
    )]
    if mismatches and not allow_changes:
        raise AuditError("vendor source changed after configuration; reconfigure before acceptance: " + ", ".join(mismatches))
    dirty = [entry["name"] for entry in results if entry["status"] == "dirty"]
    return {"schema": "vsgraysim.source.verification", "version": 1, "ok": True, "strict_clean": not dirty and not mismatches, "dirty": bool(dirty), "allow_local_changes": allow_changes, "modified_components": dirty, "changed_since_configuration": mismatches, "dependencies": results}


def audit(repo: Path, build: Path, report_path: Path, report_only: bool = False, allow_unbuilt: bool = False, allow_changes: bool = False) -> dict:
    report = read_json(report_path)
    dependencies = validate_report(repo, build, report, not (report_only or allow_unbuilt))
    verification = verify_current_sources(repo, report, allow_changes)
    pending = [entry["target"] for entry in dependencies if not Path(entry["artifact"]).is_file()]
    partial = allow_unbuilt and bool(pending)
    result = {"schema": "vsgraysim.dependencies.audit", "version": 1, "ok": True, "complete": not (report_only or partial), "strict_clean": verification["strict_clean"], "mode": "report-only" if report_only else "partial" if partial else "full", "pending_targets": pending, "build_dir": str(build), "verification": verification, "dependencies": dependencies}
    if not report_only:
        result["compilation_commands"] = compile_sources(build, dependencies, partial)
        result["link_commands"], result["managed_link_references"] = link_commands(build, dependencies, not partial)
        # A targeted build can legitimately leave the managed engine unbuilt.
        # Its source/link configuration is checked, but this is explicitly not
        # a successful artifact or runtime acceptance result.
        if not partial:
            result["elf"] = elf_dependencies(build, dependencies)
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path, nargs="?")
    parser.add_argument("--build-dir", type=Path, dest="build_option")
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--report", type=Path, help="default: BUILD_DIR/dependency-report.json")
    parser.add_argument("--report-only", action="store_true", help="validate configured provenance without claiming compiled/link/runtime verification")
    parser.add_argument("--allow-unbuilt", action="store_true", help="allow a targeted build to leave artifacts pending, explicitly without full runtime acceptance")
    parser.add_argument("--allow-local-changes", action="store_true", help="explicit development mode: report dirty/current source and configuration mismatch without claiming strict-clean acceptance")
    parser.add_argument("--json-output", type=Path)
    args = parser.parse_args(argv)
    try:
        if bool(args.build_dir) == bool(args.build_option):
            parser.error("provide one build directory, either positionally or with --build-dir")
        repo, build = args.repo.resolve(), (args.build_dir or args.build_option).resolve()
        report_path = args.report.resolve() if args.report else build / "dependency-report.json"
        result = audit(repo, build, report_path, args.report_only, args.allow_unbuilt, args.allow_local_changes)
        if args.json_output:
            atomic_json(args.json_output, result)
        for entry in result["dependencies"]:
            print(f"LOCAL: {entry['target']} <- {entry['source_dir']} -> {entry['artifact']}")
        if not result["strict_clean"]:
            print("DIRTY DEVELOPMENT BUILD: current source was inspected; this is not strict-clean acceptance")
        if result["mode"] == "report-only":
            print("PASS: configured source provenance only (artifacts and runtime were not inspected)")
        elif result["mode"] == "partial":
            print("PENDING: source/link configuration checked; artifact/runtime acceptance incomplete for: " + ", ".join(result["pending_targets"]))
        else:
            print(f"PASS: {result['compilation_commands']} compile commands, {result['link_commands']} link commands, {len(result['elf'])} ELF closures; all managed engine libraries resolve to this build")
        return 0
    except (AuditError, SourceError, OSError) as error:
        print(f"DEPENDENCY AUDIT FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
