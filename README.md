# ConexaoAzulPI

Sistema Django para monitoramento ambiental de rios e oceanos usando um ESP32 e Firebase.

O projeto recebe leituras de sensores conectados ao ESP32, grava os dados diretamente no Firebase
Realtime Database e apresenta um painel web com interpretacao automatica das medicoes.

As leituras ambientais ficam no Firebase. O SQLite permanece apenas como apoio ao proprio
Django em ambiente de desenvolvimento, testes e recursos internos.

## Sensores monitorados

- Temperatura da agua: ajuda a identificar aquecimento anormal e impacto na vida aquatica.
- Turbidez: indica quantidade de particulas em suspensao, barro, sedimentos ou poluicao.
- pH: mostra se a agua esta acida, neutra ou alcalina.
- Ultrassonico: estima o nivel da agua em centimetros.

## Arquitetura

1. O ESP32 coleta os sensores.
2. O ESP32 envia os dados direto para o Firebase Realtime Database via HTTPS.
3. O Django le as medicoes no Firebase.
4. O painel web interpreta os valores como `normal`, `alert` ou `critical`.

Se o Wi-Fi do ESP32 nao conectar, o firmware entra em fallback por Serial USB:
ele publica a leitura em JSON e um script no notebook pode repassar esses dados ao Firebase.

## Estrutura principal

- [conexao_azul/settings.py](/home/bluefrost/Documentos/GitHub/ConexaoAzulPI/conexao_azul/settings.py)
- [monitoring/views.py](/home/bluefrost/Documentos/GitHub/ConexaoAzulPI/monitoring/views.py)
- [monitoring/services/firebase.py](/home/bluefrost/Documentos/GitHub/ConexaoAzulPI/monitoring/services/firebase.py)
- [monitoring/services/analysis.py](/home/bluefrost/Documentos/GitHub/ConexaoAzulPI/monitoring/services/analysis.py)
- [templates/monitoring/dashboard.html](/home/bluefrost/Documentos/GitHub/ConexaoAzulPI/templates/monitoring/dashboard.html)

## Como executar

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
export FIREBASE_CREDENTIALS_PATH=/caminho/para/firebase-service-account.json
export FIREBASE_PROJECT_ID=seu-projeto-firebase
export FIREBASE_DATABASE_URL=https://seu-projeto-default-rtdb.firebaseio.com
python3 manage.py migrate
python3 manage.py runserver
```

## Configuracao do Firebase

1. Crie um projeto no Firebase.
2. Ative o Realtime Database.
3. Gere uma conta de servico no console do Google Cloud.
4. Baixe o JSON da conta de servico.
5. Defina no ambiente:

```bash
export FIREBASE_CREDENTIALS_PATH=/caminho/para/firebase-service-account.json
export FIREBASE_PROJECT_ID=seu-projeto-firebase
export FIREBASE_DATABASE_URL=https://seu-projeto-default-rtdb.firebaseio.com
export FIREBASE_RTDB_PATH=water_readings
```

Se preferir, use `.env.example` como modelo para organizar suas variaveis e carregue esse arquivo
no shell antes de subir o servidor.

Para consultas mais eficientes no Realtime Database, vale configurar uma regra de indice:

```json
{
  "rules": {
    "water_readings": {
      ".indexOn": ["measured_at_unix"]
    }
  }
}
```

## Formato esperado no Firebase

Os registros gravados direto no Realtime Database seguem este formato:

```json
{
  "device_id": "esp32-rio-01",
  "location": "Rio Sao Francisco",
  "temperature_c": 26.4,
  "turbidity_ntu": 4.8,
  "ph": 7.1,
  "water_level_cm": 132.0,
  "source": "esp32",
  "measured_at": "2026-04-23T15:30:00Z",
  "measured_at_unix": 1776958200
}
```

## Exemplo de envio pelo ESP32

O sketch pronto do projeto esta em [firmware/esp32_conexao_azul/esp32_conexao_azul.ino](/home/bluefrost/Documentos/GitHub/ConexaoAzulPI/firmware/esp32_conexao_azul/esp32_conexao_azul.ino:1).

Ele mantem os mesmos pinos do circuito atual:

- `Trig`: GPIO `5`
- `Echo`: GPIO `18`
- `Turbidez`: GPIO `34`
- `pH`: GPIO `32`
- `Temperatura DS18B20`: GPIO `4`
- `LED interno`: GPIO `2`

Antes de gravar no ESP32, ajuste estes campos no inicio do arquivo:

- `WIFI_SSID`
- `WIFI_PASSWORD`
- `FIREBASE_DATABASE_URL`
- `FIREBASE_RTDB_PATH`
- `FIREBASE_AUTH_TOKEN`
- `DEVICE_ID`
- `DEVICE_LOCATION`

O firmware agora nao passa mais pelo endpoint do Django. Ele grava direto no Firebase usando HTTPS.
Se o Wi-Fi falhar, ele cai para o modo `Serial USB` automaticamente.

O firmware envia para o sistema web estes campos:

```json
{
  "device_id": "esp32-rio-01",
  "location": "Rio Sao Francisco",
  "temperature_c": 26.4,
  "turbidity_ntu": 4.8,
  "ph": 7.1,
  "water_level_cm": 132.0,
  "measured_at": "2026-04-23T15:30:00Z",
  "measured_at_unix": 1776958200,
  "source": "esp32"
}
```

Observacao sobre a turbidez: o seu codigo antigo calculava `pureza %`, onde numero maior significava agua mais limpa. Para combinar com o painel web, o novo sketch envia `turbidity_ntu`, onde numero maior significa agua mais turva. A porcentagem de pureza continua aparecendo no monitor serial apenas como apoio visual.

## Regras do Firebase para envio direto

Como o ESP32 vai escrever direto no Realtime Database, o banco precisa aceitar escrita do dispositivo.

Deixei um modelo em [firebase/realtime-database.rules.json](/home/bluefrost/Documentos/GitHub/ConexaoAzulPI/firebase/realtime-database.rules.json:1).
Tambem deixei a estrutura-base em [firebase/realtime-database.structure.json](/home/bluefrost/Documentos/GitHub/ConexaoAzulPI/firebase/realtime-database.structure.json:1).

Esse modelo:

- bloqueia leitura publica
- permite escrita apenas em `water_readings/<id>`
- valida o formato esperado dos campos
- cria indice para `measured_at_unix`

Para prototipo, esse caminho e o mais simples. Em producao, o ideal e substituir a escrita publica por Firebase Authentication com token de usuario/dispositivo.

Para montar a estrutura inicial do banco automaticamente, rode:

```bash
source .venv/bin/activate
python scripts/bootstrap_firebase_structure.py
```

Esse bootstrap cria, se ainda nao existirem:

- `metadata`
- `monitoring_config`
- `devices`
- `locations`
- `latest_readings`
- `water_readings`

Trecho ilustrativo de escrita direta usando REST:

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

const char* ssid = "SEU_WIFI";
const char* password = "SUA_SENHA";
const char* databaseUrl = "https://SEU_PROJETO-default-rtdb.firebaseio.com";

void enviarLeitura(float temperatura, float turbidez, float ph, float nivel) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, String(databaseUrl) + "/water_readings/leitura-001.json");
  http.addHeader("Content-Type", "application/json");

  String payload = "{";
  payload += "\"device_id\":\"esp32-rio-01\",";
  payload += "\"location\":\"Rio Sao Francisco\",";
  payload += "\"temperature_c\":" + String(temperatura, 2) + ",";
  payload += "\"turbidity_ntu\":" + String(turbidez, 2) + ",";
  payload += "\"ph\":" + String(ph, 2) + ",";
  payload += "\"water_level_cm\":" + String(nivel, 2);
  payload += "}";

  http.PUT(payload);
  http.end();
}
```

## Fallback por Serial USB

Se o ESP32 estiver ligado no notebook por USB e o Wi-Fi nao conectar, o firmware publica uma linha
com prefixo `SERIAL_JSON:` no monitor serial.

Para aproveitar esse modo e ainda gravar no Firebase, rode:

```bash
source .venv/bin/activate
python scripts/serial_to_firebase.py --port /dev/ttyUSB0
```

Exemplos comuns de porta:

- Linux: `/dev/ttyUSB0` ou `/dev/ttyACM0`
- Windows: `COM3`
- macOS: `/dev/tty.usbserial-*`

O script:

- escuta a Serial USB do ESP32
- detecta linhas `SERIAL_JSON:`
- normaliza a leitura
- grava no Firebase usando a mesma configuracao do projeto

Assim voce fica com dois modos validos:

- com Wi-Fi: `ESP32 -> Firebase`
- sem Wi-Fi: `ESP32 -> Serial USB -> notebook -> Firebase`

## Como o sistema explica os dados

- Temperatura: faixa ideal aproximada entre 20 e 30 degC para monitoramento geral.
- Turbidez: ate 5 NTU e tratada como boa; acima disso o sistema entra em observacao.
- pH: entre 6.5 e 8.5 e considerado equilibrado.
- Nivel da agua: usa limites configuraveis em `WATER_LEVEL_LOW_CM` e `WATER_LEVEL_HIGH_CM`.

Essas faixas sao genericas e servem como base inicial. Em projeto real, o ideal e ajustar os
limites conforme o rio, estuario, oceano costeiro ou reservatorio monitorado.
