from __future__ import annotations

import os
from pathlib import Path

from dotenv import load_dotenv
from sqlalchemy import Engine, create_engine
from sqlalchemy.orm import Session, sessionmaker

REPO_ROOT = Path(__file__).resolve().parents[1]

def _database_url() -> str:
    load_dotenv(REPO_ROOT / ".env")
    url = os.environ["DATABASE_URL"]
    if url.startswith("postgresql://"):
        return url.replace("postgresql://", "postgresql+psycopg://", 1)
    return url

engine: Engine = create_engine(_database_url(), pool_pre_ping=True)
SessionLocal: sessionmaker[Session] = sessionmaker(engine, expire_on_commit=False)
