import pytest_asyncio

from shredder_api.db import engine


@pytest_asyncio.fixture(autouse=True)
async def _dispose_engine_after_test():
    """Dispose the async engine after each test so pooled asyncpg connections
    don't outlive the per-test event loop (which raises RuntimeError on GC).
    """
    yield
    await engine.dispose()
