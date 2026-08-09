"""Apply schema from models.py without wiping data.

  python -m db.init_db
  python -m db.init_db --reset

  `reset` drops every declared table first.
"""

from __future__ import annotations

import argparse

from sqlalchemy import and_, func, inspect, select, text
from sqlalchemy.ext.compiler import compiles
from sqlalchemy.schema import AddConstraint, DDLElement
from sqlalchemy.sql.schema import Column, Table

from db.models import LOOKUPS, Base, FreeSound, Fsd50k, Media, MediaOrigin, YtVideo
from db.session import engine

class AddColumn(DDLElement):
    inherit_cache = False
    def __init__(self, table: Table, column: Column) -> None:
        self.table = table
        self.column = column

@compiles(AddColumn)
def _compile_add_column(element: AddColumn, compiler, **kw) -> str:
    colspec = compiler.get_column_specification(element.column)
    table = compiler.preparer.format_table(element.table)
    return f"ALTER TABLE {table} ADD COLUMN IF NOT EXISTS {colspec}"

class CreateOrReplaceView(DDLElement):
    inherit_cache = False
    def __init__(self, name: str, selectable) -> None:
        self.name = name
        self.selectable = selectable

@compiles(CreateOrReplaceView)
def _compile_create_view(element: CreateOrReplaceView, compiler, **kw) -> str:
    body = compiler.sql_compiler.process(element.selectable, literal_binds=True)
    return f"CREATE OR REPLACE VIEW {compiler.preparer.quote(element.name)} AS {body}"

def _media_source_selectable():
    m = Media.__table__
    fs = FreeSound.__table__
    yt = YtVideo.__table__
    fsd = Fsd50k.__table__
    return (
        select(
            m.c.id.label("media_id"),
            m.c.origin,
            m.c.external_id,
            m.c.corpus_role,
            m.c.byte_size,
            func.coalesce(fs.c.title, yt.c.title, fsd.c.title, m.c.external_id).label("title"),
            func.coalesce(fs.c.username, yt.c.uploader, yt.c.uploader_id).label("recorded_by"),
            func.coalesce(fs.c.webpage_url, yt.c.webpage_url).label("url"),
            func.coalesce(fs.c.license, yt.c.license, fsd.c.license).label("license"),
            fs.c.recording_id,
            fs.c.freesound_id,
            yt.c.ytid,
            fsd.c.fname.label("fsd_fname"),
        ).select_from(
            m.outerjoin(fs, and_(fs.c.media_id == m.c.id, m.c.origin == MediaOrigin.freesound))
            .outerjoin(yt, and_(yt.c.media_id == m.c.id, m.c.origin == MediaOrigin.youtube))
            .outerjoin(fsd, and_(fsd.c.media_id == m.c.id, m.c.origin == MediaOrigin.fsd50k))
        )
    )

def _add_missing_foreign_keys() -> None:
    insp = inspect(engine)
    existing_tables = set(insp.get_table_names())

    with engine.begin() as conn:
        for table in Base.metadata.sorted_tables:
            if table.name not in existing_tables:
                continue
            have = {
                (tuple(fk["constrained_columns"]), fk["referred_table"])
                for fk in insp.get_foreign_keys(table.name)
            }
            for fkc in table.foreign_key_constraints:
                cols = tuple(c.name for c in fkc.columns)
                referred = fkc.referred_table.name
                if (cols, referred) in have:
                    continue
                conn.execute(AddConstraint(fkc))
                print(f"  + fk {table.name}({', '.join(cols)}) -> {referred}")


def _seed_lookups(conn) -> None:
    for table_name, enum_cls in LOOKUPS.items():
        keys = [e.value for e in enum_cls]
        conn.execute(
            text(
                f'INSERT INTO "{table_name}" (key) SELECT unnest(CAST(:keys AS text[]))'
                f" ON CONFLICT (key) DO NOTHING"
            ),
            {"keys": keys},
        )
        stale = conn.execute(
            text(
                f'DELETE FROM "{table_name}" WHERE key <> ALL(CAST(:keys AS text[]))'
                f" RETURNING key"
            ),
            {"keys": keys},
        ).scalars().all()
        for key in stale:
            print(f"  - {table_name}.{key}")

def _add_missing_columns() -> None:
    insp = inspect(engine)
    existing_tables = set(insp.get_table_names())

    with engine.begin() as conn:
        for table in Base.metadata.sorted_tables:
            if table.name not in existing_tables:
                continue
            have = {c["name"] for c in insp.get_columns(table.name)}
            for col in table.columns:
                if col.name in have:
                    continue
                col_copy = col._copy()
                if (
                    not col_copy.nullable
                    and col_copy.server_default is None
                    and col_copy.computed is None
                ):
                    col_copy.nullable = True
                conn.execute(AddColumn(table, col_copy))
                print(f"  + {table.name}.{col.name}")

def init_db(reset: bool = False) -> None:
    with engine.begin() as conn:
        conn.execute(text("DROP VIEW IF EXISTS media_source"))
    if reset:
        Base.metadata.drop_all(bind=engine)
        print("  ! dropped all declared tables (--reset)")

    Base.metadata.create_all(bind=engine)
    _add_missing_columns()
    with engine.begin() as conn:
        _seed_lookups(conn)
    _add_missing_foreign_keys()
    with engine.begin() as conn:
        conn.execute(CreateOrReplaceView("media_source", _media_source_selectable()))
    print("schema ready (lookup-seeded + media_source view)")

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--reset",
        action="store_true",
        help="DROP every declared table before recreating. Destroys all rows.",
    )
    init_db(reset=parser.parse_args().reset)

if __name__ == "__main__":
    main()
