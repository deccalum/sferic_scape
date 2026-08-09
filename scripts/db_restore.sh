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
FILE="${1:?usage: $0 <dump-filename-inside-/backups>}"

docker exec -i "$CONTAINER" \
  pg_restore -U "$USER" -d "$DB" --clean --if-exists --verbose "/backups/$FILE"
