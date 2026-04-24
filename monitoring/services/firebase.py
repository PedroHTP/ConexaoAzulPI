from pathlib import Path

from django.conf import settings
from django.utils import timezone

try:
    import firebase_admin
    from firebase_admin import credentials, db
    from firebase_admin.exceptions import InvalidArgumentError
except ImportError:  # pragma: no cover - tratado em tempo de execucao
    firebase_admin = None
    credentials = None
    db = None
    InvalidArgumentError = RuntimeError

from .payloads import from_realtime_database_record, to_realtime_database_record


class FirebaseConfigurationError(RuntimeError):
    pass


def _get_firebase_app():
    if firebase_admin is None:
        raise FirebaseConfigurationError(
            "Dependencia 'firebase-admin' nao instalada. Rode 'pip install -r requirements.txt'."
        )

    credentials_path = settings.FIREBASE_CREDENTIALS_PATH.strip()
    if not credentials_path:
        raise FirebaseConfigurationError(
            "Defina FIREBASE_CREDENTIALS_PATH apontando para o JSON da conta de servico."
        )

    database_url = settings.FIREBASE_DATABASE_URL.strip()
    if not database_url:
        raise FirebaseConfigurationError(
            "Defina FIREBASE_DATABASE_URL apontando para o Realtime Database."
        )

    credential_file = Path(credentials_path)
    if not credential_file.exists():
        raise FirebaseConfigurationError(
            f"Arquivo de credenciais nao encontrado em: {credential_file}"
        )

    try:
        return firebase_admin.get_app()
    except ValueError:
        options = {}
        if settings.FIREBASE_PROJECT_ID:
            options["projectId"] = settings.FIREBASE_PROJECT_ID
        options["databaseURL"] = database_url

        return firebase_admin.initialize_app(
            credentials.Certificate(str(credential_file)),
            options,
        )


def get_realtime_database_root():
    app = _get_firebase_app()
    return db.reference(settings.FIREBASE_RTDB_PATH, app=app)


def save_reading(reading):
    root = get_realtime_database_root()
    payload = {
        **reading,
        "received_at": timezone.now(),
    }
    serialized = to_realtime_database_record(payload)
    push_result = root.push(serialized)
    payload["id"] = push_result.key
    return payload


def list_recent_readings(limit=20):
    root = get_realtime_database_root()
    try:
        data = root.order_by_child("measured_at_unix").limit_to_last(limit).get() or {}
    except InvalidArgumentError:
        # Fallback for projects that have not yet added ".indexOn" to RTDB rules.
        data = root.get() or {}

    readings = []
    for reading_id, record in data.items():
        readings.append(from_realtime_database_record(reading_id, record or {}))

    readings.sort(key=lambda reading: reading["measured_at"], reverse=True)
    return readings[:limit]
