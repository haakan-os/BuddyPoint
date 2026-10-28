# BuddyPoint AO3 bridge

A single-user Docker service for a home server or NAS. It handles AO3 login,
private bookmarks, work metadata, EPUB downloads and X3 image optimisation.
BuddyPoint uses small JSON jobs and streams completed EPUBs into `/AO3`.
The existing reader themes and four-part fic badges are used.

## Start on your NAS

From a checkout of the `feature/ao3-integration` branch:

```sh
cd companion/ao3-bridge
docker compose up -d --build
docker compose exec bridge python bridge.py token
```

Open `http://YOUR-NAS-IP:8080` in a browser. Enter the printed device token,
then sign in to AO3. The token grants access to this single-account bridge;
keep it private. The password is only used for login. Cookies, original EPUBs
and optimised copies live in the persistent `bridge-data` volume.

The container runs as UID 10001 with a read-only application filesystem. For a
NAS bind mount instead of the named volume, give UID 10001 write access to the
data directory. Do not run `docker compose down -v` unless you intend to erase
the bridge data. Back up the volume as private account data.

The default port is 8080. Set `BRIDGE_PORT=8081` or `BRIDGE_BIND=192.168.1.10`
in a Compose `.env` file to change the host port or bind address. For example:

```dotenv
BRIDGE_PORT=8080
OPTIMIZE_EPUB=true
AO3_FORCE_IPV4=false
```

HTTP is intended for a trusted home LAN. For access outside it, use HTTPS through
your reverse proxy and preserve the original `Host` header. The firmware verifies
HTTPS certificates. Do not forward the plain HTTP port to the internet.

### Test an IPv4 route to AO3

If AO3 returns HTTP 525, compare the host's default connection with an IPv4-only
request. HTTP 525 is an upstream TLS error; it does not by itself identify IPv6
as the cause. This command prints the status and destination IP without cookies:

```sh
curl -4 -sS -o /dev/null --connect-timeout 15 --max-time 45 \
  -w 'HTTP %{http_code}; remote IP %{remote_ip}\n' \
  https://archiveofourown.org/users/login
```

To test the bridge over IPv4, set `AO3_FORCE_IPV4=true` in the `.env` file next
to `compose.yaml`, then run `docker compose up -d --build`. Startup logs will
show `AO3 outbound networking: IPv4 only`. The setting applies to all outbound
Requests/urllib3 connections in the bridge process, including login, bookmarks
and EPUB downloads. It retains DNS hostname resolution, TLS certificate and
hostname verification, and configured proxies. With an HTTP(S) proxy, it selects
IPv4 for the connection to the proxy; the proxy controls its onward route to AO3.
It does not disable IPv6 on the host or change Tailscale or reader networking.

Set `AO3_FORCE_IPV4=false` and run `docker compose up -d` to restore normal
address selection. No firmware update or data-volume reset is needed. A separate
`docker compose exec ... python -c` command does not run the bridge's startup
configuration; use the bridge web page to test login after restarting.

## Configure the Xteink

Install the bridge-capable application firmware. The easiest way to paste URLs
and the device token is the reader's existing paired HTTPS panel:

1. Open **File Transfer → Join a Network**, then **AO3**.
2. Open the displayed HTTPS address, verify the fingerprint and pair.
3. In **Docker bridge**, enable the bridge, enter a default API URL such as
   `http://192.168.1.10:8080`, paste the device token and save.
4. Optionally enter a Wi-Fi network name and its API URL. This adds/replaces one
   override. A blank override URL removes that network's override. A blank token
   field keeps the saved token.
5. Leave File Transfer and open **Apps → AO3**. Use **My AO3 bookmarks** or
   **Download bookmarked fics**. Sign in on the NAS web page first.

The same settings are available directly under **AO3 → Bridge settings**.
Select an existing network row to edit/remove it. Up to eight network overrides
are supported. URL selection uses the exact connected SSID (including case):

| Connected Wi-Fi | Configuration | Selected address |
| --- | --- | --- |
| Home WiFi | Override: `http://192.168.1.10:8080` | Local NAS |
| Phone Hotspot | Override: `https://books.example.net` | Remote NAS address |
| Any other network | Default: `https://books.example.net` | Default address |
| Any other network | Default left blank | Reports missing bridge configuration |

Alternate addresses should reach the **same bridge**, since it uses one device
token. A URL setting does not create a VPN or make your NAS remotely reachable.
Prefer a reserved LAN IP or a DNS name that the reader can resolve. URLs can
include a reverse-proxy base path; IPv4 addresses and DNS hostnames are supported.
For a base path, configure the proxy to strip that prefix and open the browser
page with a trailing slash (for example `https://books.example.net/ao3/`). API
requests must reach the service without redirects.

Bridge mode never silently falls back to direct AO3 requests. Disable bridge mode
to use the previous direct integration. Settings are saved at
`/.crosspoint/ao3-bridge.json`; the device token uses the existing device-derived
obfuscation (not encryption), and normal file-transfer endpoints block this path.
The bridge's **Forget AO3 session** button removes its saved cookies; deleting
the reader's old direct AO3 session does not sign the bridge out.

## Downloads and optimisation

The bridge downloads and validates the original EPUB before offering a file to
the reader. PNG/JPEG images are resized to fit 480×800 and converted to grayscale
without changing their file format or path. Text, chapter IDs, spine order,
links, AO3 notes, metadata, CSS and fonts are preserved. Other image formats are
left intact. The optimiser does not rewrite chapter structure or strip assets.

The original and optimised EPUB are both cached. Work metadata/download URL
changes create a new cache version; changing the optimiser version creates a new
optimised copy. `OPTIMIZE_EPUB=false` serves originals. The reader's update check
also compares this optimisation revision, so switching profiles replaces its copy
even when the AO3 chapter count is unchanged. Cached private works are
revalidated against AO3 when preparing a download. Old cache versions are retained
until you remove them from the data volume with the bridge stopped.

Files have `Content-Length`, `ETag` and HTTP Range support. The reader currently
restarts an interrupted EPUB transfer on retry; it publishes the EPUB only after
validating the completed ZIP and preserves the prior download on failure.
Existing reading progress is retained, though AO3 authors can still reorder or
replace chapters between updates.

## API and limits

Every `/api/v1/` request requires `Authorization: Bearer DEVICE_TOKEN`.
Credentials never go in URLs. Redirects from the bridge are rejected by the reader.

| Request | Response |
| --- | --- |
| `GET /healthz` | Public process health and API version |
| `GET /api/v1/status` | Account and optimisation status |
| `POST /api/v1/jobs` | `202 {"job":"32-hex-id"}` |
| `GET /api/v1/jobs/{id}` | queued/running/done/error and result |
| `GET /api/v1/files/{id}` | Completed authenticated EPUB; supports Range |

Job bodies use `operation`: `login` (`username`, `password`), `logout`,
`work`/`prepare` (`id`), or `list` (`url`: an AO3 bookmark/search URL).
Bookmark usernames in list URLs are replaced with the signed-in account.
Lists return at most five works and an opaque `next` URL; this keeps reader memory
bounded. One worker serializes AO3 requests with at least two seconds between
requests, with one retry for a GET returning HTTP 525. Login POSTs are not replayed.
Rate limits, expired sessions and challenges are reported; CAPTCHAs are not bypassed.

There are at most eight queued jobs, 64 retained jobs and a 30-minute result lifetime.
Cancelling on the reader stops its polling/transfer; an already-running NAS job may
finish and populate the cache. EPUBs are limited to 64 MiB compressed / 128 MiB
expanded. The bridge accepts no arbitrary upstream host or filesystem path.

## Validation

```sh
python -m pip install -r requirements.txt
python -m unittest discover -s tests -v
```

Tests use local HTTP servers and mocked AO3 responses, including private-session
expiry, trusted redirects, cookie handling, jobs, authorization, Range delivery,
ZIP validation, preserved chapter IDs and correct image formats. A real AO3 account
and physical X3 are still needed to validate end-to-end login and sync.

For a managed cloud Docker build that needs a proxy CA, the Dockerfile accepts an
optional BuildKit secret without baking that CA into the image:

```sh
docker build --secret id=proxy_ca,src=/path/to/combined-ca.pem \
  -f companion/ao3-bridge/Dockerfile -t buddypoint-ao3-bridge:local companion
```
