from __future__ import annotations

from datetime import date, datetime
from enum import StrEnum
from typing import Any, Optional

from sqlalchemy import (BigInteger,Boolean,CheckConstraint,Computed,Date,DateTime,Double,ForeignKey,Index,Integer,LargeBinary,SmallInteger,Text,UniqueConstraint,func)
from sqlalchemy.dialects.postgresql import ARRAY, JSONB
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column, relationship

class Base(DeclarativeBase):
    pass
class CorpusRole(StrEnum):
    reference = "reference"
    curated   = "curated"
class MediaOrigin(StrEnum):
    fsd50k    = "fsd50k"
    freesound = "freesound"
    youtube   = "youtube"
class LabelSource(StrEnum):
    audioset = "audioset"
    fsd50k   = "fsd50k"
class Domain(StrEnum):
    thunder = "thunder"
    rain    = "rain"
    wind    = "wind"
    ocean   = "ocean"
class LogLevel(StrEnum):
    debug = "debug"
    info  = "info"
    warn  = "warn"
    error = "error"
class RejectReason(StrEnum):
    noise       = "noise"
    unsort      = "unsort"
    bad_window  = "bad_window"
    bad_quality = "bad_quality"

class CorpusRoleRow(Base):
    __tablename__ = "corpus_role"
    key: Mapped[str] = mapped_column(Text, primary_key=True)
class MediaOriginRow(Base):
    __tablename__ = "media_origin"
    key: Mapped[str] = mapped_column(Text, primary_key=True)
class LabelSourceRow(Base):
    __tablename__ = "label_source"
    key: Mapped[str] = mapped_column(Text, primary_key=True)
class DomainRow(Base):
    __tablename__ = "domain"
    key: Mapped[str] = mapped_column(Text, primary_key=True)
class LogLevelRow(Base):
    __tablename__ = "log_level"
    key: Mapped[str] = mapped_column(Text, primary_key=True)
class RejectReasonRow(Base):
    __tablename__ = "reject_reason"
    key: Mapped[str] = mapped_column(Text, primary_key=True)

LOOKUPS: dict[str, type[StrEnum]] = {
    "corpus_role":   CorpusRole,
    "media_origin":  MediaOrigin,
    "label_source":  LabelSource,
    "domain":        Domain,
    "log_level":     LogLevel,
    "reject_reason": RejectReason,
}

def _fk(table: str) -> ForeignKey:
    return ForeignKey(f"{table}.key", onupdate="CASCADE")
def _CORPUS_ROLE_FK() -> ForeignKey:
    return _fk("corpus_role")
def _MEDIA_ORIGIN_FK() -> ForeignKey:
    return _fk("media_origin")
def _LABEL_SOURCE_FK() -> ForeignKey:
    return _fk("label_source")
def _DOMAIN_FK() -> ForeignKey:
    return _fk("domain")
def _LOG_LEVEL_FK() -> ForeignKey:
    return _fk("log_level")
def _REJECT_REASON_FK() -> ForeignKey:
    return _fk("reject_reason")

class LabelRow(Base):
    __tablename__ = "labels"
    id:     Mapped[int]           = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    key:    Mapped[str]           = mapped_column(Text, unique=True, nullable=False)
    name:   Mapped[str]           = mapped_column(Text, nullable=False)
    domain: Mapped[Optional[str]] = mapped_column(Text, _DOMAIN_FK())
    source: Mapped[Optional[str]] = mapped_column(Text, _LABEL_SOURCE_FK())

class Media(Base):
    __tablename__  = "media"
    __table_args__ = (UniqueConstraint("origin", "external_id", "start_s", "end_s"),)
    id:          Mapped[int]                   = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    origin:      Mapped[str]                   = mapped_column(Text, _MEDIA_ORIGIN_FK(), nullable=False)
    corpus_role: Mapped[str]                   = mapped_column(Text, _CORPUS_ROLE_FK(), nullable=False, default=CorpusRole.reference)
    external_id: Mapped[str]                   = mapped_column(Text, nullable=False)
    start_s:     Mapped[Optional[float]]       = mapped_column(Double)
    end_s:       Mapped[Optional[float]]       = mapped_column(Double)
    sample_rate: Mapped[int]                   = mapped_column(Integer, nullable=False)
    channels:    Mapped[int]                   = mapped_column(Integer, nullable=False)
    codec:       Mapped[str]                   = mapped_column(Text, nullable=False)
    audio:       Mapped[Optional[bytes]]       = mapped_column(LargeBinary)
    byte_size:   Mapped[Optional[int]]         = mapped_column(Integer, Computed("length(audio)", persisted=True))
    marks:       Mapped[Optional[list[float]]] = mapped_column(ARRAY(Double))
    notes:       Mapped[Optional[str]]         = mapped_column(Text)
    created_at:  Mapped[datetime]              = mapped_column(DateTime(timezone=True), server_default=func.now())
    ytvideo:     Mapped[Optional["YtVideo"]]   = relationship(back_populates="media", uselist=False)
    freesound:   Mapped[Optional["FreeSound"]] = relationship(back_populates="media", uselist=False)
    fsd50k:      Mapped[Optional["Fsd50k"]]    = relationship(back_populates="media", uselist=False)
    labels:      Mapped[list[LabelRow]]        = relationship(secondary="media_labels")

class MediaLabel(Base):
    __tablename__ = "media_labels"
    media_id: Mapped[int] = mapped_column(ForeignKey("media.id", ondelete="CASCADE"), primary_key=True)
    label_id: Mapped[int] = mapped_column(ForeignKey("labels.id", ondelete="CASCADE"), primary_key=True)

class YtVideo(Base):
    __tablename__   = "ytvideo"
    media_id:       Mapped[int]                      = mapped_column(ForeignKey("media.id", ondelete="CASCADE"), primary_key=True)
    ytid:           Mapped[str]                      = mapped_column(Text, nullable=False)
    title:          Mapped[Optional[str]]            = mapped_column(Text)
    description:    Mapped[Optional[str]]            = mapped_column(Text)
    webpage_url:    Mapped[Optional[str]]            = mapped_column(Text)
    uploader:       Mapped[Optional[str]]            = mapped_column(Text)
    uploader_id:    Mapped[Optional[str]]            = mapped_column(Text)
    channel:        Mapped[Optional[str]]            = mapped_column(Text)
    channel_id:     Mapped[Optional[str]]            = mapped_column(Text)
    license:        Mapped[Optional[str]]            = mapped_column(Text)
    location:       Mapped[Optional[str]]            = mapped_column(Text)
    upload_date:    Mapped[Optional[date]]           = mapped_column(Date)
    duration_s:     Mapped[Optional[float]]          = mapped_column(Double)
    view_count:     Mapped[Optional[int]]            = mapped_column(BigInteger)
    like_count:     Mapped[Optional[int]]            = mapped_column(BigInteger)
    tags:           Mapped[Optional[list[str]]]      = mapped_column(ARRAY(Text))
    categories:     Mapped[Optional[list[str]]]      = mapped_column(ARRAY(Text))
    section_start:  Mapped[Optional[float]]          = mapped_column(Double)
    section_end:    Mapped[Optional[float]]          = mapped_column(Double)
    extractor:      Mapped[Optional[str]]            = mapped_column(Text)
    availability:   Mapped[Optional[str]]            = mapped_column(Text)
    raw_info:       Mapped[Optional[dict[str, Any]]] = mapped_column(JSONB)
    ingested_at:    Mapped[datetime]                 = mapped_column(DateTime(timezone=True), server_default=func.now())
    media:          Mapped[Media]                    = relationship(back_populates="ytvideo")

class Fsd50k(Base):
    __tablename__ = "fsd50k"
    media_id: Mapped[int]                      = mapped_column(ForeignKey("media.id", ondelete="CASCADE"), primary_key=True)
    fname:    Mapped[str]                      = mapped_column(Text, unique=True, nullable=False)
    split:    Mapped[Optional[str]]            = mapped_column(Text)
    mids:     Mapped[Optional[list[str]]]      = mapped_column(ARRAY(Text))
    license:  Mapped[Optional[str]]            = mapped_column(Text)
    title:    Mapped[Optional[str]]            = mapped_column(Text)
    raw_meta: Mapped[Optional[dict[str, Any]]] = mapped_column(JSONB)
    media:    Mapped[Media]                    = relationship(back_populates="fsd50k")

class FreeSound(Base):
    __tablename__ = "freesound"
    media_id:     Mapped[int]                      = mapped_column(ForeignKey("media.id", ondelete="CASCADE"), primary_key=True)
    freesound_id: Mapped[str]                      = mapped_column(Text, unique=True, nullable=False)
    recording_id: Mapped[Optional[str]]            = mapped_column(Text)
    webpage_url:  Mapped[Optional[str]]            = mapped_column(Text)
    username:     Mapped[Optional[str]]            = mapped_column(Text)
    license:      Mapped[Optional[str]]            = mapped_column(Text)
    title:        Mapped[Optional[str]]            = mapped_column(Text)
    tags:         Mapped[Optional[list[str]]]      = mapped_column(ARRAY(Text))
    raw_meta:     Mapped[Optional[dict[str, Any]]] = mapped_column(JSONB)
    media:        Mapped[Media]                    = relationship(back_populates="freesound")

class IngestLog(Base):
    __tablename__ = "ingest_log"
    id:          Mapped[int]             = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    media_id:    Mapped[Optional[int]]   = mapped_column(ForeignKey("media.id", ondelete="SET NULL"))
    origin:      Mapped[Optional[str]]   = mapped_column(Text, _MEDIA_ORIGIN_FK())
    external_id: Mapped[Optional[str]]   = mapped_column(Text)
    start_s:     Mapped[Optional[float]] = mapped_column(Double)
    end_s:       Mapped[Optional[float]] = mapped_column(Double)
    level:       Mapped[str]             = mapped_column(Text, _LOG_LEVEL_FK(), nullable=False, default=LogLevel.info)
    message:     Mapped[str]             = mapped_column(Text, nullable=False)
    created_at:  Mapped[datetime]        = mapped_column(DateTime(timezone=True), server_default=func.now())

class Processed(Base):
    __tablename__  = "processed"
    __table_args__ = (CheckConstraint("parent_id IS NULL OR parent_id <> id", name="processed_parent_not_self"),CheckConstraint("parent_id IS NULL OR (start_s IS NOT NULL AND end_s IS NOT NULL AND end_s > start_s)",name="processed_child_window"),Index("processed_playable_idx","id",postgresql_where="audio IS NOT NULL"))
    id:                  Mapped[int]                            = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    parent_id:           Mapped[Optional[int]]                  = mapped_column(ForeignKey("processed.id", ondelete="CASCADE"))
    corpus_role:         Mapped[str]                            = mapped_column(Text, _CORPUS_ROLE_FK(), nullable=False, default=CorpusRole.curated)
    source_media_id:     Mapped[int]                            = mapped_column(ForeignKey("media.id", ondelete="RESTRICT"), nullable=False)
    source_origin:       Mapped[str]                            = mapped_column(Text, _MEDIA_ORIGIN_FK(), nullable=False)
    source_external_id:  Mapped[str]                            = mapped_column(Text, nullable=False)
    source_recording_id: Mapped[Optional[str]]                  = mapped_column(Text)
    source_title:        Mapped[Optional[str]]                  = mapped_column(Text)
    source_recorded_by:  Mapped[Optional[str]]                  = mapped_column(Text)
    source_url:          Mapped[Optional[str]]                  = mapped_column(Text)
    source_license:      Mapped[Optional[str]]                  = mapped_column(Text)
    domain:              Mapped[Optional[str]]                  = mapped_column(Text, _DOMAIN_FK())
    duration_s:          Mapped[float]                          = mapped_column(Double, nullable=False)
    size_bytes:          Mapped[int]                            = mapped_column(BigInteger, nullable=False)
    start_s:             Mapped[Optional[float]]                = mapped_column(Double)
    end_s:               Mapped[Optional[float]]                = mapped_column(Double)
    start_frame:         Mapped[Optional[int]]                  = mapped_column(BigInteger)
    end_frame:           Mapped[Optional[int]]                  = mapped_column(BigInteger)
    confidence:          Mapped[Optional[float]]                = mapped_column(Double)
    audio:               Mapped[Optional[bytes]]                = mapped_column(LargeBinary)
    codec:               Mapped[str]                            = mapped_column(Text, nullable=False, default="flac")
    stereo_invariance:   Mapped[float]                          = mapped_column(Double, nullable=False, default=0)
    noise_floor:         Mapped[Optional[float]]                = mapped_column(Double)
    peak:                Mapped[Optional[float]]                = mapped_column(Double)
    children:            Mapped[int]                            = mapped_column(Integer, nullable=False, default=0)
    notes:               Mapped[Optional[str]]                  = mapped_column(Text)
    processed_at:        Mapped[datetime]                       = mapped_column(DateTime(timezone=True), server_default=func.now())
    thunder:             Mapped[Optional["ProcessedThunder"]]   = relationship(back_populates="processed", uselist=False)
    wind:                Mapped[Optional["ProcessedWind"]]      = relationship(back_populates="processed", uselist=False)
    rain:                Mapped[Optional["ProcessedRain"]]      = relationship(back_populates="processed", uselist=False)
    ocean:               Mapped[Optional["ProcessedOcean"]]     = relationship(back_populates="processed", uselist=False)
    bands:               Mapped[list["SampleBand"]]             = relationship(back_populates="processed")
    adsr:                Mapped[Optional["Adsr"]]               = relationship(back_populates="processed", uselist=False)
    parametric_model:    Mapped[Optional["ParametricModel"]]    = relationship(back_populates="processed", uselist=False)

class ProcessedThunder(Base):
    __tablename__ = "processed_thunder"
    processed_id: Mapped[int]       = mapped_column(ForeignKey("processed.id", ondelete="CASCADE"), primary_key=True)
    sub_strikes:  Mapped[int]       = mapped_column(Integer, nullable=False, default=0)
    pre_crack:    Mapped[bool]      = mapped_column(Boolean, nullable=False, default=False)
    processed:    Mapped[Processed] = relationship(back_populates="thunder")

class ProcessedWind(Base):
    __tablename__ = "processed_wind"
    processed_id: Mapped[int]       = mapped_column(ForeignKey("processed.id", ondelete="CASCADE"), primary_key=True)
    intensity:    Mapped[int]       = mapped_column(Integer, nullable=False, default=0)
    processed:    Mapped[Processed] = relationship(back_populates="wind")

class ProcessedRain(Base):
    __tablename__ = "processed_rain"
    processed_id: Mapped[int]       = mapped_column(ForeignKey("processed.id", ondelete="CASCADE"), primary_key=True)
    loopable:     Mapped[bool]      = mapped_column(Boolean, nullable=False, default=False)
    processed:    Mapped[Processed] = relationship(back_populates="rain")

class ProcessedOcean(Base):
    __tablename__ = "processed_ocean"
    processed_id: Mapped[int]       = mapped_column(ForeignKey("processed.id", ondelete="CASCADE"), primary_key=True)
    processed:    Mapped[Processed] = relationship(back_populates="ocean")

class SampleBand(Base):
    __tablename__ = "sample_band"
    processed_id: Mapped[int]       = mapped_column(ForeignKey("processed.id", ondelete="CASCADE"), primary_key=True)
    band_index:   Mapped[int]       = mapped_column(SmallInteger, primary_key=True)
    amount:       Mapped[float]     = mapped_column(Double, nullable=False)
    f_lo_hz:      Mapped[float]     = mapped_column(Double, nullable=False)
    f_hi_hz:      Mapped[float]     = mapped_column(Double, nullable=False)
    processed:    Mapped[Processed] = relationship(back_populates="bands")

class Adsr(Base):
    __tablename__  = "adsr"
    processed_id:  Mapped[int]             = mapped_column(ForeignKey("processed.id", ondelete="CASCADE"), primary_key=True)
    onset_s:       Mapped[Optional[float]] = mapped_column(Double)
    peak_s:        Mapped[Optional[float]] = mapped_column(Double)
    attack_s:      Mapped[Optional[float]] = mapped_column(Double)
    decay_s:       Mapped[Optional[float]] = mapped_column(Double)
    sustain_s:     Mapped[Optional[float]] = mapped_column(Double)
    release_s:     Mapped[Optional[float]] = mapped_column(Double)
    sustain_level: Mapped[Optional[float]] = mapped_column(Double)
    processed:     Mapped[Processed]       = relationship(back_populates="adsr")

class ParametricModel(Base):
    __tablename__       = "parametric_model"
    processed_id:       Mapped[int]       = mapped_column(ForeignKey("processed.id", ondelete="CASCADE"), primary_key=True)
    resynth_similarity: Mapped[float]     = mapped_column(Double, nullable=False)
    analyzed_at:        Mapped[datetime]  = mapped_column(DateTime(timezone=True), server_default=func.now())
    processed:          Mapped[Processed] = relationship(back_populates="parametric_model")

class DetectorRun(Base):
    __tablename__     = "detector_run"
    id:               Mapped[int]                    = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    detector_name:    Mapped[str]                    = mapped_column(Text, nullable=False)
    detector_version: Mapped[str]                    = mapped_column(Text, nullable=False)
    domain:           Mapped[str]                    = mapped_column(Text, _DOMAIN_FK(), nullable=False)
    started_at:       Mapped[datetime]               = mapped_column(DateTime(timezone=True), server_default=func.now())
    finished_at:      Mapped[Optional[datetime]]     = mapped_column(DateTime(timezone=True))
    media_count:      Mapped[int]                    = mapped_column(Integer, nullable=False, default=0)
    candidate_count:  Mapped[int]                    = mapped_column(Integer, nullable=False, default=0)
    notes:            Mapped[Optional[str]]          = mapped_column(Text)

class MediaDetection(Base):
    __tablename__     = "media_detection"
    __table_args__    = (
        Index("media_detection_detector_idx", "detector_name", "detector_version", "domain"),
    )
    media_id:         Mapped[int]      = mapped_column(ForeignKey("media.id", ondelete="CASCADE"), primary_key=True)
    detector_name:    Mapped[str]      = mapped_column(Text, primary_key=True)
    detector_version: Mapped[str]      = mapped_column(Text, primary_key=True)
    detector_run_id:  Mapped[int]      = mapped_column(ForeignKey("detector_run.id", ondelete="CASCADE"), nullable=False)
    domain:           Mapped[str]      = mapped_column(Text, _DOMAIN_FK(), nullable=False)
    candidate_count:  Mapped[int]      = mapped_column(Integer, nullable=False, default=0)
    detected_at:      Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())

class Exemplar(Base):
    __tablename__    = "exemplar"
    __table_args__   = (CheckConstraint("end_s > start_s", name="exemplar_window"),)
    id:              Mapped[int]                   = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    source_media_id: Mapped[int]                   = mapped_column(ForeignKey("media.id", ondelete="CASCADE"), nullable=False)
    detector_run_id: Mapped[int]                   = mapped_column(ForeignKey("detector_run.id", ondelete="CASCADE"), nullable=False)
    start_s:         Mapped[float]                 = mapped_column(Double, nullable=False)
    end_s:           Mapped[float]                 = mapped_column(Double, nullable=False)
    confidence:      Mapped[Optional[float]]       = mapped_column(Double)
    approved:        Mapped[Optional[bool]]        = mapped_column(Boolean)
    reviewed_at:     Mapped[Optional[datetime]]    = mapped_column(DateTime(timezone=True))
    reject_reason:   Mapped[Optional[str]]         = mapped_column(Text, _REJECT_REASON_FK())
    carried_from:    Mapped[Optional[str]]         = mapped_column(Text)
    notes:           Mapped[Optional[str]]         = mapped_column(Text)
    features:        Mapped[list["ExemplarFeature"]] = relationship(back_populates="exemplar", cascade="all, delete-orphan")

class ExemplarFeature(Base):
    __tablename__    = "exemplar_feature"
    exemplar_id:     Mapped[int]      = mapped_column(ForeignKey("exemplar.id", ondelete="CASCADE"), primary_key=True)
    frame_index:     Mapped[int]      = mapped_column(Integer, primary_key=True)
    t_s:             Mapped[float]    = mapped_column(Double, nullable=False)
    low_band_db:     Mapped[float]    = mapped_column(Double, nullable=False)
    centroid_hz:     Mapped[float]    = mapped_column(Double, nullable=False)
    spread_hz:       Mapped[float]    = mapped_column(Double, nullable=False)
    dominance:       Mapped[float]    = mapped_column(Double, nullable=False)
    stereo_corr:     Mapped[float]    = mapped_column(Double, nullable=False)
    ms_width:        Mapped[float]    = mapped_column(Double, nullable=False)
    phase_coherence: Mapped[float]    = mapped_column(Double, nullable=False)
    stereo_balance:  Mapped[float]    = mapped_column(Double, nullable=False)
    exemplar:        Mapped[Exemplar] = relationship(back_populates="features")

class Log(Base):
    __tablename__ = "log"
    id:        Mapped[int]      = mapped_column(BigInteger, primary_key=True, autoincrement=True)
    run:       Mapped[str]      = mapped_column(Text, nullable=False, index=True)
    level:     Mapped[str]      = mapped_column(Text, _LOG_LEVEL_FK(), nullable=False)
    func:      Mapped[str]      = mapped_column(Text, nullable=False)
    message:   Mapped[str]      = mapped_column(Text, nullable=False)
    logged_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False)
