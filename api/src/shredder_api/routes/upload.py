from typing import Annotated

from fastapi import APIRouter, Depends, File, Form, HTTPException, UploadFile, status
from sqlalchemy.ext.asyncio import AsyncSession

from shredder_api.config import Settings, get_settings
from shredder_api.db import get_session
from shredder_api.enums import JobStatus, ShredMode
from shredder_api.models import Job
from shredder_api.schemas import UploadResponse
from shredder_api.services.file_intake import IntakeError, receive_upload

router = APIRouter(tags=["upload"])


@router.post(
    "/upload",
    response_model=UploadResponse,
    status_code=status.HTTP_201_CREATED,
    responses={
        413: {"description": "Upload exceeds size cap"},
        415: {"description": "Magic-byte sniff rejected the file"},
    },
)
async def upload(
    file: Annotated[UploadFile, File()],
    session: Annotated[AsyncSession, Depends(get_session)],
    settings: Annotated[Settings, Depends(get_settings)],
    mode: Annotated[ShredMode, Form()] = ShredMode.purge,
    reason: Annotated[str | None, Form()] = None,
    operator: Annotated[str | None, Form()] = None,
) -> UploadResponse:
    if not file.filename:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail="missing filename",
        )

    try:
        intake = await receive_upload(
            upload=file,
            inbox_dir=settings.inbox_dir,
            max_bytes=settings.max_upload_bytes,
        )
    except IntakeError as exc:
        http_status = (
            status.HTTP_413_REQUEST_ENTITY_TOO_LARGE
            if exc.code == "upload_too_large"
            else status.HTTP_415_UNSUPPORTED_MEDIA_TYPE
        )
        raise HTTPException(status_code=http_status, detail=str(exc)) from exc

    job = Job(
        original_filename=file.filename,
        temp_path=str(intake.temp_path),
        file_size_bytes=intake.size_bytes,
        sha256_pre=intake.sha256_hex,
        mode=mode.value,
        passes=mode.passes,
        reason=reason,
        operator=operator,
        status=JobStatus.queued.value,
    )
    session.add(job)
    await session.commit()
    await session.refresh(job)

    return UploadResponse(
        job_id=job.id,
        status=JobStatus(job.status),
        original_filename=job.original_filename,
        file_size_bytes=job.file_size_bytes or 0,
        sha256_pre=job.sha256_pre,
        mode=ShredMode(job.mode),
    )
