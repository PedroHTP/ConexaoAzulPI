SEVERITY_ORDER = {
    "normal": 0,
    "alert": 1,
    "critical": 2,
}


def _build_metric(slug, name, value, unit, status, message):
    return {
        "slug": slug,
        "name": name,
        "value": value,
        "unit": unit,
        "status": status,
        "message": message,
    }


def _temperature_status(value):
    if 20 <= value <= 30:
        return "normal", "Faixa estavel para boa parte dos ambientes aquaticos."
    if 15 <= value <= 35:
        return "alert", "Temperatura fora da faixa ideal e merece acompanhamento."
    return "critical", "Temperatura extrema com potencial de estresse para a vida aquatica."


def _turbidity_status(value):
    if value <= 5:
        return "normal", "Agua visualmente limpa, com baixa presenca de particulas suspensas."
    if value <= 25:
        return "alert", "Turbidez elevada, sugerindo sedimentos ou materia organica."
    return "critical", "Turbidez muito alta, com forte indicio de contaminacao ou carreamento."


def _ph_status(value):
    if 6.5 <= value <= 8.5:
        return "normal", "pH equilibrado para monitoramento ambiental geral."
    if 6.0 <= value <= 9.0:
        return "alert", "pH levemente fora do ideal e precisa de observacao."
    return "critical", "pH extremo, com risco para fauna, flora e qualidade da agua."


def _water_level_status(value, low_level_cm, high_level_cm):
    if low_level_cm <= value <= high_level_cm:
        return "normal", "Nivel dentro da faixa operacional esperada."
    if value < 0:
        return "critical", "Medicao invalida para nivel da agua."
    band = max(high_level_cm - low_level_cm, 1)
    critical_low = max(low_level_cm - band * 0.5, 0)
    critical_high = high_level_cm + band * 0.5
    if critical_low <= value <= critical_high:
        return "alert", "Nivel fora da faixa prevista, indicando seca ou aumento do volume."
    return "critical", "Nivel muito distante do padrao, com risco de anomalia severa."


def _overall_status(metrics):
    return max(metrics, key=lambda metric: SEVERITY_ORDER[metric["status"]])["status"]


def _summary(overall_status):
    summaries = {
        "normal": "Leitura estavel, sem sinais importantes de anomalia no momento.",
        "alert": "Leitura com variacoes que merecem acompanhamento de campo.",
        "critical": "Leitura com risco ambiental relevante e necessidade de acao rapida.",
    }
    return summaries[overall_status]


def analyze_reading(reading, low_level_cm, high_level_cm):
    temperature_status, temperature_message = _temperature_status(reading["temperature_c"])
    turbidity_status, turbidity_message = _turbidity_status(reading["turbidity_ntu"])
    ph_status, ph_message = _ph_status(reading["ph"])
    water_level_status, water_level_message = _water_level_status(
        reading["water_level_cm"],
        low_level_cm=low_level_cm,
        high_level_cm=high_level_cm,
    )

    metrics = [
        _build_metric(
            "temperature",
            "Temperatura",
            reading["temperature_c"],
            "degC",
            temperature_status,
            temperature_message,
        ),
        _build_metric(
            "turbidity",
            "Turbidez",
            reading["turbidity_ntu"],
            "NTU",
            turbidity_status,
            turbidity_message,
        ),
        _build_metric(
            "ph",
            "pH",
            reading["ph"],
            "",
            ph_status,
            ph_message,
        ),
        _build_metric(
            "water-level",
            "Nivel da agua",
            reading["water_level_cm"],
            "cm",
            water_level_status,
            water_level_message,
        ),
    ]

    overall_status = _overall_status(metrics)
    return {
        "overall_status": overall_status,
        "summary": _summary(overall_status),
        "metrics": metrics,
    }
