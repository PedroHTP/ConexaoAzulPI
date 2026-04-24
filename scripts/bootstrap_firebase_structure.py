#!/usr/bin/env python3

import json
import os
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

os.environ.setdefault("DJANGO_SETTINGS_MODULE", "conexao_azul.settings")

import django

django.setup()

from monitoring.services.firebase import FirebaseConfigurationError, get_realtime_database_root


STRUCTURE_PATH = PROJECT_ROOT / "firebase" / "realtime-database.structure.json"


def load_structure():
    return json.loads(STRUCTURE_PATH.read_text(encoding="utf-8"))


def merge_if_missing(reference, value):
    current = reference.get()

    if current is None:
        reference.set(value)
        return "created"

    if isinstance(value, dict) and isinstance(current, dict):
        changes = []
        for key, child_value in value.items():
            child_status = merge_if_missing(reference.child(key), child_value)
            if child_status != "unchanged":
                changes.append((key, child_status))
        return "merged" if changes else "unchanged"

    return "unchanged"


def main():
    if not STRUCTURE_PATH.exists():
        raise SystemExit(f"Estrutura nao encontrada em {STRUCTURE_PATH}")

    try:
        root = get_realtime_database_root().parent
    except FirebaseConfigurationError as exc:
        raise SystemExit(f"Firebase nao configurado corretamente: {exc}") from exc

    structure = load_structure()
    status = merge_if_missing(root, structure)

    print("Bootstrap do Firebase concluido.")
    print(f"Database URL: {os.getenv('FIREBASE_DATABASE_URL', '')}")
    print(f"Estrutura: {STRUCTURE_PATH}")
    print(f"Status: {status}")


if __name__ == "__main__":
    main()
