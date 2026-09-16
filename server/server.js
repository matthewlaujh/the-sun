// the sun — preset server. Serves the simulator (../simulator) and a small JSON API backed by one SQLite file.
// Env: PORT (8080), DATA_DIR (./data), SUN_ADMIN_KEY (required for delete/restore/purge), CORS_ORIGINS (comma list, optional)
import express from 'express';
import Database from 'better-sqlite3';
import { mkdirSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { randomUUID, timingSafeEqual } from 'node:crypto';

const here = dirname(fileURLToPath(import.meta.url));
const PORT = +(process.env.PORT || 8080), DATA_DIR = process.env.DATA_DIR || join(here, 'data');
const ADMIN = process.env.SUN_ADMIN_KEY || '';
const ORIGINS = (process.env.CORS_ORIGINS || '').split(',').map(s => s.trim()).filter(Boolean);
mkdirSync(DATA_DIR, { recursive: true });
const db = new Database(join(DATA_DIR, 'sun.sqlite'));
db.pragma('journal_mode = WAL');
db.exec(`CREATE TABLE IF NOT EXISTS presets (
  id TEXT PRIMARY KEY, name TEXT NOT NULL, author TEXT DEFAULT '', notes TEXT DEFAULT '', pattern TEXT NOT NULL,
  data TEXT NOT NULL, created TEXT NOT NULL, deleted TEXT, ip TEXT, ua TEXT);
CREATE INDEX IF NOT EXISTS presets_created ON presets(created);
CREATE TABLE IF NOT EXISTS events (id INTEGER PRIMARY KEY AUTOINCREMENT, at TEXT NOT NULL, kind TEXT NOT NULL, preset_id TEXT, detail TEXT, ip TEXT);`);
const q = {
  list: db.prepare(`SELECT id, name, author, notes, pattern, created, deleted FROM presets WHERE (@all = 1 OR deleted IS NULL) ORDER BY created DESC`),
  get: db.prepare(`SELECT * FROM presets WHERE id = ?`),
  all: db.prepare(`SELECT * FROM presets ORDER BY created DESC`),
  ins: db.prepare(`INSERT INTO presets (id, name, author, notes, pattern, data, created, ip, ua) VALUES (@id, @name, @author, @notes, @pattern, @data, @created, @ip, @ua)`),
  del: db.prepare(`UPDATE presets SET deleted = ? WHERE id = ? AND deleted IS NULL`),
  restore: db.prepare(`UPDATE presets SET deleted = NULL WHERE id = ?`),
  purge: db.prepare(`DELETE FROM presets WHERE id = ? AND deleted IS NOT NULL`),
  ev: db.prepare(`INSERT INTO events (at, kind, preset_id, detail, ip) VALUES (?, ?, ?, ?, ?)`),
  events: db.prepare(`SELECT * FROM events ORDER BY id DESC LIMIT ?`),
  stats: db.prepare(`SELECT COUNT(*) AS total, COALESCE(SUM(deleted IS NULL),0) AS live, COALESCE(SUM(deleted IS NOT NULL),0) AS archived, COUNT(DISTINCT author) AS authors, COUNT(DISTINCT pattern) AS patterns, MIN(created) AS first, MAX(created) AS last FROM presets`),
  byPattern: db.prepare(`SELECT pattern, COUNT(*) AS n FROM presets WHERE deleted IS NULL GROUP BY pattern ORDER BY n DESC`),
  byAuthor: db.prepare(`SELECT COALESCE(NULLIF(author,''),'anon') AS author, COUNT(*) AS n FROM presets WHERE deleted IS NULL GROUP BY 1 ORDER BY n DESC`),
};
const ip = req => (req.headers['x-forwarded-for'] || req.socket.remoteAddress || '').toString().split(',')[0].trim();
const log = (kind, id, detail, req) => q.ev.run(new Date().toISOString(), kind, id || null, detail ? String(detail).slice(0, 500) : null, ip(req));
const isAdmin = req => { const k = String(req.headers['x-admin-key'] || ''); if (!ADMIN || !k || k.length !== ADMIN.length) return false; return timingSafeEqual(Buffer.from(k), Buffer.from(ADMIN)); };
const row2preset = (r, full) => { const p = { id: r.id, name: r.name, author: r.author, notes: r.notes, pattern: r.pattern, created: r.created, deleted: r.deleted || undefined }; if (full) Object.assign(p, JSON.parse(r.data)); return p; };

const app = express();
app.set('trust proxy', true);
app.use(express.json({ limit: '256kb' }));
app.use((req, res, next) => {                                     // CORS: same-origin needs nothing; other copies of the page are allowed if listed, or any origin when the list is empty
  const o = req.headers.origin;
  if (o && (ORIGINS.length === 0 || ORIGINS.includes(o))) { res.setHeader('Access-Control-Allow-Origin', o); res.setHeader('Vary', 'Origin'); res.setHeader('Access-Control-Allow-Headers', 'content-type, x-admin-key'); res.setHeader('Access-Control-Allow-Methods', 'GET, POST, DELETE, OPTIONS'); }
  if (req.method === 'OPTIONS') return res.sendStatus(204);
  next();
});
const buckets = new Map();                                        // crude write rate limit: 60 saves per 10 min per IP
const limited = req => { const k = ip(req), now = Date.now(), recent = (buckets.get(k) || []).filter(t => now - t < 600000); recent.push(now); buckets.set(k, recent); return recent.length > 60; };

app.get('/api/health', (req, res) => res.json({ ok: true, presets: q.stats.get().live, admin: !!ADMIN, time: new Date().toISOString() }));
app.get('/api/presets', (req, res) => res.json(q.list.all({ all: req.query.all === '1' ? 1 : 0 }).map(r => row2preset(r, false))));
app.get('/api/presets/:id', (req, res) => { const r = q.get.get(req.params.id); if (!r) return res.status(404).json({ error: 'not found' }); res.json(row2preset(r, true)); });
app.post('/api/presets', (req, res) => {
  if (limited(req)) return res.status(429).json({ error: 'too many saves, try again in a few minutes' });
  const p = req.body || {}; if (!p.pattern || typeof p.pattern !== 'string') return res.status(400).json({ error: 'pattern required' });
  const { name = '', author = '', notes = '', ...rest } = p; delete rest.id; delete rest.created; delete rest.deleted;
  const row = { id: randomUUID(), name: String(name).slice(0, 120) || p.pattern, author: String(author).slice(0, 80), notes: String(notes).slice(0, 4000), pattern: p.pattern.slice(0, 80), data: JSON.stringify(rest), created: new Date().toISOString(), ip: ip(req), ua: String(req.headers['user-agent'] || '').slice(0, 200) };
  q.ins.run(row); log('save', row.id, row.name, req);
  res.status(201).json(row2preset(q.get.get(row.id), true));
});
app.delete('/api/presets/:id', (req, res) => { if (!isAdmin(req)) return res.status(401).json({ error: 'admin key required' }); const n = q.del.run(new Date().toISOString(), req.params.id).changes; if (!n) return res.status(404).json({ error: 'not found or already archived' }); log('delete', req.params.id, null, req); res.json({ ok: true }); });
app.post('/api/presets/:id/restore', (req, res) => { if (!isAdmin(req)) return res.status(401).json({ error: 'admin key required' }); q.restore.run(req.params.id); log('restore', req.params.id, null, req); res.json({ ok: true }); });
app.delete('/api/presets/:id/purge', (req, res) => { if (!isAdmin(req)) return res.status(401).json({ error: 'admin key required' }); const n = q.purge.run(req.params.id).changes; log('purge', req.params.id, null, req); res.json({ ok: !!n }); });
app.get('/api/export', (req, res) => { res.setHeader('content-disposition', `attachment; filename="sun-presets-${new Date().toISOString().slice(0, 10)}.json"`); res.json({ exported: new Date().toISOString(), presets: q.all.all().map(r => row2preset(r, true)) }); });
app.get('/api/stats', (req, res) => res.json({ ...q.stats.get(), byPattern: q.byPattern.all(), byAuthor: q.byAuthor.all() }));
app.get('/api/events', (req, res) => { if (!isAdmin(req)) return res.status(401).json({ error: 'admin key required' }); res.json(q.events.all(Math.min(500, +(req.query.limit || 200)))); });
app.get('/api/admin/check', (req, res) => res.json({ admin: isAdmin(req) }));

const site = join(here, '..', 'simulator');
if (existsSync(site)) app.use(express.static(site, { extensions: ['html'], maxAge: '5m' }));
app.listen(PORT, () => console.log(`the sun · presets on :${PORT} · db ${join(DATA_DIR, 'sun.sqlite')} · admin ${ADMIN ? 'set' : 'NOT SET (deletes disabled)'} · site ${existsSync(site) ? site : 'not found'}`));
