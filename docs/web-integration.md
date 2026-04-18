# Web integration

Three ways to get the JSON from a web application. Pick whichever matches your hosting topology.

## A — RCON pull (recommended)

The web app connects to the game server over the Source RCON protocol (TCP, same port as game UDP by default) and sends `mm_getinfo`. The server replies with the JSON as the RCON response body.

**Pros:** zero infra, real-time, no cron.
**Cons:** web host must be able to reach the game server on the RCON port.

### PHP — `thedudeguy/rcon`
```php
require __DIR__ . '/vendor/autoload.php';

$rcon = new \Thedudeguy\Rcon('1.2.3.4', 27015, 'rcon_pw', 3);
if (!$rcon->connect()) {
    throw new RuntimeException('rcon auth failed');
}
$raw  = $rcon->sendCommand('mm_getinfo');
$data = json_decode(trim($raw), true);
```

### Node.js — `rcon-srcds`
```js
import RCON from 'rcon-srcds';

const server = new RCON({ host: '1.2.3.4', port: 27015, timeout: 5000 });
await server.authenticate('rcon_pw');
const raw  = await server.execute('mm_getinfo');
const data = JSON.parse(raw.trim());
```

### Python — `valve.rcon`
```python
from valve.rcon import RCON
import json

with RCON(('1.2.3.4', 27015), 'rcon_pw') as rcon:
    data = json.loads(rcon('mm_getinfo').strip())
```

### Go — `gorcon/rcon`
```go
conn, err := rcon.Dial("1.2.3.4:27015", "rcon_pw")
if err != nil { log.Fatal(err) }
defer conn.Close()

raw, err := conn.Execute("mm_getinfo")
var data ServerInfo
_ = json.Unmarshal([]byte(strings.TrimSpace(raw)), &data)
```

**Note:** a 60-player JSON (~12 KB) does not fit in a single RCON packet. Pick an RCON library that reassembles multi-packet responses (all the libraries above do).

---

## B — File + static HTTP

Write a JSON file on disk and serve it with nginx/Apache. The web side is a plain `fetch()`.

**Pros:** web and game server can live on different hosts. Zero web-side RCON code.
**Cons:** needs a shared filesystem or a scheduler that pokes the server over RCON and writes the file locally.

1. Share a directory between the game server container and the web server container (docker-compose volume, NFS mount, etc.).
2. Run a cron job on the game host:
   ```cron
   * * * * * /usr/local/bin/rcon 127.0.0.1:27015 rcon_pw "mm_getinfo_file /srv/web/stats.json" >/dev/null
   ```
   Use any RCON CLI (e.g. [gorcon/rcon-cli](https://github.com/gorcon/rcon-cli)).
3. Nginx config:
   ```nginx
   location = /stats.json {
       alias /srv/web/stats.json;
       add_header Cache-Control "public, max-age=5";
   }
   ```
4. Web frontend:
   ```js
   const data = await fetch('/stats.json').then(r => r.json());
   ```

The file is written atomically (`.tmp` + `rename`), so readers never see a torn JSON.

---

## C — `mm_getinfo_slot` for fast single-player polling

Admin dashboards that only care about one player (view-a-player page, live kill feed filtered by slot) should use `mm_getinfo_slot <N>` instead of pulling the full list.

```js
// poll slot 5 once per second
setInterval(async () => {
  const raw  = await rcon.execute('mm_getinfo_slot 5');
  const data = JSON.parse(raw.trim());
  if (Object.keys(data).length === 0) {
    // slot empty
  } else {
    render(data);
  }
}, 1000);
```

Scales linearly — N polls per second = N `mm_getinfo_slot` calls — each ~200 bytes and under a millisecond of CPU.

---

## Recommendation

- **Same host, real-time dashboard** → A (RCON pull)
- **Separate web host, or you already have static file pipeline** → B (file + nginx)
- **Per-player widget** → C (`mm_getinfo_slot`)
