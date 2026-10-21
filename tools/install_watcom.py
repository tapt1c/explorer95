"""Explicit, hash-pinned Linux x64 build-tool setup; never run by make itself."""
import hashlib
from pathlib import Path
import tarfile
import tempfile
import urllib.request

URL = "https://github.com/open-watcom/open-watcom-v2/releases/download/2026-10-01-Build/ow-snapshot.tar.xz"
SHA256 = "e6aa1b1e40ac8bbf97658d2c70fff8a4242d6ca4a1c60806f2baa5317083d4fe"
destination = Path(__file__).resolve().parents[1] / ".tools/watcom"
wanted = {"binl64/wasm", "binl64/wlink", "binl64/wcc386"}
with tempfile.TemporaryDirectory(prefix="ex95-watcom-") as temp:
    archive = Path(temp) / "watcom.tar.xz"
    urllib.request.urlretrieve(URL, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise ValueError("Open Watcom archive SHA256 mismatch")
    with tarfile.open(archive, "r:xz") as source:
        found = set()
        for member in source:
            name = member.name.removeprefix("./")
            if name not in wanted:
                continue
            if not member.isfile():
                raise ValueError("Expected regular tool file")
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.extractfile(member).read())
            target.chmod(0o755)
            found.add(name)
        if found != wanted:
            raise ValueError("Missing Open Watcom build tools")
print(destination)
