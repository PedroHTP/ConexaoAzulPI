import json

from django.conf import settings
from django.http import JsonResponse
from django.shortcuts import render
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_GET, require_POST

from .services.analysis import analyze_reading
from .services.firebase import (
    FirebaseConfigurationError,
    list_recent_readings,
    save_reading,
)
from .services.payloads import normalize_sensor_payload, serialize_reading

SENSOR_DESCRIPTIONS = [
    {
        "name": "Temperatura",
        "unit": "°C",
        "description": (
            "Indica o aquecimento da água. Variações fortes afetam oxigenação, "
            "vida aquática e equilíbrio do ecossistema."
        ),
    },
    {
        "name": "Turbidez",
        "unit": "NTU",
        "description": (
            "Mostra o quanto a água está opaca por partículas em suspensão. "
            "Valores altos podem indicar sedimentos, esgoto ou contaminação."
        ),
    },
    {
        "name": "pH",
        "unit": "",
        "description": (
            "Mede acidez ou alcalinidade. Faixas extremas podem prejudicar peixes, "
            "plantas aquáticas e a qualidade geral da água."
        ),
    },
    {
        "name": "Nível da água",
        "unit": "cm",
        "description": (
            "Estimado pelo sensor ultrassônico. Ajuda a detectar seca, cheia, "
            "assoreamento ou mudanças rápidas na coluna d'água."
        ),
    },
]


def _enrich_reading(reading):
    analysis = analyze_reading(
        reading,
        low_level_cm=settings.WATER_LEVEL_LOW_CM,
        high_level_cm=settings.WATER_LEVEL_HIGH_CM,
    )
    return {
        **reading,
        "analysis": analysis,
    }


@require_GET
def dashboard(request):
    recent_readings = []
    firebase_error = ""

    try:
        recent_readings = [_enrich_reading(reading) for reading in list_recent_readings()]
    except FirebaseConfigurationError as exc:
        firebase_error = str(exc)

    latest_reading = recent_readings[0] if recent_readings else None

    context = {
        "firebase_ready": not firebase_error,
        "firebase_error": firebase_error,
        "firebase_database_url": settings.FIREBASE_DATABASE_URL,
        "firebase_rtdb_path": settings.FIREBASE_RTDB_PATH,
        "latest_reading": latest_reading,
        "recent_readings": recent_readings,
        "sensor_descriptions": SENSOR_DESCRIPTIONS,
        "sample_payload": json.dumps(
            {
                "device_id": "esp32-rio-01",
                "location": "Rio Sao Francisco",
                "temperatura": 26.4,
                "turbidez": 4.8,
                "ph": 7.1,
                "nivel_agua_cm": 132.0,
            },
            indent=2,
            ensure_ascii=True,
        ),
    }
    return render(request, "monitoring/dashboard.html", context)


@require_GET
def healthcheck(request):
    return JsonResponse(
        {
            "status": "ok",
            "firebase_configured": bool(settings.FIREBASE_CREDENTIALS_PATH),
            "database_url": settings.FIREBASE_DATABASE_URL,
            "database_path": settings.FIREBASE_RTDB_PATH,
        }
    )


@csrf_exempt
@require_POST
def receive_sensor_data(request):
    expected_api_key = settings.DEVICE_API_KEY.strip()
    if expected_api_key:
        received_api_key = request.headers.get("X-API-Key", "").strip()
        if received_api_key != expected_api_key:
            return JsonResponse(
                {"error": "Chave de API invalida ou ausente."},
                status=401,
            )

    try:
        payload = json.loads(request.body.decode("utf-8") or "{}")
    except json.JSONDecodeError:
        return JsonResponse(
            {"error": "JSON invalido enviado pelo dispositivo."},
            status=400,
        )

    try:
        normalized = normalize_sensor_payload(payload)
        saved_reading = save_reading(normalized)
        response_payload = serialize_reading(saved_reading)
        analysis = analyze_reading(
            saved_reading,
            low_level_cm=settings.WATER_LEVEL_LOW_CM,
            high_level_cm=settings.WATER_LEVEL_HIGH_CM,
        )
    except ValueError as exc:
        return JsonResponse({"error": str(exc)}, status=400)
    except FirebaseConfigurationError as exc:
        return JsonResponse({"error": str(exc)}, status=503)

    return JsonResponse(
        {
            "message": "Leitura recebida com sucesso.",
            "reading": response_payload,
            "analysis": analysis,
        },
        status=201,
    )
