#include "WebUI.h"
#include "MeshNetwork.h"
#include "mbedtls/md5.h"
#include <cstring>
#include <cstdio>

bool WebUI::begin(uint16_t port, const char* username, const char* password, MeshNetwork* mesh) {
    _mesh = mesh;
    strncpy(_username, username, sizeof(_username) - 1);
    strncpy(_password, password, sizeof(_password) - 1);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.max_uri_handlers = 12;

    if (httpd_start(&_server, &config) != ESP_OK) {
        return false;
    }

    // Register URI handlers
    httpd_uri_t root      = { .uri = "/",              .method = HTTP_GET,  .handler = _handleRoot,       .user_ctx = this };
    httpd_uri_t status    = { .uri = "/api/v1/status", .method = HTTP_GET,  .handler = _handleStatus,     .user_ctx = this };
    httpd_uri_t nodes     = { .uri = "/api/v1/nodes",  .method = HTTP_GET,  .handler = _handleNodes,      .user_ctx = this };
    httpd_uri_t routes    = { .uri = "/api/v1/routes", .method = HTTP_GET,  .handler = _handleRoutes,     .user_ctx = this };
    httpd_uri_t peers     = { .uri = "/api/v1/peers",  .method = HTTP_GET,  .handler = _handlePeers,      .user_ctx = this };
    httpd_uri_t topology  = { .uri = "/api/v1/topology", .method = HTTP_GET, .handler = _handleTopology,  .user_ctx = this };
    httpd_uri_t cfgGet    = { .uri = "/api/v1/config", .method = HTTP_GET,  .handler = _handleConfig,     .user_ctx = this };
    httpd_uri_t cfgPost   = { .uri = "/api/v1/config", .method = HTTP_POST, .handler = _handleConfigPost, .user_ctx = this };

    httpd_register_uri_handler(_server, &root);
    httpd_register_uri_handler(_server, &status);
    httpd_register_uri_handler(_server, &nodes);
    httpd_register_uri_handler(_server, &routes);
    httpd_register_uri_handler(_server, &peers);
    httpd_register_uri_handler(_server, &topology);
    httpd_register_uri_handler(_server, &cfgGet);
    httpd_register_uri_handler(_server, &cfgPost);

    return true;
}

void WebUI::stop() {
    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
    }
    if (_prometheusServer) {
        httpd_stop(_prometheusServer);
        _prometheusServer = nullptr;
    }
}

bool WebUI::startPrometheus(uint16_t port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;

    if (httpd_start(&_prometheusServer, &config) != ESP_OK) {
        return false;
    }

    httpd_uri_t metrics = { .uri = "/metrics", .method = HTTP_GET, .handler = _handleMetrics, .user_ctx = this };
    httpd_register_uri_handler(_prometheusServer, &metrics);

    return true;
}

bool WebUI::_checkDigestAuth(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;

    size_t authLen = httpd_req_get_hdr_value_len(req, "Authorization");
    if (authLen == 0) {
        char authHeader[128];
        snprintf(authHeader, sizeof(authHeader),
                 "Digest realm=\"%s\", nonce=\"%08lx\", qop=\"auth\"",
                 self->_realm, (unsigned long)millis());
        httpd_resp_set_hdr(req, "WWW-Authenticate", authHeader);
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return false;
    }

    // Simplified: accept any Authorization header for now
    // TODO: Full HTTP Digest validation
    return true;
}

// ─── Dashboard HTML ───────────────────────────────────────────────────────────

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>EnigmaNG Mesh</title>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:20px;background:#1a1a2e;color:#eee}
h1{color:#0ff;margin-bottom:5px}
.subtitle{color:#888;margin-bottom:20px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px;margin-bottom:20px}
.card{background:#16213e;border-radius:8px;padding:16px;border:1px solid #0f3460}
.card h3{margin:0 0 12px;color:#0ff;font-size:14px;text-transform:uppercase;letter-spacing:1px}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{padding:6px 8px;text-align:left;border-bottom:1px solid #0f3460}
th{color:#888;font-weight:normal}
.status-ok{color:#0f0} .status-warn{color:#ff0} .status-err{color:#f00}
.topology{background:#0d1b2a;border-radius:8px;padding:16px;min-height:200px;position:relative}
.node-dot{width:12px;height:12px;border-radius:50%;background:#0ff;position:absolute}
.node-label{font-size:11px;color:#aaa;position:absolute}
#refresh-btn{background:#0ff;color:#000;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;font-weight:bold}
#refresh-btn:hover{background:#0ae}
.stat{font-size:28px;font-weight:bold;color:#fff}
.stat-label{font-size:12px;color:#888}
</style>
</head>
<body>
<h1>🔗 EnigmaNG Mesh</h1>
<p class="subtitle" id="info">Loading...</p>

<div class="grid">
  <div class="card"><h3>Nodes</h3><div class="stat" id="node-count">-</div><div class="stat-label">Connected peers</div></div>
  <div class="card"><h3>Routes</h3><div class="stat" id="route-count">-</div><div class="stat-label">Active routes</div></div>
  <div class="card"><h3>Uptime</h3><div class="stat" id="uptime">-</div><div class="stat-label">Seconds</div></div>
  <div class="card"><h3>Free Heap</h3><div class="stat" id="heap">-</div><div class="stat-label">Bytes</div></div>
</div>

<div class="card">
<h3>Network Topology</h3>
<div id="topology-container"></div>
<table id="topology-table"><thead><tr><th>Node</th><th>IP</th><th>RSSI</th><th>Hops</th><th>Via</th></tr></thead><tbody id="topo-body"></tbody></table>
</div>

<div class="grid" style="margin-top:16px">
  <div class="card">
    <h3>Peers (Direct Neighbors)</h3>
    <table><thead><tr><th>MAC</th><th>RSSI</th><th>Key</th><th>Last Seen</th></tr></thead><tbody id="peers-body"></tbody></table>
  </div>
  <div class="card">
    <h3>Routes</h3>
    <table><thead><tr><th>Dest IP</th><th>Dest MAC</th><th>Next Hop</th><th>Hops</th></tr></thead><tbody id="routes-body"></tbody></table>
  </div>
</div>

<p style="margin-top:16px"><button id="refresh-btn" onclick="refresh()">Refresh</button></p>

<script>
function mac(m){return m.map(b=>b.toString(16).padStart(2,'0')).join(':')}
function ago(ms){let s=Math.floor(ms/1000);return s<60?s+'s':Math.floor(s/60)+'m'+s%60+'s'}

async function refresh(){
  try{
    let [st,nd,rt,tp]=await Promise.all([
      fetch('/api/v1/status').then(r=>r.json()),
      fetch('/api/v1/nodes').then(r=>r.json()),
      fetch('/api/v1/routes').then(r=>r.json()),
      fetch('/api/v1/topology').then(r=>r.json())
    ]);
    document.getElementById('node-count').textContent=st.nodes;
    document.getElementById('route-count').textContent=st.routes;
    document.getElementById('uptime').textContent=st.uptime;
    document.getElementById('heap').textContent=st.freeHeap;
    document.getElementById('info').textContent='Channel: '+st.channel+' | Mode: '+st.mode+' | MAC: '+st.mac+' | IP: '+st.ip;

    let tb=document.getElementById('topo-body');
    tb.innerHTML=tp.map(n=>'<tr><td>'+mac(n.mac)+'</td><td>'+n.ip+'</td><td>'+n.rssi+'</td><td>'+n.hops+'</td><td>'+(n.nextHop?mac(n.nextHop):'direct')+'</td></tr>').join('');

    let pb=document.getElementById('peers-body');
    pb.innerHTML=nd.map(n=>'<tr><td>'+mac(n.mac)+'</td><td>'+n.rssi+'</td><td class="'+(n.keyOk?'status-ok':'status-err')+'">'+(n.keyOk?'✓':'✗')+'</td><td>'+ago(n.age)+'</td></tr>').join('');

    let rb=document.getElementById('routes-body');
    rb.innerHTML=rt.map(r=>'<tr><td>'+r.destIP+'</td><td>'+mac(r.destMac)+'</td><td>'+mac(r.nextHop)+'</td><td>'+r.hops+'</td></tr>').join('');
  }catch(e){console.error(e)}
}
refresh();
setInterval(refresh,5000);
</script>
</body>
</html>
)rawliteral";

esp_err_t WebUI::_handleRoot(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, DASHBOARD_HTML, strlen(DASHBOARD_HTML));
    return ESP_OK;
}

// ─── JSON API endpoints ───────────────────────────────────────────────────────

esp_err_t WebUI::_handleStatus(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    MeshNetwork* mesh = self->_mesh;
    char json[256];

    const uint8_t* mac = mesh ? mesh->getMAC() : nullptr;
    snprintf(json, sizeof(json),
             "{\"uptime\":%lu,\"nodes\":%d,\"routes\":%u,\"freeHeap\":%lu,"
             "\"channel\":%d,\"mode\":\"%s\","
             "\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
             "\"ip\":\"%s\"}",
             (unsigned long)(millis() / 1000),
             mesh ? mesh->getNodeCount() : 0,
             (unsigned)( mesh ? mesh->getRouter().getRouteCount() : 0),
             (unsigned long)ESP.getFreeHeap(),
             mesh ? mesh->getChannel() : 0,
             mesh ? (mesh->isGateway() ? "gateway" : "node") : "unknown",
             mac ? mac[0] : 0, mac ? mac[1] : 0, mac ? mac[2] : 0,
             mac ? mac[3] : 0, mac ? mac[4] : 0, mac ? mac[5] : 0,
             mesh ? mesh->getLocalIP().toString().c_str() : "0.0.0.0");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t WebUI::_handleNodes(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    MeshNetwork* mesh = self->_mesh;
    if (!mesh) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }

    PeerManager& pm = mesh->getPeerManager();
    size_t count = pm.getPeerCount();

    // Build JSON array
    char* buf = (char*)malloc(128 * count + 4);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_OK;
    }

    size_t pos = 0;
    buf[pos++] = '[';

    uint32_t now = millis();
    for (size_t i = 0; i < count; i++) {
        PeerEntry* peer = pm.getPeerByIndex(i);
        if (!peer || !peer->valid) continue;

        if (pos > 1) buf[pos++] = ',';
        int n = snprintf(buf + pos, 128,
                         "{\"mac\":[%u,%u,%u,%u,%u,%u],\"rssi\":%.0f,\"keyOk\":%s,\"age\":%lu}",
                         peer->mac[0], peer->mac[1], peer->mac[2],
                         peer->mac[3], peer->mac[4], peer->mac[5],
                         peer->rssiEwma,
                         peer->keyEstablished ? "true" : "false",
                         (unsigned long)(now - peer->lastSeen));
        pos += n;
    }
    buf[pos++] = ']';
    buf[pos] = '\0';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, pos);
    free(buf);
    return ESP_OK;
}

esp_err_t WebUI::_handleRoutes(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    MeshNetwork* mesh = self->_mesh;
    if (!mesh) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }

    Router& router = mesh->getRouter();
    size_t count = router.getRouteCount();

    char* buf = (char*)malloc(200 * count + 4);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_OK;
    }

    size_t pos = 0;
    buf[pos++] = '[';

    for (size_t i = 0; i < count; i++) {
        RouteEntry* r = router.getRouteByIndex(i);
        if (!r || !r->valid) continue;

        if (pos > 1) buf[pos++] = ',';
        int n = snprintf(buf + pos, 200,
                         "{\"destIP\":\"%s\","
                         "\"destMac\":[%u,%u,%u,%u,%u,%u],"
                         "\"nextHop\":[%u,%u,%u,%u,%u,%u],"
                         "\"hops\":%u}",
                         r->destIP.toString().c_str(),
                         r->destMac[0], r->destMac[1], r->destMac[2],
                         r->destMac[3], r->destMac[4], r->destMac[5],
                         r->nextHopMac[0], r->nextHopMac[1], r->nextHopMac[2],
                         r->nextHopMac[3], r->nextHopMac[4], r->nextHopMac[5],
                         r->hopCount);
        pos += n;
    }
    buf[pos++] = ']';
    buf[pos] = '\0';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, pos);
    free(buf);
    return ESP_OK;
}

esp_err_t WebUI::_handlePeers(httpd_req_t* req) {
    // Same as nodes for now — direct neighbors
    return _handleNodes(req);
}

esp_err_t WebUI::_handleTopology(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    MeshNetwork* mesh = self->_mesh;
    if (!mesh) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }

    // Topology = combined view: peers (direct) + routes (multi-hop)
    Router& router = mesh->getRouter();
    PeerManager& pm = mesh->getPeerManager();
    size_t routeCount = router.getRouteCount();

    char* buf = (char*)malloc(200 * (routeCount + 1) + 4);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_OK;
    }

    size_t pos = 0;
    buf[pos++] = '[';

    const uint8_t* localMac = mesh->getMAC();

    for (size_t i = 0; i < routeCount; i++) {
        RouteEntry* r = router.getRouteByIndex(i);
        if (!r || !r->valid) continue;

        // Skip the gateway's own entry (learned via route adv reflection)
        if (memcmp(r->destMac, localMac, 6) == 0) continue;

        // Get RSSI for direct peers
        float rssi = 0;
        PeerEntry* peer = pm.findPeer(r->destMac);
        if (peer) rssi = peer->rssiEwma;

        if (pos > 1) buf[pos++] = ',';
        int n = snprintf(buf + pos, 200,
                         "{\"mac\":[%u,%u,%u,%u,%u,%u],"
                         "\"ip\":\"%s\",\"rssi\":%.0f,\"hops\":%u,"
                         "\"nextHop\":%s}",
                         r->destMac[0], r->destMac[1], r->destMac[2],
                         r->destMac[3], r->destMac[4], r->destMac[5],
                         r->destIP.toString().c_str(),
                         rssi, r->hopCount,
                         (r->hopCount <= 1) ? "null" :
                         (String("[") + r->nextHopMac[0] + "," + r->nextHopMac[1] + "," +
                          r->nextHopMac[2] + "," + r->nextHopMac[3] + "," +
                          r->nextHopMac[4] + "," + r->nextHopMac[5] + "]").c_str());
        pos += n;
    }
    buf[pos++] = ']';
    buf[pos] = '\0';

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, pos);
    free(buf);
    return ESP_OK;
}

esp_err_t WebUI::_handleConfig(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    MeshNetwork* mesh = self->_mesh;
    char json[128];
    snprintf(json, sizeof(json),
             "{\"channel\":%d,\"mode\":\"%s\"}",
             mesh ? mesh->getChannel() : 0,
             mesh ? (mesh->isGateway() ? "gateway" : "node") : "unknown");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t WebUI::_handleConfigPost(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    MeshNetwork* mesh = self->_mesh;
    if (!mesh) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No mesh");
        return ESP_OK;
    }

    // Read POST body (max 128 bytes)
    char body[128] = {};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_OK;
    }

    // Simple parsing: look for "channel":N
    char* chPtr = strstr(body, "\"channel\":");
    if (chPtr) {
        int ch = atoi(chPtr + 10);
        if (ch >= 1 && ch <= 14) {
            mesh->setChannel((uint8_t)ch);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":true}", 11);
    return ESP_OK;
}

esp_err_t WebUI::_handleMetrics(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    MeshNetwork* mesh = self->_mesh;

    char metrics[512];
    snprintf(metrics, sizeof(metrics),
             "# HELP enigmang_nodes_total Total mesh nodes\n"
             "# TYPE enigmang_nodes_total gauge\n"
             "enigmang_nodes_total %d\n"
             "# HELP enigmang_routes_total Total routes in table\n"
             "# TYPE enigmang_routes_total gauge\n"
             "enigmang_routes_total %u\n"
             "# HELP enigmang_free_heap_bytes Free heap memory\n"
             "# TYPE enigmang_free_heap_bytes gauge\n"
             "enigmang_free_heap_bytes %lu\n"
             "# HELP enigmang_uptime_seconds Uptime in seconds\n"
             "# TYPE enigmang_uptime_seconds counter\n"
             "enigmang_uptime_seconds %lu\n",
             mesh ? mesh->getNodeCount() : 0,
             (unsigned)(mesh ? mesh->getRouter().getRouteCount() : 0),
             (unsigned long)ESP.getFreeHeap(),
             (unsigned long)(millis() / 1000));

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, metrics, strlen(metrics));
    return ESP_OK;
}
