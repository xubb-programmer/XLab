#!/usr/bin/env python3
"""Refresh an existing offline ARM64 archive with current web code and map files."""

from __future__ import annotations

import argparse
import copy
import os
import tarfile
import time
from pathlib import Path, PurePosixPath


ROOT = "intelligent-cooperative-server-offline"


def add_file(archive: tarfile.TarFile, name: str, source: Path, mode: int | None = None) -> None:
    info = tarfile.TarInfo(name)
    info.size = source.stat().st_size
    info.mtime = source.stat().st_mtime
    info.mode = mode if mode is not None else 0o644
    info.type = tarfile.REGTYPE
    with source.open("rb") as data:
        archive.addfile(info, data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_archive", type=Path)
    parser.add_argument("bundle_directory", type=Path)
    parser.add_argument("output_archive", type=Path)
    args = parser.parse_args()

    source_archive = args.source_archive.resolve()
    bundle = args.bundle_directory.resolve()
    output = args.output_archive.resolve()
    if not source_archive.is_file():
        parser.error(f"source archive not found: {source_archive}")
    if not (bundle / "web" / "server").is_dir() or not (bundle / "web" / "src").is_dir():
        parser.error(f"bundle directory is missing web/server or web/src: {bundle}")
    if output.exists() or output == source_archive:
        parser.error(f"output archive already exists or matches source archive: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)

    replacements: dict[str, Path] = {
        f"{ROOT}/README.md": bundle / "README.md",
        f"{ROOT}/PROJECT-README.md": bundle / "PROJECT-README.md",
        f"{ROOT}/SHA256SUMS": bundle / "SHA256SUMS",
    }
    for section in ("server", "src"):
        root = bundle / "web" / section
        for path in root.rglob("*"):
            if path.is_file():
                relative_path = path.relative_to(bundle / "web").as_posix()
                replacements[f"{ROOT}/web/{relative_path}"] = path

    map_root = bundle / "web" / "dist" / "offline-maps" / "combined-theaters"
    if not (map_root / "tiles.db").is_file() or not (map_root / "manifest.json").is_file():
        parser.error(f"combined map is incomplete: {map_root}")
    for path in map_root.rglob("*"):
        if path.is_file():
            relative_path = path.relative_to(bundle / "web").as_posix()
            replacements[f"{ROOT}/web/{relative_path}"] = path

    temporary = output.with_name(output.name + ".tmp")
    if temporary.exists():
        parser.error(f"temporary output already exists: {temporary}")
    seen: set[str] = set()
    replaced: set[str] = set()
    copied = 0

    try:
        with tarfile.open(source_archive, "r:gz") as source, tarfile.open(
            temporary, "w:gz", compresslevel=1, format=tarfile.PAX_FORMAT
        ) as target:
            for member in source:
                name = member.name
                normalized = name.rstrip("/")
                is_source_tree = normalized.startswith(f"{ROOT}/web/server/") or normalized.startswith(
                    f"{ROOT}/web/src/"
                )
                replacement = replacements.get(normalized)

                if is_source_tree and member.isfile() and replacement is None:
                    continue
                if replacement is not None and member.isfile():
                    info = copy.copy(member)
                    info.size = replacement.stat().st_size
                    info.mtime = replacement.stat().st_mtime
                    with replacement.open("rb") as data:
                        target.addfile(info, data)
                    replaced.add(normalized)
                else:
                    target.addfile(member, source.extractfile(member) if member.isfile() else None)
                seen.add(normalized)
                copied += 1

            required_dirs: set[str] = set()
            for name in replacements:
                parent = PurePosixPath(name).parent
                while str(parent) != ".":
                    required_dirs.add(parent.as_posix())
                    parent = parent.parent
            for name in sorted(required_dirs, key=lambda item: (item.count("/"), item)):
                if name in seen:
                    continue
                info = tarfile.TarInfo(name + "/")
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                info.mtime = time.time()
                target.addfile(info)
                seen.add(name)

            for name, path in sorted(replacements.items()):
                if name in seen:
                    continue
                add_file(target, name, path)
                seen.add(name)

        os.replace(temporary, output)
    except Exception:
        temporary.unlink(missing_ok=True)
        raise

    print(f"Updated archive: {output}")
    print(f"Replaced files: {len(replaced)}; archive size: {output.stat().st_size} bytes")


if __name__ == "__main__":
    main()
