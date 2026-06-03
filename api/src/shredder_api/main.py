from pathlib import Path
from typing import Annotated

from fastapi import Depends, FastAPI
from fastapi.responses import FileResponse
from pydantic import BaseModel
from sqlalchemy import text
from sqlalchemy.ext.asyncio import AsyncSession

from shredder_api import __version__
from shredder_api.db import get_session
from shredder_api.routes import jobs, upload

STATIC_DIR = Path(__file__).parent / "static"

app = FastAPI(
    title="Shredder API",
    version=__version__,
    description="NIST 800-88 file sanitization orchestration layer.",
)

app.include_router(upload.router)
app.include_router(jobs.router)


@app.get("/", include_in_schema=False)
async def index() -> FileResponse:
    return FileResponse(STATIC_DIR / "index.html")


class HealthResponse(BaseModel):
    status: str
    version: str
    db: str


@app.get("/health", response_model=HealthResponse)
async def health(
    session: Annotated[AsyncSession, Depends(get_session)],
) -> HealthResponse:
    db_ok = False
    try:
        result = await session.execute(text("SELECT 1"))
        db_ok = result.scalar_one() == 1
    except Exception:
        db_ok = False

    return HealthResponse(
        status="ok" if db_ok else "degraded",
        version=__version__,
        db="connected" if db_ok else "unreachable",
    )
