from enum import StrEnum


class ShredMode(StrEnum):
    clear = "clear"
    purge = "purge"
    destroy = "destroy"

    @property
    def passes(self) -> int:
        return {ShredMode.clear: 1, ShredMode.purge: 3, ShredMode.destroy: 35}[self]


class JobStatus(StrEnum):
    queued = "queued"
    shredding = "shredding"
    verifying = "verifying"
    completed = "completed"
    failed = "failed"
