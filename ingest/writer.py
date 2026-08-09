from __future__ import annotations

from dataclasses import fields

from sqlalchemy import exists, select, update
from sqlalchemy.dialects.postgresql import insert as pg_insert
from sqlalchemy.orm import Session

from db.models import (CorpusRole, FreeSound, IngestLog, LabelRow, LabelSource, LogLevel, Media, MediaLabel, MediaOrigin, YtVideo)
from ingest.audioset import Segment
from ingest.config import LIBRARY_CODEC
from ingest.freesound import CuratedFile
from ingest.labels import Domain, Label
from ingest.metadata import YTFields
from ingest.ytdl import FlacInfo

def sync_vocabulary(session: Session, vocab: dict[str, Label]) -> dict[str, int]:
    """Insert or refresh the whole AudioSet vocabulary.
       Returns mid to labels.id."""

    stmt = pg_insert(LabelRow).values([{
        "key": label.mid,
        "name": label.name,
        "domain": label.domain,
        "source": LabelSource.audioset
    } for label in vocab.values()])
    stmt = stmt.on_conflict_do_update(
        index_elements=["key"], set_={
        "name": stmt.excluded.name,
        "domain": stmt.excluded.domain
        }
    ).returning(LabelRow.key, LabelRow.id)
    return {key: label_id for key, label_id in session.execute(stmt)}

def _upsert_media(session: Session, seg: Segment, audio: bytes, fmt: FlacInfo) -> int:
    stmt = pg_insert(Media).values(
        origin=MediaOrigin.youtube,
        corpus_role=CorpusRole.reference,
        external_id=seg.ytid,
        start_s=seg.start_s,
        end_s=seg.end_s,
        sample_rate=fmt.sample_rate,
        channels=fmt.channels,
        codec=LIBRARY_CODEC,
        audio=audio,
    )
    stmt = stmt.on_conflict_do_update(
        index_elements=["origin", "external_id", "start_s", "end_s"],
        set_={
        "sample_rate": stmt.excluded.sample_rate,
        "channels": stmt.excluded.channels,
        "codec": stmt.excluded.codec,
        "audio": stmt.excluded.audio
        }
    ).returning(Media.id)
    return session.execute(stmt).scalar_one()

def _upsert_ytvideo(session: Session, media_id: int, meta: YTFields) -> None:
    values = {"media_id": media_id} | {f.name: getattr(meta, f.name) for f in fields(meta)}
    stmt = pg_insert(YtVideo).values(**values)
    session.execute(
        stmt.on_conflict_do_update(
        index_elements=["media_id"],
        set_={k: stmt.excluded[k] for k in values if k != "media_id"}
        )
    )

def write(
    session: Session, seg: Segment, audio: bytes, fmt: FlacInfo, meta: YTFields,
    label_ids: dict[str, int]
) -> int:
    media_id = _upsert_media(session, seg, audio, fmt)
    _upsert_ytvideo(session, media_id, meta)
    for mid in seg.mids:
        session.execute(
            pg_insert(MediaLabel).values(media_id=media_id,
            label_id=label_ids[mid]).on_conflict_do_nothing()
        )
    session.add(
        IngestLog(
        media_id=media_id,
        origin=MediaOrigin.youtube,
        external_id=seg.ytid,
        start_s=seg.start_s,
        end_s=seg.end_s,
        level=LogLevel.info,
        message=f"ingested {len(audio)} bytes @ {fmt.sample_rate} Hz / {fmt.channels} ch"
        )
    )
    return media_id

def failed_segment_keys(session: Session, origin: MediaOrigin) -> set[tuple[str, float, float]]:
    """(external_id, start_s, end_s) for segments with a logged failure and no media row.

    A segment can be retried outside this script or by an earlier 
        --retry-failed run.
    succeed without its old error row ever being deleted — "has an error entry"
    alone would retry it forever. Only segments still missing a `media` row are
    truly pending.
    """

    resolved = exists().where(
        Media.origin == IngestLog.origin,
        Media.external_id == IngestLog.external_id,
        Media.start_s == IngestLog.start_s,
        Media.end_s == IngestLog.end_s,
    )
    stmt = (
        select(IngestLog.external_id, IngestLog.start_s,
        IngestLog.end_s).where(IngestLog.level == LogLevel.error, IngestLog.origin == origin,
        ~resolved).distinct()
    )
    return {(external_id, start_s, end_s) for external_id, start_s, end_s in session.execute(stmt)}

def log_failure(session: Session, seg: Segment, err: Exception) -> None:
    session.add(
        IngestLog(
        origin=MediaOrigin.youtube,
        external_id=seg.ytid,
        start_s=seg.start_s,
        end_s=seg.end_s,
        level=LogLevel.error,
        message=f"{type(err).__name__}: {err}"
        )
    )

def curated_domain_label(session: Session, domain: Domain) -> int:
    """The label curated media link to so `fetch_curated(domain)` finds them."""

    stmt = pg_insert(LabelRow).values(
        key=f"curated:{domain}", name=domain.capitalize(), domain=domain, source=None
    )
    stmt = stmt.on_conflict_do_update(
        index_elements=["key"], set_={"name": stmt.excluded.name, "domain": stmt.excluded.domain}
    ).returning(LabelRow.id)
    return session.execute(stmt).scalar_one()

def _upsert_curated_media(session: Session, cf: CuratedFile, audio: bytes, fmt: FlacInfo) -> int:
    existing = session.execute(
        select(Media.id).where(Media.origin == MediaOrigin.freesound,
        Media.external_id == cf.freesound_id)
    ).scalar_one_or_none()
    if existing is not None:
        session.execute(
            update(Media).where(Media.id == existing).values(
            sample_rate=fmt.sample_rate, channels=fmt.channels, codec=LIBRARY_CODEC, audio=audio)
        )
        return existing
    return session.execute(
        pg_insert(Media).values(
        origin=MediaOrigin.freesound,
        corpus_role=CorpusRole.curated,
        external_id=cf.freesound_id,
        start_s=None,
        end_s=None,
        sample_rate=fmt.sample_rate,
        channels=fmt.channels,
        codec=LIBRARY_CODEC,
        audio=audio,
        ).returning(Media.id)
    ).scalar_one()

def _upsert_freesound(session: Session, media_id: int, cf: CuratedFile) -> None:
    stmt = pg_insert(FreeSound).values(
        media_id=media_id, freesound_id=cf.freesound_id, username=cf.username,
        title=cf.title, webpage_url=cf.webpage_url
    )
    session.execute(
        stmt.on_conflict_do_update(
        index_elements=["media_id"],
        set_={
        "freesound_id": stmt.excluded.freesound_id,
        "username": stmt.excluded.username,
        "title": stmt.excluded.title,
        "webpage_url": stmt.excluded.webpage_url
        }
        )
    )

def write_curated(
    session: Session, cf: CuratedFile, audio: bytes, fmt: FlacInfo, label_id: int
) -> int:
    media_id = _upsert_curated_media(session, cf, audio, fmt)
    _upsert_freesound(session, media_id, cf)
    session.execute(
        pg_insert(MediaLabel).values(media_id=media_id,
        label_id=label_id).on_conflict_do_nothing()
    )
    session.add(
        IngestLog(
        media_id=media_id,
        origin=MediaOrigin.freesound,
        external_id=cf.freesound_id,
        level=LogLevel.info,
        message=f"ingested {len(audio)} bytes @ {fmt.sample_rate} Hz / {fmt.channels} ch (curated)"
        )
    )
    return media_id

def log_curated_failure(session: Session, cf: CuratedFile, err: Exception) -> None:
    session.add(
        IngestLog(
        origin=MediaOrigin.freesound,
        external_id=cf.freesound_id,
        level=LogLevel.error,
        message=f"{type(err).__name__}: {err}"
        )
    )
