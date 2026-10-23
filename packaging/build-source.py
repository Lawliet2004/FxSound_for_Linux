#!/usr/bin/env python3
"""Archive the current source worktree and populated submodules, without build output."""
import argparse
from pathlib import Path
import subprocess
import tarfile

repo = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", nargs="?", default=str(repo / "dist/FxSound-linux-source.tar.gz"))
args = parser.parse_args()
output = Path(args.output).resolve()
output.parent.mkdir(parents=True, exist_ok=True)
paths = subprocess.check_output(
    ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=repo
).decode().split("\0")

def source_only(info):
    if ".git" in Path(info.name).parts:
        return None
    info.uid = info.gid = 0
    info.uname = info.gname = "root"
    return info

with tarfile.open(output, "w:gz") as archive:
    for relative in sorted(set(paths)):
        if not relative:
            continue
        source = repo / relative
        # Sparse-checkout entries need not exist. Directory gitlinks recurse
        # into the populated, pinned dependency source, excluding Git metadata.
        if source.exists() and source != output:
            archive.add(source, arcname="FxSound-linux-source/" + relative, filter=source_only)
print(output)
