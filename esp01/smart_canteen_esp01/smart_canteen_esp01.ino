/*
 * Smart Canteen Management System — ESP01 (ESP8266) Side
 *
 * ─── What this sketch does ────────────────────────────────────────────────────
 *  1. Connects to your WiFi network.
 *  2. Hosts a single-page web application on port 80 (file: /data/index.html
 *     stored in LittleFS — upload it with the "LittleFS Data Upload" tool).
 *  3. Exposes a REST API used by the web app:
 *       GET/POST/DELETE  /api/users
 *       GET/POST/DELETE  /api/menu
 *       GET              /api/orders
 *       POST             /api/orders/ready
 *       POST             /api/orders/cancel
 *  4. Communicates with the Arduino over hardware Serial (9600 baud):
 *       ← VERIFY:<UID>          → GRANTED:<name>:<balance>  or  DENIED
 *       ← MENU:GET              → MENU:id,name,price,qty|...
 *       ← ORDER:uid,total;id,qty;...
 *                               → ORDER_OK:<newBalance>  or  ORDER_FAIL
 *       → READY:<userName>      (sent to Arduino when kitchen clicks Ready)
 *
 * ─── Hardware ─────────────────────────────────────────────────────────────────
 *  ESP01 TX (GPIO1) → Arduino RX (pin 0)
 *  ESP01 RX (GPIO3) → Arduino TX (pin 1)
 *  Power: 3.3 V, ≥ 300 mA
 *
 * ─── Libraries required ───────────────────────────────────────────────────────
 *  ESP8266 core   (https://arduino.esp8266.com/stable/package_esp8266com_index.json)
 *  ArduinoJson    v6  by Benoit Blanchon  (install via Library Manager)
 *
 * ─── UPDATE THESE TWO LINES before uploading ─────────────────────────────────
 */
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
/* ─────────────────────────────────────────────────────────────────────────── */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

ESP8266WebServer server(80);

// ── Data structures ───────────────────────────────────────────────────────────
struct User {
  char  uid[20];
  char  name[30];
  float balance;
  bool  active;
};

struct MenuItem {
  int   id;
  char  name[30];
  float price;
  int   quantity;
  bool  active;
};

struct OrderItem {
  int   menuId;
  char  name[30];
  int   qty;
  float price;
};

struct Order {
  int       id;
  char      userUid[20];
  char      userName[30];
  float     total;
  OrderItem items[10];
  int       itemCount;
  int       status;   // 0=pending  1=ready  2=cancelled
  bool      active;
};

#define MAX_USERS  50
#define MAX_MENU   30
#define MAX_ORDERS 20

User     users[MAX_USERS];
int      userCount     = 0;
MenuItem menuItems[MAX_MENU];
int      menuItemCount = 0;
int      nextMenuId    = 1;
Order    orders[MAX_ORDERS];
int      orderCount    = 0;
int      nextOrderId   = 1;

// ── CORS helper ───────────────────────────────────────────────────────────────
void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin",  "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ── Data persistence (LittleFS /data.json) ────────────────────────────────────
void saveData() {
  File f = LittleFS.open("/data.json", "w");
  if (!f) return;
  DynamicJsonDocument doc(8192);
  JsonArray ua = doc.createNestedArray("users");
  for (int i = 0; i < userCount; i++) {
    JsonObject u = ua.createNestedObject();
    u["uid"]     = users[i].uid;
    u["name"]    = users[i].name;
    u["balance"] = users[i].balance;
    u["active"]  = users[i].active;
  }
  JsonArray ma = doc.createNestedArray("menu");
  for (int i = 0; i < menuItemCount; i++) {
    JsonObject m = ma.createNestedObject();
    m["id"]       = menuItems[i].id;
    m["name"]     = menuItems[i].name;
    m["price"]    = menuItems[i].price;
    m["quantity"] = menuItems[i].quantity;
    m["active"]   = menuItems[i].active;
  }
  doc["nextMenuId"]  = nextMenuId;
  doc["nextOrderId"] = nextOrderId;
  serializeJson(doc, f);
  f.close();
}

void loadData() {
  if (!LittleFS.exists("/data.json")) return;
  File f = LittleFS.open("/data.json", "r");
  if (!f) return;
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();

  userCount = 0;
  for (JsonObject u : doc["users"].as<JsonArray>()) {
    if (userCount >= MAX_USERS) break;
    memset(&users[userCount], 0, sizeof(users[0]));
    strncpy(users[userCount].uid,  u["uid"]  | "", sizeof(users[0].uid)  - 1);
    strncpy(users[userCount].name, u["name"] | "", sizeof(users[0].name) - 1);
    users[userCount].balance = u["balance"] | 0.0f;
    users[userCount].active  = u["active"]  | true;
    userCount++;
  }
  menuItemCount = 0;
  for (JsonObject m : doc["menu"].as<JsonArray>()) {
    if (menuItemCount >= MAX_MENU) break;
    memset(&menuItems[menuItemCount], 0, sizeof(menuItems[0]));
    menuItems[menuItemCount].id       = m["id"]       | 0;
    strncpy(menuItems[menuItemCount].name, m["name"] | "", sizeof(menuItems[0].name) - 1);
    menuItems[menuItemCount].price    = m["price"]    | 0.0f;
    menuItems[menuItemCount].quantity = m["quantity"] | 0;
    menuItems[menuItemCount].active   = m["active"]   | true;
    menuItemCount++;
  }
  nextMenuId  = doc["nextMenuId"]  | 1;
  nextOrderId = doc["nextOrderId"] | 1;
}

// ── Arduino serial message handler ───────────────────────────────────────────
void handleArduinoMsg(const String& msg) {

  /* ── VERIFY:<UID> ─────────────────────────────────────────────────── */
  if (msg.startsWith("VERIFY:")) {
    String uid = msg.substring(7);
    for (int i = 0; i < userCount; i++) {
      if (users[i].active && String(users[i].uid) == uid) {
        Serial.println("GRANTED:" + String(users[i].name) + ":" + String(users[i].balance, 2));
        return;
      }
    }
    Serial.println("DENIED");

  /* ── MENU:GET ─────────────────────────────────────────────────────── */
  } else if (msg == "MENU:GET") {
    String resp = "MENU:";
    bool first = true;
    for (int i = 0; i < menuItemCount; i++) {
      if (!menuItems[i].active || menuItems[i].quantity <= 0) continue;
      if (!first) resp += "|";
      resp += String(menuItems[i].id) + "," + menuItems[i].name
              + "," + String(menuItems[i].price, 2)
              + "," + String(menuItems[i].quantity);
      first = false;
    }
    Serial.println(resp);

  /* ── ORDER:uid,total;menuId,qty;... ──────────────────────────────── */
  } else if (msg.startsWith("ORDER:")) {
    String data     = msg.substring(6);
    int    commaPos = data.indexOf(',');
    int    semiPos  = data.indexOf(';');
    if (commaPos < 0 || semiPos < 0) { Serial.println("ORDER_FAIL"); return; }

    String uid   = data.substring(0, commaPos);
    float  total = data.substring(commaPos + 1, semiPos).toFloat();
    String items = data.substring(semiPos + 1);

    // Find user
    int uIdx = -1;
    for (int i = 0; i < userCount; i++) {
      if (users[i].active && String(users[i].uid) == uid) { uIdx = i; break; }
    }
    if (uIdx < 0 || users[uIdx].balance < total) { Serial.println("ORDER_FAIL"); return; }

    // Create order (circular buffer)
    int   oIdx = orderCount % MAX_ORDERS;
    Order& o   = orders[oIdx];
    memset(&o, 0, sizeof(o));
    o.id     = nextOrderId++;
    o.total  = total;
    o.status = 0;
    o.active = true;
    strncpy(o.userUid,  uid.c_str(),           sizeof(o.userUid)  - 1);
    strncpy(o.userName, users[uIdx].name,       sizeof(o.userName) - 1);

    // Parse items
    int pos = 0;
    while (pos < (int)items.length() && o.itemCount < 10) {
      int end = items.indexOf(';', pos);
      if (end < 0) end = items.length();
      String part = items.substring(pos, end);
      int    c    = part.indexOf(',');
      if (c > 0) {
        int mId = part.substring(0, c).toInt();
        int qty = part.substring(c + 1).toInt();
        for (int i = 0; i < menuItemCount; i++) {
          if (menuItems[i].active && menuItems[i].id == mId) {
            OrderItem& oi = o.items[o.itemCount++];
            oi.menuId = mId;
            oi.qty    = qty;
            oi.price  = menuItems[i].price;
            strncpy(oi.name, menuItems[i].name, sizeof(oi.name) - 1);
            menuItems[i].quantity = max(0, menuItems[i].quantity - qty);
            break;
          }
        }
      }
      pos = end + 1;
    }
    if (orderCount < MAX_ORDERS) orderCount++;

    users[uIdx].balance -= total;
    saveData();
    Serial.println("ORDER_OK:" + String(users[uIdx].balance, 2));
  }
}

// ── Web API handlers ──────────────────────────────────────────────────────────

/* GET /api/users */
void handleGetUsers() {
  addCorsHeaders();
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < userCount; i++) {
    if (!users[i].active) continue;
    JsonObject o = arr.createNestedObject();
    o["uid"]     = users[i].uid;
    o["name"]    = users[i].name;
    o["balance"] = users[i].balance;
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

/* POST /api/users  body: {"uid":"…","name":"…","balance":0} */
void handleAddUser() {
  addCorsHeaders();
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"No body\"}"); return; }
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }

  const char* uid  = doc["uid"]  | "";
  const char* name = doc["name"] | "";
  float bal        = doc["balance"] | 0.0f;
  if (!*uid || !*name) { server.send(400, "application/json", "{\"error\":\"uid and name required\"}"); return; }

  for (int i = 0; i < userCount; i++) {
    if (users[i].active && strcmp(users[i].uid, uid) == 0) {
      server.send(400, "application/json", "{\"error\":\"UID already exists\"}"); return;
    }
  }
  if (userCount >= MAX_USERS) { server.send(400, "application/json", "{\"error\":\"Max users reached\"}"); return; }

  User& u = users[userCount++];
  memset(&u, 0, sizeof(u));
  strncpy(u.uid,  uid,  sizeof(u.uid)  - 1);
  strncpy(u.name, name, sizeof(u.name) - 1);
  u.balance = bal; u.active = true;
  saveData();
  server.send(200, "application/json", "{\"success\":true}");
}

/* DELETE /api/users?uid=… */
void handleDeleteUser() {
  addCorsHeaders();
  if (!server.hasArg("uid")) { server.send(400, "application/json", "{\"error\":\"uid required\"}"); return; }
  String uid = server.arg("uid");
  for (int i = 0; i < userCount; i++) {
    if (users[i].active && String(users[i].uid) == uid) {
      users[i].active = false; saveData();
      server.send(200, "application/json", "{\"success\":true}"); return;
    }
  }
  server.send(404, "application/json", "{\"error\":\"User not found\"}");
}

/* GET /api/menu */
void handleGetMenu() {
  addCorsHeaders();
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < menuItemCount; i++) {
    if (!menuItems[i].active) continue;
    JsonObject o = arr.createNestedObject();
    o["id"]       = menuItems[i].id;
    o["name"]     = menuItems[i].name;
    o["price"]    = menuItems[i].price;
    o["quantity"] = menuItems[i].quantity;
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

/* POST /api/menu  body: {"name":"…","price":0,"quantity":0} */
void handleAddMenuItem() {
  addCorsHeaders();
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"No body\"}"); return; }
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }

  const char* name = doc["name"] | "";
  float price      = doc["price"]    | 0.0f;
  int   quantity   = doc["quantity"] | 0;
  if (!*name) { server.send(400, "application/json", "{\"error\":\"name required\"}"); return; }
  if (menuItemCount >= MAX_MENU) { server.send(400, "application/json", "{\"error\":\"Max items reached\"}"); return; }

  MenuItem& m = menuItems[menuItemCount++];
  memset(&m, 0, sizeof(m));
  m.id = nextMenuId++;
  strncpy(m.name, name, sizeof(m.name) - 1);
  m.price = price; m.quantity = quantity; m.active = true;
  saveData();
  DynamicJsonDocument resp(64); resp["success"] = true; resp["id"] = m.id;
  String out; serializeJson(resp, out);
  server.send(200, "application/json", out);
}

/* DELETE /api/menu?id=… */
void handleDeleteMenuItem() {
  addCorsHeaders();
  if (!server.hasArg("id")) { server.send(400, "application/json", "{\"error\":\"id required\"}"); return; }
  int id = server.arg("id").toInt();
  for (int i = 0; i < menuItemCount; i++) {
    if (menuItems[i].active && menuItems[i].id == id) {
      menuItems[i].active = false; saveData();
      server.send(200, "application/json", "{\"success\":true}"); return;
    }
  }
  server.send(404, "application/json", "{\"error\":\"Item not found\"}");
}

/* GET /api/orders  — returns pending orders only */
void handleGetOrders() {
  addCorsHeaders();
  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = orderCount - 1; i >= 0; i--) {
    if (!orders[i].active || orders[i].status != 0) continue;
    JsonObject o = arr.createNestedObject();
    o["id"]       = orders[i].id;
    o["userName"] = orders[i].userName;
    o["userUid"]  = orders[i].userUid;
    o["total"]    = orders[i].total;
    JsonArray its = o.createNestedArray("items");
    for (int j = 0; j < orders[i].itemCount; j++) {
      JsonObject it = its.createNestedObject();
      it["name"]  = orders[i].items[j].name;
      it["qty"]   = orders[i].items[j].qty;
      it["price"] = orders[i].items[j].price;
    }
  }
  String out; serializeJson(doc, out);
  server.send(200, "application/json", out);
}

/* POST /api/orders/ready  body: {"id":N} */
void handleOrderReady() {
  addCorsHeaders();
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"No body\"}"); return; }
  DynamicJsonDocument doc(128);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
  int id = doc["id"] | -1;
  for (int i = 0; i < orderCount; i++) {
    if (orders[i].active && orders[i].id == id) {
      orders[i].status = 1;
      Serial.println(String("READY:") + orders[i].userName);
      server.send(200, "application/json", "{\"success\":true}"); return;
    }
  }
  server.send(404, "application/json", "{\"error\":\"Order not found\"}");
}

/* POST /api/orders/cancel  body: {"id":N} */
void handleOrderCancel() {
  addCorsHeaders();
  if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"No body\"}"); return; }
  DynamicJsonDocument doc(128);
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "application/json", "{\"error\":\"Bad JSON\"}"); return; }
  int id = doc["id"] | -1;
  for (int i = 0; i < orderCount; i++) {
    if (!orders[i].active || orders[i].id != id) continue;
    orders[i].status = 2;
    // Refund balance
    for (int j = 0; j < userCount; j++) {
      if (users[j].active && strcmp(users[j].uid, orders[i].userUid) == 0) {
        users[j].balance += orders[i].total; break;
      }
    }
    // Restore stock
    for (int k = 0; k < orders[i].itemCount; k++) {
      for (int m = 0; m < menuItemCount; m++) {
        if (menuItems[m].active && menuItems[m].id == orders[i].items[k].menuId) {
          menuItems[m].quantity += orders[i].items[k].qty; break;
        }
      }
    }
    saveData();
    server.send(200, "application/json", "{\"success\":true}"); return;
  }
  server.send(404, "application/json", "{\"error\":\"Order not found\"}");
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  // NOTE: This Serial port is reserved for Arduino ↔ ESP01 communication.
  // Do NOT add Serial.print() debug calls — they will corrupt the Arduino protocol.
  Serial.begin(9600);

  LittleFS.begin();
  loadData();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  // Wait up to 20 s for connection; the web server still starts so the
  // Arduino side works even if WiFi is unavailable (orders are served locally).
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(500);

  // Serve index.html at root
  server.on("/", HTTP_GET, []() {
    File f = LittleFS.open("/index.html", "r");
    if (!f) { server.send(503, "text/plain", "Upload data files via LittleFS tool."); return; }
    server.streamFile(f, "text/html"); f.close();
  });

  // REST API
  server.on("/api/users",          HTTP_GET,    handleGetUsers);
  server.on("/api/users",          HTTP_POST,   handleAddUser);
  server.on("/api/users",          HTTP_DELETE, handleDeleteUser);
  server.on("/api/menu",           HTTP_GET,    handleGetMenu);
  server.on("/api/menu",           HTTP_POST,   handleAddMenuItem);
  server.on("/api/menu",           HTTP_DELETE, handleDeleteMenuItem);
  server.on("/api/orders",         HTTP_GET,    handleGetOrders);
  server.on("/api/orders/ready",   HTTP_POST,   handleOrderReady);
  server.on("/api/orders/cancel",  HTTP_POST,   handleOrderCancel);

  // Status endpoint — useful for the web UI to show connectivity info
  server.on("/api/status", HTTP_GET, []() {
    addCorsHeaders();
    DynamicJsonDocument doc(256);
    doc["wifi"]  = (WiFi.status() == WL_CONNECTED);
    doc["ip"]    = WiFi.localIP().toString();
    doc["rssi"]  = WiFi.RSSI();
    String out; serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // CORS preflight
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) { addCorsHeaders(); server.send(204); }
    else server.send(404, "text/plain", "Not found");
  });

  server.begin();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    if (msg.length()) handleArduinoMsg(msg);
  }
}
