# curl examples

Set your token first:

```bash
export BRIDGE_TOKEN="your-token-here"
export BRIDGE_URL="http://127.0.0.1:8787"
```

## Health check (unauthenticated)

```bash
curl "$BRIDGE_URL/api/health"
```

## Raw behaviour controls (broadcast to WS clients)

These endpoints are designed to exercise `robot_v3` behaviour without any agent
events. They broadcast JSON frames to all connected WebSocket clients.

Discover the full catalog:

```bash
curl -H "Authorization: Bearer $BRIDGE_TOKEN" \
  "$BRIDGE_URL/api/raw/capabilities"
```

Set valence (V) and arousal (A):

```bash
curl -X POST "$BRIDGE_URL/api/raw/emotion/set-valence" \
  -H "Authorization: Bearer $BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"v": 0.35}'

curl -X POST "$BRIDGE_URL/api/raw/emotion/set-arousal" \
  -H "Authorization: Bearer $BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"a": 0.25}'
```

Start a verb and fire an overlay:

```bash
curl -X POST "$BRIDGE_URL/api/raw/verb/start" \
  -H "Authorization: Bearer $BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"verb":"reading"}'

curl -X POST "$BRIDGE_URL/api/raw/verb/overlay" \
  -H "Authorization: Bearer $BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"verb":"attracting_attention","duration_ms":1200}'
```

Send an arbitrary WS frame verbatim:

```bash
curl -X POST "$BRIDGE_URL/api/raw/broadcast" \
  -H "Authorization: Bearer $BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"type":"emotion.command","action":"modifyValence","params":{"delta_v":0.1}}'
```

## Inject a message into the running Claude Code session

```bash
curl -X POST "$BRIDGE_URL/api/messages" \
  -H "Authorization: Bearer $BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"content": "What files are in this directory?"}'
```

Response:

```json
{ "chat_id": "chat_<uuid>", "status": "forwarded" }
```

Pass an explicit `chat_id` to thread messages into an existing conversation:

```bash
curl -X POST "$BRIDGE_URL/api/messages" \
  -H "Authorization: Bearer $BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"content": "And now the subdirectories?", "chat_id": "chat_abc"}'
```

## Read the conversation log

```bash
curl -H "Authorization: Bearer $BRIDGE_TOKEN" \
  "$BRIDGE_URL/api/messages/chat_abc"
```

## Inspect bridge state

```bash
curl -H "Authorization: Bearer $BRIDGE_TOKEN" "$BRIDGE_URL/api/state"
```

## List currently-pending permission requests

```bash
curl -H "Authorization: Bearer $BRIDGE_TOKEN" "$BRIDGE_URL/api/permissions"
```

## Approve or deny a permission request

`request_id` is the 5-letter id (e.g. `abcde`) Claude Code emits.

```bash
curl -X POST "$BRIDGE_URL/api/permissions/abcde" \
  -H "Authorization: Bearer $BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"behavior": "allow"}'
```

Response:

```json
{ "request_id": "abcde", "behavior": "allow", "applied": true }
```

`applied: false` means Claude Code already closed the request (the terminal user
answered first). The verdict was sent but not used.
