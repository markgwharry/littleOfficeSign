# Power Automate Flows

This folder describes two flows:

1) **M365 → Radicale (CalDAV)** one-way sync
2) **M365 → Sign (via ntfy)** status push

---

## 1) M365 → Radicale (CalDAV)

**Trigger:** Recurrence (every 5 min)

**Get calendar view of events (V3):**
- Start: `utcNow()`
- End: `addDays(utcNow(), 14)`

**Apply to each** over `body('Get_calendar_view_of_events_(V3)')?['value']`

Inside:
- Optional **Condition** to skip `showAs == 'free'` or cancelled
- **Compose (VEVENT)**:
```
BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//OfficeSign Bridge//EN
BEGIN:VEVENT
UID:@{coalesce(items('Apply_to_each')?['iCalUId'], items('Apply_to_each')?['id'])}
DTSTAMP:@{formatDateTime(utcNow(), 'yyyyMMddTHHmmssZ')}
DTSTART:@{formatDateTime(items('Apply_to_each')?['startWithTimeZone'], 'yyyyMMddTHHmmssZ')}
DTEND:@{formatDateTime(items('Apply_to_each')?['endWithTimeZone'], 'yyyyMMddTHHmmssZ')}
SUMMARY:@{replace(string(items('Apply_to_each')?['subject']), '\n', '\\n')}
END:VEVENT
END:VCALENDAR
```
- **HTTP (PUT)** to Radicale:
  - Method: `PUT`
  - URL: `https://calendar.nas/<username>/calendar/@{coalesce(items('Apply_to_each')?['iCalUId'], items('Apply_to_each')?['id'])}.ics`
  - Auth: Basic (Radicale credentials)
  - Headers: `Content-Type: text/calendar; charset=utf-8`
  - Body: outputs of the Compose step

---

## 2) M365 → Sign via ntfy

**Trigger:** Recurrence (every 2 min)

**Get calendar view of events (V3):**
- Start: `utcNow()`
- End: `addHours(utcNow(), 8)`

**Filter array (current)** over `...['value']` (Advanced):
```
@and(
  lessOrEquals(ticks(utcNow()), ticks(item()?['endWithTimeZone'])),
  greaterOrEquals(ticks(utcNow()), ticks(item()?['startWithTimeZone']))
)
```

**Filter array (next)** (Advanced):
```
@and(
  greater(ticks(item()?['startWithTimeZone']), ticks(utcNow())),
  not(equals(toLower(item()?['showAs']), 'free'))
)
```

**Compose (state):**
```json
{
  "status": "@{if(greater(length(body('Filter_array_current')), 0), 'busy', 'free')}",
  "now": {
    "title": "@{if(greater(length(body('Filter_array_current')),0), first(body('Filter_array_current'))?['subject'], '')}",
    "end": "@{if(greater(length(body('Filter_array_current')),0), first(body('Filter_array_current'))?['endWithTimeZone'], '')}"
  },
  "next": {
    "title": "@{if(greater(length(body('Filter_array_next')),0), first(body('Filter_array_next'))?['subject'], '')}",
    "start": "@{if(greater(length(body('Filter_array_next')),0), first(body('Filter_array_next'))?['startWithTimeZone'], '')}"
  }
}
```

**HTTP (POST)** to ntfy:
- URL: `https://ntfy.sh/<your-status-topic>`
- Header: `Content-Type: application/json`
- Body: outputs of Compose
