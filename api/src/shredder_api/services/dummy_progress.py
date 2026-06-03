import asyncio
import json
import time
from collections.abc import AsyncIterator
from uuid import UUID

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from shredder_api.enums import JobStatus
from shredder_api.models import Job


def _sse(event: dict) -> bytes:
    return f"data: {json.dumps(event, separators=(',', ':'))}\n\n".encode()


async def stream_dummy_progress(
    session: AsyncSession, job_id: UUID, pause_seconds: float = 0.3
) -> AsyncIterator[bytes]:
    """Drive a Job through the state machine with simulated progress events.

    Milestone 3 placeholder: yields the same JSON shape the C++ engine emits
    via --json-progress, so the client (and milestone 4 wiring) can stay the
    same when the real engine takes over.
    """
    job = (await session.execute(select(Job).where(Job.id == job_id))).scalar_one()
    total_passes = job.passes or 1
    total_bytes = job.file_size_bytes or 0

    yield _sse({"event": "queued", "job_id": str(job_id)})

    job.status = JobStatus.shredding.value
    await session.commit()
    yield _sse({"event": "shredding", "job_id": str(job_id)})

    start = time.monotonic()
    for pass_index in range(1, total_passes + 1):
        pattern = "random" if pass_index != 2 else "ones"
        yield _sse(
            {
                "event": "pass_started",
                "pass": pass_index,
                "total_passes": total_passes,
                "pattern": pattern,
            }
        )
        await asyncio.sleep(pause_seconds)
        for written in (total_bytes // 2, total_bytes):
            yield _sse(
                {
                    "event": "progress",
                    "pass": pass_index,
                    "bytes_written": written,
                    "total_bytes": total_bytes,
                    "speed_mbps": 250.0,
                }
            )
            await asyncio.sleep(pause_seconds / 2)
        yield _sse({"event": "pass_finished", "pass": pass_index})

        job.progress_pct = int(pass_index / total_passes * 100)
        await session.commit()

    elapsed_ms = int((time.monotonic() - start) * 1000)
    job.status = JobStatus.completed.value
    job.elapsed_ms = elapsed_ms
    from sqlalchemy import func

    job.completed_at = (
        await session.execute(select(func.now()))
    ).scalar_one()
    await session.commit()

    yield _sse(
        {
            "event": "summary",
            "job_id": str(job_id),
            "status": JobStatus.completed.value,
            "passes": total_passes,
            "size": total_bytes,
            "sha256": job.sha256_pre,
            "elapsed_ms": elapsed_ms,
        }
    )
