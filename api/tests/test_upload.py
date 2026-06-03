import hashlib
import json

import pytest
from httpx import ASGITransport, AsyncClient

from shredder_api.main import app


@pytest.mark.asyncio
async def test_upload_pdf_returns_201_with_hash():
    payload = b"%PDF-1.4\n%fake pdf body for unit test\n" + b"x" * 1024
    expected_hash = hashlib.sha256(payload).hexdigest()

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.post(
            "/upload",
            files={"file": ("memo.pdf", payload, "application/pdf")},
            data={"mode": "purge", "operator": "test-runner"},
        )

    assert response.status_code == 201, response.text
    body = response.json()
    assert body["status"] == "queued"
    assert body["mode"] == "purge"
    assert body["sha256_pre"] == expected_hash
    assert body["original_filename"] == "memo.pdf"
    assert body["file_size_bytes"] == len(payload)


@pytest.mark.asyncio
async def test_upload_rejects_macho_executable():
    macho = b"\xcf\xfa\xed\xfe" + b"\x00" * 200

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.post(
            "/upload",
            files={"file": ("malware", macho, "application/octet-stream")},
            data={"mode": "clear"},
        )

    assert response.status_code == 415
    assert "Mach-O" in response.json()["detail"]


@pytest.mark.asyncio
async def test_upload_rejects_elf():
    elf = b"\x7fELF" + b"\x00" * 200

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        response = await client.post(
            "/upload",
            files={"file": ("a.out", elf, "application/octet-stream")},
        )

    assert response.status_code == 415
    assert "ELF" in response.json()["detail"]


@pytest.mark.asyncio
async def test_stream_yields_summary_event():
    payload = b"plain text content " * 200

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        upload_resp = await client.post(
            "/upload",
            files={"file": ("doc.txt", payload, "text/plain")},
            data={"mode": "clear"},
        )
        assert upload_resp.status_code == 201
        job_id = upload_resp.json()["job_id"]

        events: list[dict] = []
        async with client.stream("GET", f"/jobs/{job_id}/stream") as stream:
            assert stream.status_code == 200
            async for line in stream.aiter_lines():
                if line.startswith("data: "):
                    events.append(json.loads(line[len("data: ") :]))

    event_types = [e["event"] for e in events]
    assert event_types[0] == "queued"
    assert "shredding" in event_types
    assert event_types[-1] == "summary"
    summary = events[-1]
    assert summary["status"] == "completed"
    assert summary["passes"] == 1
