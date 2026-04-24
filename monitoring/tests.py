import json
from unittest.mock import patch

from django.test import Client, TestCase, override_settings

from .services.analysis import analyze_reading
from .services.payloads import normalize_sensor_payload


class PayloadNormalizationTests(TestCase):
    def test_accepts_portuguese_sensor_names(self):
        payload = normalize_sensor_payload(
            {
                "device_id": "esp32-mar-02",
                "temperatura": 24.5,
                "tubidez": 11.2,
                "ph": 7.8,
                "ultrassonico_cm": 95,
            }
        )

        self.assertEqual(payload["device_id"], "esp32-mar-02")
        self.assertEqual(payload["temperature_c"], 24.5)
        self.assertEqual(payload["turbidity_ntu"], 11.2)
        self.assertEqual(payload["ph"], 7.8)
        self.assertEqual(payload["water_level_cm"], 95.0)


class ReadingAnalysisTests(TestCase):
    def test_marks_high_turbidity_as_critical(self):
        analysis = analyze_reading(
            {
                "temperature_c": 25.0,
                "turbidity_ntu": 42.0,
                "ph": 7.2,
                "water_level_cm": 120.0,
            },
            low_level_cm=40,
            high_level_cm=180,
        )

        self.assertEqual(analysis["overall_status"], "critical")
        turbidity_metric = next(
            metric for metric in analysis["metrics"] if metric["slug"] == "turbidity"
        )
        self.assertEqual(turbidity_metric["status"], "critical")


class ReceiveSensorDataViewTests(TestCase):
    def setUp(self):
        self.client = Client()

    @patch("monitoring.views.save_reading")
    def test_creates_reading_from_json_payload(self, mock_save_reading):
        mock_save_reading.side_effect = lambda payload: {**payload, "id": "abc123"}

        response = self.client.post(
            "/api/readings/",
            data=json.dumps(
                {
                    "device_id": "esp32-rio-01",
                    "temperatura": 26.5,
                    "turbidez": 3.1,
                    "ph": 7.0,
                    "nivel_agua_cm": 110.0,
                }
            ),
            content_type="application/json",
        )

        self.assertEqual(response.status_code, 201)
        self.assertEqual(response.json()["reading"]["id"], "abc123")

    @override_settings(DEVICE_API_KEY="segredo-123")
    def test_rejects_request_without_expected_api_key(self):
        response = self.client.post(
            "/api/readings/",
            data=json.dumps(
                {
                    "device_id": "esp32-rio-01",
                    "temperatura": 26.5,
                    "turbidez": 3.1,
                    "ph": 7.0,
                    "nivel_agua_cm": 110.0,
                }
            ),
            content_type="application/json",
        )

        self.assertEqual(response.status_code, 401)
