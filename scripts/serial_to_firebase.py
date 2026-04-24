#!/usr/bin/env python3

import argparse
import json
import os
import sys
from pathlib import Path

import serial


PROJECT_ROOT = Path(__file__).resolve().parents[1]
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

os.environ.setdefault("DJANGO_SETTINGS_MODULE", "conexao_azul.settings")

import django

django.setup()

from monitoring.services.firebase import FirebaseConfigurationError, save_reading
from monitoring.services.payloads import normalize_sensor_payload, serialize_reading


def parse_args():
    parser = argparse.ArgumentParser(
        description="Le a Serial USB do ESP32 e grava leituras no Firebase."
    )
    parser.add_argument("--port", required=True, help="Porta serial, ex.: /dev/ttyUSB0")
    parser.add_argument("--baudrate", type=int, default=115200, help="Baud rate")
    parser.add_argument(
        "--prefix",
        default="SERIAL_JSON:",
        help="Prefixo usado pelo ESP32 para publicar o JSON na serial.",
    )
    parser.add_argument(
        "--once",
        action="store_true",
        help="Processa uma leitura valida e encerra.",
    )
    return parser.parse_args()


def main():
    args = parse_args()

    try:
        serial_conn = serial.Serial(args.port, args.baudrate, timeout=1)
    except serial.SerialException as exc:
        raise SystemExit(f"Falha ao abrir a porta serial: {exc}") from exc

    print(f"Ouvindo {args.port} em {args.baudrate} baud...")

    try:
        while True:
            raw_line = serial_conn.readline()
            if not raw_line:
                continue

            line = raw_line.decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            print(f"[serial] {line}")

            if not line.startswith(args.prefix):
                continue

            payload_text = line[len(args.prefix) :].strip()

            try:
                payload = json.loads(payload_text)
                normalized = normalize_sensor_payload(payload)
                saved = save_reading(normalized)
            except json.JSONDecodeError as exc:
                print(f"JSON invalido recebido pela serial: {exc}")
                continue
            except ValueError as exc:
                print(f"Leitura rejeitada: {exc}")
                continue
            except FirebaseConfigurationError as exc:
                raise SystemExit(f"Firebase nao configurado corretamente: {exc}") from exc

            print("[firebase] leitura gravada:")
            print(json.dumps(serialize_reading(saved), indent=2, ensure_ascii=False))

            if args.once:
                break
    except KeyboardInterrupt:
        print("\nEncerrando captura da serial.")
    finally:
        serial_conn.close()


if __name__ == "__main__":
    main()
