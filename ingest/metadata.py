"""yt-dlp info_dict to ytvideo row fields."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import date, datetime
from typing import Any

from ingest.audioset import Segment

def _upload_date(raw: Any) -> date | None:
    if raw is None:
        return None
    s = str(raw).strip()
    return datetime.strptime(s, "%Y%m%d").date() if len(s) == 8 and s.isdigit() else None

@dataclass(frozen=True, slots=True)
class YTFields:
    ytid: str
    section_start: float
    section_end: float
    title: str | None = None
    description: str | None = None
    webpage_url: str | None = None
    uploader: str | None = None
    uploader_id: str | None = None
    channel: str | None = None
    channel_id: str | None = None
    license: str | None = None
    location: str | None = None
    upload_date: date | None = None
    duration_s: float | None = None
    view_count: int | None = None
    like_count: int | None = None
    tags: list[str] | None = None
    categories: list[str] | None = None
    extractor: str | None = None
    availability: str | None = None
    raw_info: dict[str, Any] | None = None

    @classmethod
    def of(cls, info: dict[str, Any], seg: Segment, raw_info: dict[str, Any]) -> "YTFields":
        duration = info.get("duration")
        views = info.get("view_count")
        likes = info.get("like_count")
        return cls(
            ytid=seg.ytid,
            section_start=seg.start_s,
            section_end=seg.end_s,
            title=info.get("title"),
            description=info.get("description"),
            webpage_url=info.get("webpage_url"),
            uploader=info.get("uploader"),
            uploader_id=info.get("uploader_id"),
            channel=info.get("channel"),
            channel_id=info.get("channel_id"),
            license=info.get("license"),
            location=info.get("location"),
            upload_date=_upload_date(info.get("upload_date")),
            duration_s=float(duration) if duration is not None else None,
            view_count=int(views) if views is not None else None,
            like_count=int(likes) if likes is not None else None,
            tags=list(info["tags"]) if info.get("tags") else None,
            categories=list(info["categories"]) if info.get("categories") else None,
            extractor=info.get("extractor"),
            availability=info.get("availability"),
            raw_info=raw_info,
        )
