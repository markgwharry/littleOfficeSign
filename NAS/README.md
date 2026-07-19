# NAS backend setup

The bridge and Mosquitto are separate Compose projects. They communicate over
the external Docker network named by `MQTT_NETWORK_NAME` (default:
`mosquito_default`). Start the broker stack first so that network exists.

## Configuration

Copy `office-sign.env.example` to `office-sign.env` and set the service values.
At minimum, configure MQTT and replace `SHARED_SECRET` with a long random value.
Keep the real env file out of Git.

`compose.env.example` contains Compose-time settings: the service env-file path,
host bind address, and shared MQTT network name. Copy it to `compose.env` when
those defaults are unsuitable.

## Deploy

Start Mosquitto, then the bridge:

```sh
docker compose --env-file compose.env -f docker-compose.mosquitto.yml up -d
docker compose --env-file compose.env -f docker-compose.bridge.yml up -d --build
```

On this NAS, the bridge is deployed with:

```sh
docker compose --env-file /volume3/docker/personal/appdata/office-sign/compose.env -f docker-compose.bridge.yml up -d --build
```

Radicale remains optional and can be started separately:

```sh
docker compose -f docker-compose.radicale.yml up -d
```

## HTTP endpoints

- `GET /status` is an unauthenticated liveness endpoint and returns the last
  state seen by the bridge (or `null`). Docker uses it for its healthcheck.
- `GET /health` is an unauthenticated readiness endpoint. It returns HTTP 200
  only while MQTT is connected, otherwise HTTP 503.
- `POST /status_update` republishes calendar state to MQTT.
- `GET /radicale/list` lists events from Radicale (optional).
- `POST /radicale/upsert` upserts `uid` plus `vevent` in Radicale (optional).

All POST endpoints and both Radicale endpoints require
`Authorization: Bearer <SHARED_SECRET>`. `DISPLAY_TZ` controls local formatting
for `end_local` and `start_local`; `NTFY_STATUS_TOPIC` must match the topic used
by the Power Automate ntfy POST URL.
