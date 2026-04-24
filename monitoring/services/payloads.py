from datetime import datetime

from django.utils import timezone


FIELD_ALIASES = {
    "device_id": ["device_id", "esp32_id", "sensor_id"],
    "location": ["location", "localizacao", "local"],
    "temperature_c": ["temperature_c", "temperatura", "temperature"],
    "turbidity_ntu": ["turbidity_ntu", "turbidez", "tubidez", "turbidity"],
    "ph": ["ph", "p_h"],
    "water_level_cm": [
        "water_level_cm",
        "nivel_agua_cm",
        "nivel_agua",
        "ultrassonico_cm",
        "distancia_ultrassonica_cm",
    ],
    "source": ["source", "origem"],
    "measured_at": ["measured_at", "timestamp", "data_hora"],
}


def _first_present(payload, keys):
    for key in keys:
        if key in payload and payload[key] not in ("", None):
            return payload[key]
    return None


def _to_float(value, field_name):
    try:
        return float(value)
    except (TypeError, ValueError):
        raise ValueError(f"O campo '{field_name}' precisa ser numerico.")


def _parse_datetime(value):
    if value in ("", None):
        return timezone.now()

    if isinstance(value, datetime):
        dt_value = value
    elif isinstance(value, str):
        dt_value = datetime.fromisoformat(value.replace("Z", "+00:00"))
    else:
        raise ValueError("O campo 'measured_at' precisa estar em formato ISO 8601.")

    if timezone.is_naive(dt_value):
        return timezone.make_aware(dt_value, timezone.get_current_timezone())
    return dt_value


def _parse_datetime_string(value, field_name):
    if value in ("", None):
        return None

    try:
        return _parse_datetime(value)
    except ValueError as exc:
        raise ValueError(f"Valor invalido em '{field_name}': {exc}") from exc


def normalize_sensor_payload(payload):
    if not isinstance(payload, dict):
        raise ValueError("O corpo da requisicao precisa ser um objeto JSON.")

    normalized = {
        "device_id": str(
            _first_present(payload, FIELD_ALIASES["device_id"]) or "esp32-dispositivo"
        ),
        "location": str(
            _first_present(payload, FIELD_ALIASES["location"]) or "Ponto nao informado"
        ),
        "temperature_c": _to_float(
            _first_present(payload, FIELD_ALIASES["temperature_c"]),
            "temperature_c",
        ),
        "turbidity_ntu": _to_float(
            _first_present(payload, FIELD_ALIASES["turbidity_ntu"]),
            "turbidity_ntu",
        ),
        "ph": _to_float(
            _first_present(payload, FIELD_ALIASES["ph"]),
            "ph",
        ),
        "water_level_cm": _to_float(
            _first_present(payload, FIELD_ALIASES["water_level_cm"]),
            "water_level_cm",
        ),
        "source": str(_first_present(payload, FIELD_ALIASES["source"]) or "esp32"),
        "measured_at": _parse_datetime(
            _first_present(payload, FIELD_ALIASES["measured_at"])
        ),
    }
    return normalized


def serialize_reading(reading, keep_datetimes=False):
    serialized = dict(reading)
    for field_name in ("measured_at", "received_at"):
        field_value = serialized.get(field_name)
        if field_value is None:
            continue
        if keep_datetimes:
            continue
        serialized[field_name] = field_value.isoformat()
    return serialized


def to_realtime_database_record(reading):
    measured_at = reading["measured_at"]
    received_at = reading["received_at"]

    return {
        "device_id": reading["device_id"],
        "location": reading["location"],
        "temperature_c": reading["temperature_c"],
        "turbidity_ntu": reading["turbidity_ntu"],
        "ph": reading["ph"],
        "water_level_cm": reading["water_level_cm"],
        "source": reading["source"],
        "measured_at": measured_at.isoformat(),
        "measured_at_unix": int(measured_at.timestamp()),
        "received_at": received_at.isoformat(),
        "received_at_unix": int(received_at.timestamp()),
    }


def from_realtime_database_record(reading_id, record):
    restored = {
        "id": reading_id,
        "device_id": str(record.get("device_id", "esp32-dispositivo")),
        "location": str(record.get("location", "Ponto nao informado")),
        "temperature_c": float(record.get("temperature_c", 0)),
        "turbidity_ntu": float(record.get("turbidity_ntu", 0)),
        "ph": float(record.get("ph", 0)),
        "water_level_cm": float(record.get("water_level_cm", 0)),
        "source": str(record.get("source", "esp32")),
        "measured_at": _parse_datetime_string(record.get("measured_at"), "measured_at"),
        "received_at": _parse_datetime_string(record.get("received_at"), "received_at"),
    }

    if restored["measured_at"] is None:
        restored["measured_at"] = timezone.now()
    if restored["received_at"] is None:
        restored["received_at"] = restored["measured_at"]

    return restored
