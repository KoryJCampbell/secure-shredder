import asyncio
import json
from collections.abc import AsyncIterator
from pathlib import Path
from uuid import UUID

from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from shredder_api.enums import JobStatus, ShredMode
from shredder_api.models import Job


def _sse(event: dict) -> bytes:
    return f"data: {json.dumps(event, separators=(',', ':'))}\n\n".encode()


async def _terminate(proc: asyncio.subprocess.Process) -> None:
    if proc.returncode is not None:
        return
    proc.terminate()
    try:
        await asyncio.wait_for(proc.wait(), timeout=5)
    except TimeoutError:
        proc.kill()
        await proc.wait()


async def stream_engine_progress(
    session: AsyncSession,
    job_id: UUID,
    engine_binary: Path,
    temp_path: Path,
    mode: ShredMode,
) -> AsyncIterator[bytes]:
    """Spawn the C++ shredder binary and re-emit its --json-progress lines
    as Server-Sent Events, while transitioning the Job row through the
    queued -> shredding -> completed | failed state machine.
    """
    job = (
        await session.execute(select(Job).where(Job.id == job_id))
    ).scalar_one()

    if not engine_binary.exists():
        job.status = JobStatus.failed.value
        job.error_message = f"engine binary not found at {engine_binary}"
        await session.commit()
        yield _sse({"event": "failed", "error": job.error_message})
        return

    yield _sse({"event": "queued", "job_id": str(job_id)})
    job.status = JobStatus.shredding.value
    await session.commit()
    yield _sse({"event": "shredding", "job_id": str(job_id)})

    # No shell, no string interpolation — list args only.
    proc = await asyncio.create_subprocess_exec(
        str(engine_binary),
        "--mode",
        mode.value,
        "--json-progress",
        str(temp_path),
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )

    summary_event: dict | None = None
    total_passes = job.passes or 1

    try:
        assert proc.stdout is not None
        async for raw in proc.stdout:
            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue

            if event.get("event") == "summary":
                summary_event = event

            yield f"data: {line}\n\n".encode()

            if event.get("event") == "pass_finished":
                pass_index = int(event.get("pass", 0))
                job.progress_pct = min(100, int(pass_index / total_passes * 100))
                await session.commit()
    except asyncio.CancelledError:
        await _terminate(proc)
        raise

    rc = await proc.wait()
    stderr_text = ""
    if proc.stderr is not None:
        stderr_text = (await proc.stderr.read()).decode("utf-8", errors="replace")

    if rc == 0 and summary_event is not None:
        job.status = JobStatus.completed.value
        job.elapsed_ms = summary_event.get("elapsed_ms")
        job.cpp_exit_code = 0
        job.progress_pct = 100
        job.completed_at = (
            await session.execute(select(func.now()))
        ).scalar_one()
    else:
        job.status = JobStatus.failed.value
        job.cpp_exit_code = rc
        job.error_message = stderr_text.strip() or f"engine exited with code {rc}"
        yield _sse(
            {
                "event": "failed",
                "exit_code": rc,
                "error": job.error_message,
            }
        )

    await session.commit()
