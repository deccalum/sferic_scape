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

docker compose up -d db

echo "waiting for postgres..."
for i in $(seq 1 60); do
  if docker compose exec -T db pg_isready -U "${POSTGRES_USER:-sferic}" -d "${POSTGRES_DB:-sferic}" >/dev/null 2>&1; then
    break
  fi
  sleep 1
  if [[ "$i" -eq 60 ]]; then
    echo "postgres not ready" >&2
    exit 1
  fi
done

if [[ -x "$ROOT/.venv/bin/python" ]]; then
  PY="$ROOT/.venv/bin/python"
else
  PY=python3
fi

"$PY" -m db.init_db
echo "db up (schema migrated, data preserved)"
