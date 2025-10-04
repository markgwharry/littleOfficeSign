#!/usr/bin/env python3
import os, time, json, socket, threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer

import requests
import paho.mqtt.client as mqtt
from dateutil import parser as dparse, tz
from icalendar import Calendar

MQTT_HOST   = os.getenv("MQTT_HOST", "127.0.0.1")
MQTT_PORT   = int(os.getenv("MQTT_PORT", "1883"))
TOPIC_STATE = os.getenv("TOPIC_STATE", "office/sign/state")
TOPIC_RING  = os.getenv("TOPIC_RING",  "office/sign/ring")

TEAMS_WEBHOOK = os.getenv("TEAMS_WEBHOOK", "").strip()
FLOW_URL      = os.getenv("FLOW_URL", "").strip()
NTFY_URL      = os.getenv("NTFY_URL", "").strip()

NTFY_STATUS_URL   = os.getenv("NTFY_STATUS_URL", "").rstrip("/")
NTFY_STATUS_TOPIC = os.getenv("NTFY_STATUS_TOPIC", "").strip()

LISTEN_ADDR   = os.getenv("LISTEN_ADDR", "0.0.0.0")
LISTEN_PORT   = int(os.getenv("LISTEN_PORT", "8787"))
SHARED_SECRET = os.getenv("SHARED_SECRET", "")
PUBLISH_INTERVAL = int(os.getenv("PUBLISH_INTERVAL", "0"))
DISPLAY_TZ = os.getenv("DISPLAY_TZ", "Europe/London")

RAD_URL  = os.getenv("RAD_URL","").rstrip("/")
RAD_USER = os.getenv("RAD_USER","")
RAD_PASS = os.getenv("RAD_PASS","")
RAD_PATH = os.getenv("RAD_PATH","/user/calendar/")

START_TS = time.time()

_last_state_lock = threading.Lock()
LAST_STATE = None
LAST_STATE_SOURCE = None
LAST_STATE_AT = None

def now_iso(): return datetime.now(timezone.utc).isoformat(timespec="seconds")
def log(*a): print(datetime.now().strftime("%Y-%m-%d %H:%M:%S"), *a, flush=True)

def notify_all(text: str):
    if TEAMS_WEBHOOK:
        try:
            r = requests.post(TEAMS_WEBHOOK, json={"text": text}, timeout=8)
            log("Teams POST", r.status_code)
        except Exception as e:
            log("Teams notify error:", e)
    if FLOW_URL:
        try:
            r = requests.post(FLOW_URL, json={"text": text, "at": now_iso()}, timeout=8)
            log("Flow POST", r.status_code, (r.text or "")[:120])
        except Exception as e:
            log("Flow notify error:", e)
    if NTFY_URL:
        try:
            r = requests.post(NTFY_URL, data=text.encode(), timeout=8)
            log("ntfy alert POST", r.status_code)
        except Exception as e:
            log("ntfy alert error:", e)

def _fmt_local(iso_str, tzname):
    if not iso_str:
        return ""
    try:
        dt = dparse.parse(iso_str)
    except Exception:
        return ""
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    target = tz.gettz(tzname) or tz.gettz("Europe/London")
    loc = dt.astimezone(target)
    today = datetime.now(target).date()
    return loc.strftime("%H:%M") if loc.date() == today else loc.strftime("%a %H:%M")

def _enrich_state_with_local_times(state):
    if not isinstance(state, dict):
        return state
    now_obj = state.get("now", {}) or {}
    nxt_obj = state.get("next", {}) or {}
    end_local   = _fmt_local(now_obj.get("end",""),   DISPLAY_TZ)
    start_local = _fmt_local(nxt_obj.get("start",""), DISPLAY_TZ)
    if end_local:   now_obj["end_local"]   = end_local
    if start_local: nxt_obj["start_local"] = start_local
    state["now"]  = now_obj
    state["next"] = nxt_obj
    return state

client = mqtt.Client(client_id=f"office-sign-bridge@{socket.gethostname()}")

def _clone_state(state):
    try:
        return json.loads(json.dumps(state))
    except Exception:
        return state

def _record_state(state, source):
    global LAST_STATE, LAST_STATE_SOURCE, LAST_STATE_AT
    snapshot = _clone_state(state)
    with _last_state_lock:
        LAST_STATE = snapshot
        LAST_STATE_SOURCE = source
        LAST_STATE_AT = time.time()

def _state_snapshot():
    with _last_state_lock:
        state = _clone_state(LAST_STATE) if LAST_STATE is not None else None
        source = LAST_STATE_SOURCE
        at = LAST_STATE_AT
    age = time.time() - at if at else None
    return state, source, age

def on_connect(c, *_):
    log("MQTT connected")
    c.subscribe(TOPIC_RING)
    initial_state = {
        "status": "free",
        "now": {"title": ""},
        "next": {"title": "Bridge online"},
        "at": now_iso()
    }
    c.publish(TOPIC_STATE, json.dumps(initial_state), qos=0, retain=True)
    _record_state(initial_state, "mqtt_connect")

def on_message(c, _, msg):
    if msg.topic == TOPIC_RING:
        try: data = json.loads(msg.payload.decode())
        except Exception: data = {"raw": msg.payload.decode(errors="ignore")}
        log("🔔 RING:", data)
        notify_all("🔔 Someone tapped RING at your office sign.")

client.on_connect = on_connect
client.on_message = on_message

class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body="OK"):
        self.send_response(code); self.send_header("Content-Type","application/json")
        self.end_headers(); self.wfile.write(body.encode())

    def do_GET(self):
        if self.path == "/healthz":
            state, source, age = _state_snapshot()
            payload = {
                "ok": True,
                "uptime_s": round(time.time() - START_TS, 1),
                "mqtt_connected": bool(client.is_connected()),
                "last_state_source": source,
                "last_state_age_s": round(age, 1) if age is not None else None,
                "last_state": state
            }
            return self._send(200, json.dumps(payload))

        if self.path == "/radicale/list":
            try:
                out = _rad_list()
                return self._send(200, json.dumps({"events": out}))
            except Exception as e:
                return self._send(500, json.dumps({"error": str(e)}))
        return self._send(404, '{"error":"not found"}')

    def do_POST(self):
        if self.path == "/status_update":
            auth = self.headers.get("Authorization", "")
            if SHARED_SECRET and auth != f"Bearer {SHARED_SECRET}":
                return self._send(401, '{"error":"unauthorized"}')
            try:
                ln = int(self.headers.get("content-length","0"))
                data = json.loads(self.rfile.read(ln).decode("utf-8","ignore"))
            except Exception:
                return self._send(400, '{"error":"invalid json"}')
            data = _enrich_state_with_local_times(data)
            client.publish(TOPIC_STATE, json.dumps(data), qos=0, retain=True)
            log("State update via HTTP:", data)
            _record_state(data, "http")
            return self._send(200, '{"ok":true}')

        if self.path == "/radicale/upsert":
            try:
                ln = int(self.headers.get("content-length","0"))
                data = json.loads(self.rfile.read(ln).decode("utf-8","ignore"))
                uid = data.get("uid"); vevent = data.get("vevent","")
                if not uid or not vevent:
                    return self._send(400, '{"error":"bad input"}')
                res = _rad_put(uid, vevent)
                return self._send(200, json.dumps(res))
            except Exception as e:
                return self._send(500, json.dumps({"error": str(e)}))

        return self._send(404, '{"error":"not found"}')

def http_thread():
    httpd = HTTPServer((LISTEN_ADDR, LISTEN_PORT), Handler)
    log(f"HTTP listening on {LISTEN_ADDR}:{LISTEN_PORT} (POST /status_update)")
    httpd.serve_forever()

def ntfy_status_thread():
    if not (NTFY_STATUS_URL and NTFY_STATUS_TOPIC):
        log("ntfy status subscription disabled (no NTFY_STATUS_URL/TOPIC)")
        return
    url = f"{NTFY_STATUS_URL}/{NTFY_STATUS_TOPIC}/sse"
    log(f"ntfy status subscribe: {url}")
    while True:
        try:
            with requests.get(
                url,
                stream=True,
                timeout=(5, 310),
                headers={"Accept": "text/event-stream"}
            ) as r:
                if not r.ok:
                    log("ntfy SSE error", r.status_code, (r.text or "")[:120])
                    time.sleep(5); continue
                event_type = None
                for raw_line in r.iter_lines(decode_unicode=True):
                    if raw_line is None:
                        continue
                    line = raw_line.strip()
                    if not line:
                        event_type = None
                        continue
                    if line.startswith("event:"):
                        event_type = line.split(":",1)[1].strip()
                        continue
                    if not line.startswith("data:"):
                        continue
                    data = line[5:].strip()
                    try:
                        wrapper = json.loads(data)
                    except Exception as e:
                        log("ntfy data parse error:", e, data[:120])
                        continue
                    if isinstance(wrapper, dict) and wrapper.get("event") in ("open","keepalive"):
                        continue
                    state = None
                    if isinstance(wrapper, dict) and "message" in wrapper:
                        try:
                            state = json.loads(wrapper["message"])
                        except Exception as e:
                            log("ntfy inner message parse error:", e, (wrapper.get("message","")[:120]))
                            continue
                    else:
                        state = wrapper
                    if not isinstance(state, dict) or "status" not in state:
                        log("ntfy payload not a state object; skipping:", str(state)[:120])
                        continue
                    state = _enrich_state_with_local_times(state)
                    client.publish(TOPIC_STATE, json.dumps(state), qos=0, retain=True)
                    log("ntfy status -> MQTT (state):", state)
                    _record_state(state, "ntfy")
        except Exception as e:
            log("ntfy SSE connection error:", e)
            time.sleep(5)

def _rad_list():
    if not RAD_URL:
        raise RuntimeError("RAD_URL not configured")
    url = f"{RAD_URL}{RAD_PATH.rstrip('/')}.ics"
    r = requests.get(url, auth=(RAD_USER, RAD_PASS), timeout=10)
    r.raise_for_status()
    cal = Calendar.from_ical(r.content)
    out = []
    for comp in cal.walk():
        if comp.name != "VEVENT": continue
        uid = str(comp.get("uid") or "")
        summary = str(comp.get("summary") or "")
        dtstart = comp.get("dtstart").dt if comp.get("dtstart") else None
        dtend   = comp.get("dtend").dt   if comp.get("dtend") else None
        lm      = comp.get("last-modified")
        lastmod = lm.dt.isoformat() if lm else ""
        to_iso = lambda d: (d.isoformat() if hasattr(d, "isoformat") else (str(d) if d else ""))
        out.append({
            "uid": uid,
            "title": summary,
            "start": to_iso(dtstart),
            "end":   to_iso(dtend),
            "last_modified": lastmod
        })
    return out

def _rad_put(uid, vevent_text):
    if not RAD_URL:
        raise RuntimeError("RAD_URL not configured")
    url = f"{RAD_URL}{RAD_PATH}{uid}.ics"
    r = requests.put(url, auth=(RAD_USER, RAD_PASS),
                     headers={"Content-Type":"text/calendar; charset=utf-8"},
                     data=vevent_text.encode("utf-8"), timeout=10)
    r.raise_for_status()
    return {"ok": True, "status": r.status_code}

def main():
    client.connect(MQTT_HOST, MQTT_PORT, 60)
    client.loop_start()
    threading.Thread(target=http_thread, daemon=True).start()
    threading.Thread(target=ntfy_status_thread, daemon=True).start()
    last = 0.0
    if PUBLISH_INTERVAL == 0:
        log("Dummy ticker disabled (PUBLISH_INTERVAL=0)")
    else:
        log("Dummy ticker every", PUBLISH_INTERVAL, "s")
    while True:
        if PUBLISH_INTERVAL and (time.time()-last) >= PUBLISH_INTERVAL:
            state = {"status":"busy","now":{"title":"In a meeting"},"next":{"title":"Next thing"},"at": now_iso()}
            state = _enrich_state_with_local_times(state)
            client.publish(TOPIC_STATE, json.dumps(state), qos=0, retain=True)
            _record_state(state, "ticker")
            last = time.time()
        time.sleep(0.05)

if __name__ == "__main__":
    main()
