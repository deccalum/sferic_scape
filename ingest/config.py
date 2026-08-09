from __future__ import annotations

import os
from pathlib import Path

from dotenv import load_dotenv

from db.session import REPO_ROOT

load_dotenv(REPO_ROOT / ".env")

def _require(key: str) -> str:
    try:
        return os.environ[key]
    except KeyError as e:
        raise RuntimeError(f"missing env var: {key}") from e

LIBRARY_SR = int(_require("SFERIC_SAMPLE_RATE"))
LIBRARY_CODEC = _require("SFERIC_CODEC")

def _dir(key: str) -> Path:
    raw = Path(_require(key))
    return raw if raw.is_absolute() else REPO_ROOT / raw

def audioset_dir() -> Path:
    return _dir("SFERIC_AUDIOSET_DIR")

def freesound_dir() -> Path:
    return _dir("SFERIC_FREESOUND_DIR")

def cache_dir() -> Path:
    return _dir("SFERIC_CACHE_DIR")
