from typing import Annotated
from uuid import UUID

from fastapi import APIRouter, Depends, HTTPException, status
from fastapi.responses import StreamingResponse
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from shredder_api.db import get_session
from shredder_api.enums import JobStatus
from shredder_api.models import Job
from shredder_api.schemas import JobDetail
from shredder_api.services.dummy_progress import stream_dummy_progress

router = APIRouter(tags=["jobs"], prefix="/jobs")


@router.get("/{job_id}", response_model=JobDetail)
async def get_job(
    job_id: UUID,
    session: Annotated[AsyncSession, Depends(get_session)],
) -> Job:
    job = (
        await session.execute(select(Job).where(Job.id == job_id))
    ).scalar_one_or_none()
    if job is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="job not found"
        )
    return job


@router.get("/{job_id}/stream")
async def stream_job(
    job_id: UUID,
    session: Annotated[AsyncSession, Depends(get_session)],
) -> StreamingResponse:
    job = (
        await session.execute(select(Job).where(Job.id == job_id))
    ).scalar_one_or_none()
    if job is None:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND, detail="job not found"
        )
    if job.status not in {JobStatus.queued.value, JobStatus.shredding.value}:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"job already in terminal state: {job.status}",
        )

    return StreamingResponse(
        stream_dummy_progress(session, job_id),
        media_type="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no",
            "Connection": "keep-alive",
        },
    )
