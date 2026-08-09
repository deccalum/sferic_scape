"""Ingest hand-annotated ground-truth onset marks onto a media row.

  USAGE
  python -m ingest.marks <id> <seconds.point second...>
  python -m ingest.marks <id> --clear
"""

from __future__ import annotations

import argparse
import sys

from sqlalchemy import select

from db.models import Media, MediaOrigin
from db.session import SessionLocal

def set_marks(freesound_id: int, marks: list[float] | None) -> None:
    with SessionLocal() as session, session.begin():
        media = session.scalar(
            select(Media).where(Media.origin == MediaOrigin.freesound,
                                Media.external_id == str(freesound_id)))
        if media is None:
            sys.exit(f"no media for freesound {freesound_id}")
        media.marks = marks
    shown = "cleared" if marks is None else ", ".join(f"{m:g}" for m in marks)
    print(f"freesound {freesound_id} (media {media.id}) marks → {shown}")

def main() -> None:
    p = argparse.ArgumentParser(description="Write ground-truth onset marks onto a media row.")
    p.add_argument("freesound_id", type=int)
    p.add_argument("marks", type=float, nargs="*", help="onset times in seconds")
    p.add_argument("--clear", action="store_true", help="remove all marks from the row")
    args = p.parse_args()

    if args.clear:
        set_marks(args.freesound_id, None)
    else:
        if not args.marks:
            p.error("give at least one mark, or --clear")
        set_marks(args.freesound_id, sorted(args.marks))

if __name__ == "__main__":
    main()
