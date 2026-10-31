#!/usr/bin/env python3
"""Make an offline-buildable source archive (system dependencies still required)."""
import hashlib
from pathlib import Path
import tarfile
ROOT = Path(__file__).resolve().parent.parent
version = "0.1.0"
out = ROOT / "dist" / f"icontray-{version}.tar.gz"
out.parent.mkdir(exist_ok=True)
excluded = {"build", "dist", ".git", "__pycache__", ".venv"}
with tarfile.open(out, "w:gz") as archive:
    for path in sorted(ROOT.rglob("*")):
        rel = path.relative_to(ROOT)
        if any(part in excluded or part.startswith("build-") for part in rel.parts):
            continue
        if path.is_file():
            archive.add(path, arcname=str(Path(f"icontray-{version}") / rel), recursive=False)
checksum = hashlib.sha256(out.read_bytes()).hexdigest()
out.with_suffix(out.suffix + ".sha256").write_text(f"{checksum}  {out.name}\n")
print(out)
print("SHA256:", checksum)
