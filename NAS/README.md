# NAS Backend Setup

## Quick start (single compose with Mosquitto + Bridge)
```bash
docker compose -f docker-compose.bridge.yml up -d --build
```

## Separate stacks
1) Mosquitto:
```bash
docker compose -f docker-compose.mosquitto.yml up -d
```

2) Bridge:
```bash
docker compose -f docker-compose.bridge.yml up -d --build
```

3) Radicale (CalDAV):
```bash
docker compose -f docker-compose.radicale.yml up -d
```

### Nginx Proxy Manager
- Proxy host `calendar.nas` → `http://127.0.0.1:5232`
- Enable SSL and auth.

### Bridge HTTP endpoints
- `POST /status_update` — accepts calendar state JSON and republishes to MQTT.
- `GET /radicale/list` — lists events from Radicale as JSON (optional).
- `POST /radicale/upsert` — upserts `uid` + `vevent` (optional).

### Env
- `NTFY_STATUS_TOPIC` must match your Power Automate ntfy POST URL.
- `DISPLAY_TZ` sets the local time formatting (`end_local`, `start_local`).

