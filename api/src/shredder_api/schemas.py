from datetime import datetime
from uuid import UUID

from pydantic import BaseModel, ConfigDict, Field

from shredder_api.enums import JobStatus, ShredMode


class UploadResponse(BaseModel):
    job_id: UUID
    status: JobStatus
    original_filename: str
    file_size_bytes: int
    sha256_pre: str = Field(min_length=64, max_length=64)
    mode: ShredMode


class JobDetail(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: UUID
    original_filename: str
    file_size_bytes: int | None
    sha256_pre: str
    mode: str
    passes: int | None
    status: str
    progress_pct: int
    elapsed_ms: int | None
    error_message: str | None
    created_at: datetime
    completed_at: datetime | None


class RejectionReason(BaseModel):
    detail: str
    code: str
