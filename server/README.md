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

## Continuous deploy (GitHub Actions)
`.github/workflows/deploy.yml` redeploys on every push to `main` that touches `simulator/` or `server/` (and refreshes the `gh-pages` copy). One-time setup:
1. Make a deploy key on your Mac: `ssh-keygen -t ed25519 -f ~/.ssh/thesun-deploy -N "" -C thesun-deploy` and authorise it on the droplet: `ssh-copy-id -i ~/.ssh/thesun-deploy.pub root@DROPLET_IP`.
2. GitHub → repo → Settings → Secrets and variables → Actions → New repository secret:
   - `DROPLET_HOST` = the droplet's public IP (or `sun.matthewlaujh.com`)
   - `DROPLET_SSH_KEY` = contents of `~/.ssh/thesun-deploy` (the private key, whole file)
   - optional `DROPLET_USER` (defaults to `root`)
3. `server/.env` must already exist on the droplet (the first `deploy.sh` run puts it there); Actions never touches it.
Run it by hand from the Actions tab (workflow_dispatch) any time.
