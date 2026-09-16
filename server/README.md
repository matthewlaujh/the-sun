# the sun — preset server

One Node process + one SQLite file. Serves the simulator at `/` and the preset API at `/api`.

| route | auth | what |
|---|---|---|
| `GET /api/health` | – | `{ok, presets, admin}` |
| `GET /api/presets[?all=1]` | – | list (summaries); `all=1` includes archived |
| `GET /api/presets/:id` | – | full preset |
| `POST /api/presets` | – | save (rate-limited 60 / 10 min / IP) |
| `DELETE /api/presets/:id` | admin | archive (soft delete) |
| `POST /api/presets/:id/restore` | admin | un-archive |
| `DELETE /api/presets/:id/purge` | admin | remove an archived preset for good |
| `GET /api/export` | – | everything as one JSON file |
| `GET /api/stats` | – | counts, by pattern, by author |
| `GET /api/events` | admin | save/delete log |

Admin = header `X-Admin-Key` equal to `SUN_ADMIN_KEY`. The page stores the key in the browser after you enter it once in Saved › Server.

## Run locally
```
cd server && npm install && SUN_ADMIN_KEY=test node server.js   # http://localhost:8080
```

## Deploy (DigitalOcean droplet, Ubuntu)
1. `cp server/.env.example server/.env` and edit (passphrase, domain).
2. `./server/deploy.sh root@DROPLET_IP` — installs Docker if missing, syncs files, starts app + Caddy (automatic HTTPS when DOMAIN is a real hostname pointed at the droplet).
3. Re-run step 2 after any change to the simulator.

Data lives in the `sun-data` Docker volume (`/data/sun.sqlite`). Back up with `docker compose cp app:/data/sun.sqlite ./backup.sqlite` or just `GET /api/export`.
