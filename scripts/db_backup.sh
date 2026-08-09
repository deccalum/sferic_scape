#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ -f .env ]]; then
  set -a
  # shellcheck disable=SC1091
  source .env
  set +a
fi

CONTAINER="${SFERIC_DB_CONTAINER:-sferic-db}"
USER="${POSTGRES_USER:-sferic}"
DB="${POSTGRES_DB:-sferic}"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
OUT="/backups/sferic_${DB}_${STAMP}.dump"

if ! docker inspect -f '{{.State.Running}}' "$CONTAINER" 2>/dev/null | grep -q true; then
  echo "container $CONTAINER is not running — docker compose up -d" >&2
  exit 1
fi

echo "dumping $DB → $OUT (inside container host path = SFERIC_BACKUP_DIR)"
docker exec -t "$CONTAINER" \
  pg_dump -U "$USER" -d "$DB" -Fc --verbose -f "$OUT"

KEEP="${SFERIC_BACKUP_KEEP:-14}"
docker exec "$CONTAINER" sh -c \
  "ls -1t /backups/sferic_${DB}_*.dump 2>/dev/null | tail -n +$((KEEP + 1)) | xargs -r rm -f"

echo "done: host backup dir holds $(basename "$OUT")"
echo "restore example:"
echo "  docker exec -i $CONTAINER pg_restore -U $USER -d $DB --clean --if-exists /backups/$(basename "$OUT")"
