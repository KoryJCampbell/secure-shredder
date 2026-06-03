import hashlib
import uuid
from dataclasses import dataclass
from pathlib import Path

import aiofiles
from fastapi import UploadFile

# Reject obvious executables — milestone 3 blocklist.
# The shredder will accept anything not on this list (PDFs, images, text,
# archives, unknowns) because the whole point is to destroy arbitrary files.
_FORBIDDEN_PREFIXES: tuple[tuple[bytes, str], ...] = (
    (b"\x7fELF", "ELF executable"),
    (b"MZ", "Windows PE executable"),
    (b"#!", "shell script with shebang"),
    (b"\xfe\xed\xfa\xce", "Mach-O 32-bit"),
    (b"\xfe\xed\xfa\xcf", "Mach-O 64-bit"),
    (b"\xce\xfa\xed\xfe", "Mach-O 32-bit (reverse)"),
    (b"\xcf\xfa\xed\xfe", "Mach-O 64-bit (reverse)"),
    (b"\xca\xfe\xba\xbe", "Mach-O universal / Java class"),
)

_SNIFF_SIZE = 16
_CHUNK_SIZE = 64 * 1024  # 64 KiB


class IntakeError(Exception):
    def __init__(self, message: str, code: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True, slots=True)
class IntakeResult:
    temp_path: Path
    size_bytes: int
    sha256_hex: str


def _check_magic(prefix: bytes) -> None:
    for magic, label in _FORBIDDEN_PREFIXES:
        if prefix.startswith(magic):
            raise IntakeError(
                f"executable rejected by magic-byte sniff: {label}",
                code="executable_rejected",
            )


async def receive_upload(
    upload: UploadFile, inbox_dir: Path, max_bytes: int
) -> IntakeResult:
    """Stream the upload to disk, hashing as we go, with a hard size cap.

    Raises IntakeError on magic-byte rejection or size overflow.
    """
    inbox_dir.mkdir(parents=True, exist_ok=True)
    temp_path = inbox_dir / str(uuid.uuid4())

    digest = hashlib.sha256()
    written = 0
    first_chunk_checked = False

    try:
        async with aiofiles.open(temp_path, "wb") as out:
            while True:
                chunk = await upload.read(_CHUNK_SIZE)
                if not chunk:
                    break

                if not first_chunk_checked:
                    _check_magic(chunk[:_SNIFF_SIZE])
                    first_chunk_checked = True

                written += len(chunk)
                if written > max_bytes:
                    raise IntakeError(
                        f"upload exceeds {max_bytes} byte cap",
                        code="upload_too_large",
                    )

                digest.update(chunk)
                await out.write(chunk)
    except IntakeError:
        # Best-effort cleanup of the partial temp file.
        try:
            temp_path.unlink(missing_ok=True)
        except OSError:
            pass
        raise

    return IntakeResult(
        temp_path=temp_path,
        size_bytes=written,
        sha256_hex=digest.hexdigest(),
    )
