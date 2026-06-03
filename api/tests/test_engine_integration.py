"""Integration test that exercises the real C++ engine through the SSE route.

Skipped if the engine binary hasn't been built. Build it with:
    cmake -S engine -B engine/build && cmake --build engine/build
"""
import hashlib
import json
from pathlib import Path

import pytest
from httpx import ASGITransport, AsyncClient

from shredder_api.main import app

ENGINE_BINARY = (
    Path(__file__).resolve().parent.parent.parent / "engine" / "build" / "shredder"
)


pytestmark = pytest.mark.skipif(
    not ENGINE_BINARY.exists(),
    reason=f"engine binary not built at {ENGINE_BINARY}",
)


@pytest.mark.asyncio
async def test_real_engine_shreds_file_and_emits_summary():
    payload = b"top secret memo " * 1024  # 16 KiB
    expected_hash = hashlib.sha256(payload).hexdigest()

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        upload_resp = await client.post(
            "/upload",
            files={"file": ("classified.txt", payload, "text/plain")},
            data={"mode": "purge", "operator": "integration-test"},
        )
        assert upload_resp.status_code == 201
        job_id = upload_resp.json()["job_id"]

        events: list[dict] = []
        async with client.stream("GET", f"/jobs/{job_id}/stream") as stream:
            assert stream.status_code == 200
            async for line in stream.aiter_lines():
                if line.startswith("data: "):
                    events.append(json.loads(line[len("data: ") :]))

        detail = (await client.get(f"/jobs/{job_id}")).json()

    event_types = [e["event"] for e in events]
    assert event_types[0] == "queued"
    assert "shredding" in event_types
    assert "pass_started" in event_types
    assert event_types.count("pass_finished") == 3, event_types

    summary = next(e for e in events if e["event"] == "summary")
    # The summary comes from the C++ engine: sha256 was computed in C++,
    # so it should match an independent Python hash of the original payload.
    assert summary["status"] == "completed"
    assert summary["sha256"] == expected_hash
    assert summary["passes"] == 3
    assert summary["size"] == len(payload)

    assert detail["status"] == "completed"
    assert detail["progress_pct"] == 100
    assert detail["completed_at"] is not None


@pytest.mark.asyncio
async def test_engine_missing_binary_marks_failed(monkeypatch):
    from shredder_api.config import get_settings

    settings = get_settings()
    monkeypatch.setattr(settings, "engine_binary", Path("/does/not/exist/shredder"))

    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as client:
        upload_resp = await client.post(
            "/upload",
            files={"file": ("doc.txt", b"hello world", "text/plain")},
            data={"mode": "clear"},
        )
        job_id = upload_resp.json()["job_id"]

        events: list[dict] = []
        async with client.stream("GET", f"/jobs/{job_id}/stream") as stream:
            async for line in stream.aiter_lines():
                if line.startswith("data: "):
                    events.append(json.loads(line[len("data: ") :]))

        detail = (await client.get(f"/jobs/{job_id}")).json()

    assert any(e["event"] == "failed" for e in events)
    assert detail["status"] == "failed"
    assert "not found" in (detail["error_message"] or "").lower()
