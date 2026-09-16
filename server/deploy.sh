#!/bin/sh
# Deploy from your Mac:  ./server/deploy.sh root@DROPLET_IP
# Copies simulator/ + server/ to /opt/the-sun on the droplet and (re)starts the containers. Needs server/.env to exist locally.
set -e
HOST=${1:?usage: deploy.sh user@host}
cd "$(dirname "$0")/.."
[ -f server/.env ] || { echo "create server/.env first (see server/.env.example)"; exit 1; }
SSH="ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=15"
echo "→ checking $HOST"; $SSH "$HOST" 'echo "  connected to $(hostname)"' || { echo "cannot reach $HOST — is the droplet finished booting? (try again in a minute)"; exit 1; }
$SSH "$HOST" 'mkdir -p /opt/the-sun && (command -v docker >/dev/null || (curl -fsSL https://get.docker.com | sh))'
echo "→ syncing files"; rsync -az --delete -e "$SSH" --exclude 'server/data' --exclude 'server/node_modules' simulator server "$HOST:/opt/the-sun/"
echo "→ starting containers"; $SSH "$HOST" 'cd /opt/the-sun/server && docker compose up -d --build && docker compose ps'
DOM=$(grep -E '^DOMAIN=' server/.env | cut -d= -f2 | cut -d'#' -f1 | tr -d ' ')
case "$DOM" in ""|:80) echo "done — open http://${HOST#*@}/";; *) echo "done — open https://$DOM/  (certificate arrives once DNS points here)";; esac
