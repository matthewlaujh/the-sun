#!/bin/sh
# Deploy from your Mac:  ./server/deploy.sh root@DROPLET_IP
# Copies simulator/ + server/ to /opt/the-sun on the droplet and (re)starts the containers. Needs server/.env to exist locally.
set -e
HOST=${1:?usage: deploy.sh user@host}
cd "$(dirname "$0")/.."
[ -f server/.env ] || { echo "create server/.env first (see server/.env.example)"; exit 1; }
ssh "$HOST" 'mkdir -p /opt/the-sun && command -v docker >/dev/null || (curl -fsSL https://get.docker.com | sh)'
rsync -az --delete --exclude 'server/data' --exclude 'server/node_modules' simulator server "$HOST:/opt/the-sun/"
ssh "$HOST" 'cd /opt/the-sun/server && docker compose up -d --build && docker compose ps'
echo "done — open http(s)://$(grep -E '^DOMAIN=' server/.env | cut -d= -f2 | tr -d ' ' | sed 's/^:80$//')"
