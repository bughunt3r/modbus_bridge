/*
 * modbus_bridge_esp32.ino
 * Bridge HTTP -> Modbus TCP pour piloter le LOGO! 8.4 Siemens
 * Plateforme : Olimex ESP32-POE-ISO
 *
 * Routes :
 *   GET /notify?memento=<n>&action=<ON|OFF>
 *     Commande Modbus (protégée par IP whitelist + Basic Auth caméra)
 *
 *   GET /set?[params]   → protégée par Basic Auth admin (ADMIN_USER/ADMIN_PASSWORD)
 *     Paramètres :
 *       user=val            mot de passe caméra (user)
 *       password=val        mot de passe caméra (password)
 *       checkAuth=0|1       activer/désactiver la Basic Auth caméra
 *       checkIP=0|1         activer/désactiver le contrôle des IP sources
 *       clearIP=1           vider la liste des IP autorisées
 *       ip1=x.x.x.x        ajouter une IP autorisée (ip1..ip10)
 *       modbus-ip=x.x.x.x       IP de l'automate (effet immédiat + NVS)
 *       modbus-port=N            port Modbus TCP de l'automate (effet immédiat + NVS)
 *       modbus-coil-base=N       adresse de base coil, décimal ou 0xHEX (effet immédiat + NVS)
 *       modbus-unit-id=N         Unit ID Modbus [0-255] (effet immédiat + NVS)
 *       ip=x.x.x.x               IP locale du bridge (NVS + reboot auto)
 *       subnet=x.x.x.x     masque réseau (NVS + reboot auto)
 *       gateway=x.x.x.x    passerelle (NVS + reboot auto)
 *       dhcp=0|1            activer/désactiver DHCP (NVS + reboot auto)
 *       port=N              port HTTP du bridge (NVS + reboot auto)
 *     Exemples :
 *       curl -u admin:adminpass \
 *         "http://192.168.129.198:8080/set?checkAuth=1&checkIP=1&clearIP=1&ip1=192.168.129.36"
 *       curl -u admin:adminpass \
 *         "http://192.168.129.198:8080/set?ip=192.168.129.200&gateway=192.168.128.1"
 *     Paramètres immédiats : user, password, checkAuth, checkIP, ip1..ip10, modbus-ip, modbus-port,
 *                            modbus-coil-base, modbus-unit-id
 *     Paramètres avec reboot auto : ip, subnet, gateway, dhcp, port
 *
 *   BUT1 (GPIO34) maintenu au boot → reset NVS aux défauts
 *
 *   Exemples /notify :
 *     GET /notify?memento=1&action=ON   -> M1 = 1
 *     GET /notify?memento=1&action=OFF  -> M1 = 0
 *     GET /notify?memento=2&action=ON   -> M2 = 1
 *     GET /notify?memento=2&action=OFF  -> M2 = 0
 *
 * Mobotix IP Notify :
 *   Data protocol  : HTTP/1.0 Request
 *   Destination    : <ESP32_IP>:8080
 *   CGI Path       : /notify?memento=1&action=ON
 *
 *
 * Compilation :
 *   Arduino IDE >= 2.x
 *   + Board package : "esp32 by Espressif" >= 3.0.0  (NetworkServer/NetworkClient)
 *   Board cible     : "OLIMEX ESP32-POE" (ou ESP32-PICO-D4 / ESP32 Dev Module
 *                      si la board Olimex n'est pas dans la liste)
 *
 * PlatformIO (alternative) :
 *   [env:esp32-poe-iso]
 *   platform  = espressif32
 *   board     = esp32-poe-iso    ; ou esp32-poe
 *   framework = arduino
 */

#include <ETH.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <Preferences.h>

// -----------------------------------------------------------------------
// Pinout ETH Olimex ESP32-POE-ISO (LAN8720A)
// -----------------------------------------------------------------------
#define ETH_PHY_TYPE   ETH_PHY_LAN8720
#define ETH_PHY_ADDR   0
#define ETH_PHY_MDC    23
#define ETH_PHY_MDIO   18
#define ETH_PHY_POWER  12          // enable PoE power supply
#define ETH_CLK_MODE   ETH_CLOCK_GPIO17_OUT
// Note : si votre carte est une révision POE (sans ISO), essayez
//        ETH_CLOCK_GPIO0_IN à la place de ETH_CLOCK_GPIO17_OUT.

// -----------------------------------------------------------------------
// BUT1 = GPIO 34 (actif bas) : maintenu au boot → reset NVS aux défauts
// -----------------------------------------------------------------------
#define BUT1_PIN  34

// -----------------------------------------------------------------------
// Valeurs réseau par défaut (compilées) — surchargées par NVS si présentes
// -----------------------------------------------------------------------
#define DEFAULT_LOCAL_IP    "192.168.129.198"
#define DEFAULT_GATEWAY     "192.168.128.1"
#define DEFAULT_SUBNET      "255.255.254.0"
#define DEFAULT_MODBUS_IP        "192.168.129.199"
#define DEFAULT_MODBUS_PORT      510     // LOGO! 8.4 uniquement ; port standard Modbus TCP = 502 (tous les autres)
#define DEFAULT_LISTEN_PORT      8080

// -----------------------------------------------------------------------
// Table de référence Modbus TCP — ports, adresses coil et Unit ID par automate
// Modifiable à chaud via /set?modbus-coil-base=N et /set?modbus-unit-id=N
//
// Fabricant        Modèle                     Port   CoilBase UnitID  Remarques
// -----------      -----------------------    -----  -------- ------  ---------
// Siemens          LOGO! 8.4 (0BA8)           510    0x2040   0x01    Mementos M1..M64 — port non standard !
// Siemens          S7-1200 / S7-1500          502    0x0000   0x01    Adresses libres via bloc MB_SERVER
// Schneider        M340, M580                 502    0x0000   0xFF    Sorties %Q — conf. Unity Pro / EcoStruxure
// Schneider        Modicon M221, M241/M251    502    0x0000   0xFF    Sorties %Q — EcoStruxure Machine Expert
// Schneider        Zelio Logic SR2/SR3        502    0x0000   0xFF    Sorties Q1..
// Wago             750-352/362, PFC200        502    0x0000   0x01    Process image sorties physiques
// Beckhoff         CX/BK + Modbus TCP         502    0x0000   0x01    Dépend du mappage EtherCAT → Modbus
// Delta            DVP-EH2/SX2/SS2            502    0x0000   0x01    Bobines sorties Y0..
// Omron            CP1E/CP1L/NX/NJ            502    0x0000   0x01    CIO bits — vérifier mappage area
// Mitsubishi       iQ-F (FX5U), Q series      502    0x0000   0x01    Bobines Y (sorties) ou relais M
// Phoenix Contact  AXC F 1152 / 2152          502    0x0000   0x01    Sorties configurables dans PLCnext
// Moxa             ioLogik E1210/E1212         502    0x0000   0x01    Digital outputs DO0..
//
// Note : 0x0000 est l'adresse standard pour la quasi-totalité des automates modernes.
//        0x2040 est spécifique au LOGO! 8.4 Siemens (port 510 non standard).
//        UnitID 0xFF (255) = identifiant générique accepté par les automates Schneider/Modicon.
//        Certains automates permettent de remapper leurs adresses Modbus dans leur outil
//        de configuration — toujours vérifier la documentation du projet automate.
// -----------------------------------------------------------------------
#define DEFAULT_MODBUS_COIL_BASE  0x2040  // LOGO! 8.4 ; mettre 0x0000 pour la plupart des autres
#define DEFAULT_MODBUS_UNIT_ID    0x01    // LOGO! 8.4 et majorité des automates ; 0xFF pour Schneider/Modicon

// -----------------------------------------------------------------------
// Valeurs par défaut compilées — utilisées si NVS vide ou reset BUT1
// -----------------------------------------------------------------------
static const char*  DEFAULT_USER      = "user";
static const char*  DEFAULT_PASSWORD  = "pass";

// -----------------------------------------------------------------------
// Mot de passe admin (fixe, non modifiable via /set) — protège la route /set
// Utiliser : curl -u admin:1234 "http://.../set?..."
// -----------------------------------------------------------------------
static const char*  ADMIN_USER        = "admin";
static const char*  ADMIN_PASSWORD    = "1234";

// -----------------------------------------------------------------------
// Configuration runtime — chargée depuis NVS au boot, modifiable via /set
// -----------------------------------------------------------------------
static bool      cfg_secCheck  = false;   // contrôle IP sources
static bool      cfg_secAuth   = false;   // Basic Auth caméra
static String    cfg_user      = DEFAULT_USER;
static String    cfg_password  = DEFAULT_PASSWORD;
static int       cfg_ipCount   = 0;
static IPAddress cfg_ips[10];

static IPAddress cfg_localIP;
static IPAddress cfg_gateway;
static IPAddress cfg_subnet;
static bool      cfg_dhcp       = false;
static uint16_t  cfg_listenPort = DEFAULT_LISTEN_PORT;
static IPAddress cfg_modbusIP;
static uint16_t  cfg_modbusPort     = DEFAULT_MODBUS_PORT;
static uint16_t  cfg_modbusCoilBase = DEFAULT_MODBUS_COIL_BASE;
static uint8_t   cfg_modbusUnitId   = DEFAULT_MODBUS_UNIT_ID;

// -----------------------------------------------------------------------

struct ModbusCmd { int memento; bool on; };

static bool          eth_connected = false;
static NetworkServer* server = nullptr;
static QueueHandle_t modbusQueue = nullptr;
static const int     QUEUE_SIZE  = 20;

// -----------------------------------------------------------------------
// Callback événements Ethernet
// -----------------------------------------------------------------------
void onEthEvent(arduino_event_id_t event)
{
    switch (event) {
    case ARDUINO_EVENT_ETH_START:
        Serial.println("[ETH] Démarrage");
        ETH.setHostname("modbus-bridge");
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        Serial.println("[ETH] Lien physique OK");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        Serial.printf("[ETH] IP : %s  vitesse : %d Mb/s  duplex : %s\n",
                      ETH.localIP().toString().c_str(),
                      ETH.linkSpeed(),
                      ETH.fullDuplex() ? "full" : "half");
        eth_connected = true;
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        Serial.println("[ETH] Déconnecté");
        eth_connected = false;
        break;
    case ARDUINO_EVENT_ETH_STOP:
        Serial.println("[ETH] Arrêté");
        eth_connected = false;
        break;
    default:
        break;
    }
}

// -----------------------------------------------------------------------
// Construction de la trame Modbus TCP FC05 (Write Single Coil)
// -----------------------------------------------------------------------
static void buildModbusFrame(uint8_t buf[12], int memento, bool on)
{
    uint16_t addr = cfg_modbusCoilBase + (uint16_t)(memento - 1);
    uint16_t val  = on ? 0xFF00u : 0x0000u;

    buf[0]  = 0x00; buf[1]  = 0x01;           // Transaction ID
    buf[2]  = 0x00; buf[3]  = 0x00;           // Protocol ID
    buf[4]  = 0x00; buf[5]  = 0x06;           // Length
    buf[6]  = cfg_modbusUnitId;               // Unit ID
    buf[7]  = 0x05;                            // FC05
    buf[8]  = (addr >> 8) & 0xFF;
    buf[9]  =  addr       & 0xFF;
    buf[10] = (val  >> 8) & 0xFF;
    buf[11] =  val        & 0xFF;
}

// -----------------------------------------------------------------------
// Envoi Modbus et lecture de la réponse
// Retourne true si succès (echo de la trame attendu du LOGO!)
// -----------------------------------------------------------------------
static bool sendModbus(int memento, bool on)
{
    uint8_t frame[12];
    buildModbusFrame(frame, memento, on);

    Serial.print("  -> Modbus : ");
    for (int i = 0; i < 12; i++) { Serial.printf("%02X ", frame[i]); }
    Serial.println();

    NetworkClient mc;
    mc.setTimeout(1000);
    if (!mc.connect(cfg_modbusIP, cfg_modbusPort)) {
        Serial.println("  [ERREUR] Connexion Modbus échouée");
        return false;
    }

    mc.write(frame, 12);

    uint8_t resp[16];
    int n = mc.readBytes(resp, sizeof(resp));
    mc.stop();

    if (n < 6) {
        Serial.printf("  [ERREUR] Réponse trop courte : %d octet(s)\n", n);
        return false;
    }

    Serial.print("  <- LOGO   : ");
    for (int i = 0; i < n; i++) { Serial.printf("%02X ", resp[i]); }
    Serial.println();

    // Vérification : exception Modbus si byte[7] >= 0x80
    if ((n >= 8) && (resp[7] & 0x80)) {
        Serial.printf("  [ERREUR] Exception Modbus FC=%02X code=%02X\n",
                      resp[7], (n >= 9) ? resp[8] : 0);
        return false;
    }
    return true;
}

// -----------------------------------------------------------------------
// Tâche FreeRTOS : vide la file et envoie les commandes Modbus
// -----------------------------------------------------------------------
static void modbusTask(void*)
{
    ModbusCmd cmd;
    for (;;) {
        if (xQueueReceive(modbusQueue, &cmd, portMAX_DELAY) == pdTRUE) {
            bool ok = sendModbus(cmd.memento, cmd.on);
            Serial.printf("  M%d %s -> %s\n", cmd.memento,
                          cmd.on ? "ON" : "OFF", ok ? "OK" : "ECHEC");
        }
    }
}

// -----------------------------------------------------------------------
// Parsing de la première ligne HTTP
// "GET /notify?memento=1&action=ON HTTP/1.1"
// Retourne true et remplit memento/on si OK, false sinon.
// -----------------------------------------------------------------------
static bool parseRequest(const String& line, int& memento, bool& on)
{
    if (!line.startsWith("GET ")) return false;

    int qs = line.indexOf('?');
    if (qs < 0) return false;

    // Isoler la query string (avant l'espace final "HTTP/…")
    String query = line.substring(qs + 1);
    int sp = query.indexOf(' ');
    if (sp >= 0) query = query.substring(0, sp);

    // Extraire memento=N
    int mi = query.indexOf("memento=");
    if (mi < 0) return false;
    String mStr = query.substring(mi + 8);
    int sep = mStr.indexOf('&');
    if (sep >= 0) mStr = mStr.substring(0, sep);
    memento = mStr.toInt();
    if (memento < 1 || memento > 64) return false;

    // Extraire action=ON|OFF
    int ai = query.indexOf("action=");
    if (ai < 0) return false;
    String aStr = query.substring(ai + 7);
    sep = aStr.indexOf('&');
    if (sep >= 0) aStr = aStr.substring(0, sep);
    aStr.toUpperCase();
    if      (aStr == "ON")  on = true;
    else if (aStr == "OFF") on = false;
    else return false;

    return true;
}

// -----------------------------------------------------------------------
// Helpers NVS + sécurité
// -----------------------------------------------------------------------
static String base64Encode(const String& input)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String output = "";
    int len = input.length();
    for (int i = 0; i < len; i += 3) {
        uint32_t b = (uint8_t)input[i] << 16;
        if (i+1 < len) b |= (uint8_t)input[i+1] << 8;
        if (i+2 < len) b |= (uint8_t)input[i+2];
        output += b64[(b >> 18) & 0x3F];
        output += b64[(b >> 12) & 0x3F];
        output += (i+1 < len) ? b64[(b >>  6) & 0x3F] : '=';
        output += (i+2 < len) ? b64[(b      ) & 0x3F] : '=';
    }
    return output;
}

// Recherche key=val dans une query string (vérifie la limite de mot : évite
// que "ip" ne matche "modbus-ip" ou "port" ne matche "modbus-port")
static String getParam(const String& query, const String& key)
{
    String search = key + "=";
    int idx = 0;
    while (true) {
        idx = query.indexOf(search, idx);
        if (idx < 0) return "";
        if (idx == 0 || query.charAt(idx - 1) == '&') break;
        idx += search.length();
    }
    String val = query.substring(idx + search.length());
    int sep = val.indexOf('&');
    if (sep >= 0) val = val.substring(0, sep);
    return val;
}

static void loadConfig()
{
    Preferences prefs;
    prefs.begin("bridge", true);
    cfg_secCheck = prefs.getBool("secCheck", false);
    cfg_secAuth  = prefs.getBool("secAuth",  false);
    cfg_user     = prefs.getString("user",     DEFAULT_USER);
    cfg_password = prefs.getString("password", DEFAULT_PASSWORD);
    cfg_ipCount  = prefs.getInt("ipCount", 0);
    for (int i = 0; i < cfg_ipCount && i < 10; i++) {
        String s = prefs.getString(("ip" + String(i)).c_str(), "");
        if (s.length() > 0) cfg_ips[i].fromString(s);
    }
    // Réseau
    String s;
    s = prefs.getString("localIP",  DEFAULT_LOCAL_IP);  cfg_localIP.fromString(s);
    s = prefs.getString("gateway",  DEFAULT_GATEWAY);   cfg_gateway.fromString(s);
    s = prefs.getString("subnet",   DEFAULT_SUBNET);    cfg_subnet.fromString(s);
    s = prefs.getString("modbusIP", DEFAULT_MODBUS_IP); cfg_modbusIP.fromString(s);
    cfg_dhcp       = prefs.getBool  ("dhcp",       false);
    cfg_listenPort = prefs.getUShort("listenPort", DEFAULT_LISTEN_PORT);
    cfg_modbusPort     = prefs.getUShort("modbusPort", DEFAULT_MODBUS_PORT);
    cfg_modbusCoilBase = prefs.getUShort("coilBase",   DEFAULT_MODBUS_COIL_BASE);
    cfg_modbusUnitId   = prefs.getUChar ("unitId",     DEFAULT_MODBUS_UNIT_ID);
    prefs.end();
    Serial.printf("[NVS] chargé : secCheck=%d secAuth=%d user=%s ipCount=%d\n",
                  cfg_secCheck, cfg_secAuth, cfg_user.c_str(), cfg_ipCount);
    Serial.printf("[NVS] réseau : ip=%s gw=%s mask=%s dhcp=%d port=%u\n",
                  cfg_localIP.toString().c_str(), cfg_gateway.toString().c_str(),
                  cfg_subnet.toString().c_str(), cfg_dhcp, cfg_listenPort);
    Serial.printf("[NVS] modbus : ip=%s port=%u coilBase=0x%04X unitId=0x%02X\n",
                  cfg_modbusIP.toString().c_str(), cfg_modbusPort,
                  cfg_modbusCoilBase, cfg_modbusUnitId);
}

static void resetToDefaults()
{
    Preferences prefs;
    prefs.begin("bridge", false);
    prefs.clear();
    prefs.end();
    cfg_secCheck = false;
    cfg_secAuth  = false;
    cfg_user     = DEFAULT_USER;
    cfg_password = DEFAULT_PASSWORD;
    cfg_ipCount  = 0;
    cfg_localIP.fromString(DEFAULT_LOCAL_IP);
    cfg_gateway.fromString(DEFAULT_GATEWAY);
    cfg_subnet.fromString(DEFAULT_SUBNET);
    cfg_modbusIP.fromString(DEFAULT_MODBUS_IP);
    cfg_dhcp           = false;
    cfg_listenPort     = DEFAULT_LISTEN_PORT;
    cfg_modbusPort     = DEFAULT_MODBUS_PORT;
    cfg_modbusCoilBase = DEFAULT_MODBUS_COIL_BASE;
    cfg_modbusUnitId   = DEFAULT_MODBUS_UNIT_ID;
    Serial.println("[NVS] Reset aux valeurs par défaut (BUT1)");
    Serial.println("[NVS] secCheck=false secAuth=false — accès libre au redémarrage");
}

// -----------------------------------------------------------------------
// Traitement de la route /set  (protégée par Basic Auth admin)
// -----------------------------------------------------------------------
static void handleSet(NetworkClient& client, const String& query, const String& authHeader)
{
    // Vérification admin
    String expected = base64Encode(String(ADMIN_USER) + ":" + String(ADMIN_PASSWORD));
    if (authHeader != expected) {
        Serial.printf("[SET] Auth admin échouée depuis %s\n", client.remoteIP().toString().c_str());
        client.print("HTTP/1.0 401 Unauthorized\r\n"
                     "WWW-Authenticate: Basic realm=\"modbus-bridge-admin\"\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: 13\r\n"
                     "Connection: close\r\n\r\n"
                     "Unauthorized\n");
        return;
    }

    Preferences prefs;
    prefs.begin("bridge", false);
    String reply = "Config mise a jour:\n";
    String v;
    bool needsReboot = false;

    v = getParam(query, "user");
    if (v.length() > 0) { cfg_user = v; prefs.putString("user", v); reply += "user=" + v + "\n"; }

    v = getParam(query, "password");
    if (v.length() > 0) { cfg_password = v; prefs.putString("password", v); reply += "password=***\n"; }

    v = getParam(query, "checkAuth");
    if (v.length() > 0) { cfg_secAuth = (v == "1"); prefs.putBool("secAuth", cfg_secAuth); reply += "checkAuth=" + v + "\n"; }

    v = getParam(query, "checkIP");
    if (v.length() > 0) { cfg_secCheck = (v == "1"); prefs.putBool("secCheck", cfg_secCheck); reply += "checkIP=" + v + "\n"; }

    // clearIP doit être traité avant ip1..ip10
    v = getParam(query, "clearIP");
    if (v == "1") {
        for (int i = 0; i < 10; i++) prefs.remove(("ip" + String(i)).c_str());
        cfg_ipCount = 0;
        reply += "clearIP=done\n";
    }

    for (int i = 1; i <= 10; i++) {
        v = getParam(query, "ip" + String(i));
        if (v.length() > 0 && cfg_ipCount < 10) {
            IPAddress ip;
            if (ip.fromString(v)) {
                prefs.putString(("ip" + String(cfg_ipCount)).c_str(), v);
                cfg_ips[cfg_ipCount++] = ip;
                reply += "ip" + String(cfg_ipCount) + "=" + v + "\n";
            }
        }
    }
    prefs.putInt("ipCount", cfg_ipCount);

    // Modbus IP/port : effet immédiat + NVS (pas de reboot)
    v = getParam(query, "modbus-ip");
    if (v.length() > 0) {
        IPAddress ip;
        if (ip.fromString(v)) {
            cfg_modbusIP = ip;
            prefs.putString("modbusIP", v);
            reply += "modbus-ip=" + v + "\n";
        }
    }
    v = getParam(query, "modbus-port");
    if (v.length() > 0) {
        int p = v.toInt();
        if (p > 0 && p <= 65535) {
            cfg_modbusPort = (uint16_t)p;
            prefs.putUShort("modbusPort", cfg_modbusPort);
            reply += "modbus-port=" + v + "\n";
        }
    }
    v = getParam(query, "modbus-coil-base");
    if (v.length() > 0) {
        uint32_t base = (v.startsWith("0x") || v.startsWith("0X"))
                        ? (uint32_t)strtoul(v.c_str(), nullptr, 16)
                        : (uint32_t)v.toInt();
        if (base <= 0xFFFF) {
            cfg_modbusCoilBase = (uint16_t)base;
            prefs.putUShort("coilBase", cfg_modbusCoilBase);
            char hex[8]; snprintf(hex, sizeof(hex), "0x%04X", cfg_modbusCoilBase);
            reply += String("modbus-coil-base=") + hex + "\n";
        }
    }
    v = getParam(query, "modbus-unit-id");
    if (v.length() > 0) {
        int uid = v.toInt();
        if (uid >= 0 && uid <= 255) {
            cfg_modbusUnitId = (uint8_t)uid;
            prefs.putUChar("unitId", cfg_modbusUnitId);
            reply += "modbus-unit-id=" + v + "\n";
        }
    }

    // Réseau : NVS + reboot automatique
    v = getParam(query, "ip");
    if (v.length() > 0) {
        IPAddress ip;
        if (ip.fromString(v)) {
            prefs.putString("localIP", v);
            reply += "ip=" + v + " (reboot)\n";
            needsReboot = true;
        }
    }
    v = getParam(query, "subnet");
    if (v.length() > 0) {
        IPAddress ip;
        if (ip.fromString(v)) {
            prefs.putString("subnet", v);
            reply += "subnet=" + v + " (reboot)\n";
            needsReboot = true;
        }
    }
    v = getParam(query, "gateway");
    if (v.length() > 0) {
        IPAddress ip;
        if (ip.fromString(v)) {
            prefs.putString("gateway", v);
            reply += "gateway=" + v + " (reboot)\n";
            needsReboot = true;
        }
    }
    v = getParam(query, "dhcp");
    if (v.length() > 0) {
        prefs.putBool("dhcp", v == "1");
        reply += "dhcp=" + v + " (reboot)\n";
        needsReboot = true;
    }
    v = getParam(query, "port");
    if (v.length() > 0) {
        int p = v.toInt();
        if (p > 0 && p <= 65535) {
            prefs.putUShort("listenPort", (uint16_t)p);
            reply += "port=" + v + " (reboot)\n";
            needsReboot = true;
        }
    }

    prefs.end();

    if (needsReboot) reply += "--- redemarrage dans 500ms ---\n";
    Serial.printf("[SET] Modifié par %s : %s", client.remoteIP().toString().c_str(), reply.c_str());
    client.printf("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                  reply.length());
    client.print(reply);
    if (needsReboot) {
        client.stop();
        delay(500);
        ESP.restart();
    }
}

// -----------------------------------------------------------------------
// Traitement d'un client HTTP
// -----------------------------------------------------------------------
static void handleClient(NetworkClient& client)
{
    String firstLine;
    String authHeader;
    bool   gotFirst = false;
    unsigned long deadline = millis() + 2000UL;

    // Lecture des headers
    while (client.connected() && millis() < deadline) {
        if (!client.available()) { delay(1); continue; }
        String line = client.readStringUntil('\n');
        line.trim();
        if (!gotFirst && line.length() > 0) {
            firstLine = line;
            gotFirst  = true;
        } else if (line.startsWith("Authorization: Basic ")) {
            authHeader = line.substring(21);
        }
        if (line.length() == 0) break;
    }

    if (!gotFirst) {
        client.print("HTTP/1.0 400 Bad Request\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: 12\r\n"
                     "Connection: close\r\n\r\n"
                     "Bad Request\n");
        return;
    }

    Serial.printf("[HTTP] %s -> %s\n",
                  client.remoteIP().toString().c_str(),
                  firstLine.c_str());

    // Routing : /set (admin) ou /notify (caméra)
    if (firstLine.startsWith("GET /set")) {
        String query = "";
        int qs = firstLine.indexOf('?');
        if (qs >= 0) {
            query = firstLine.substring(qs + 1);
            int sp = query.indexOf(' ');
            if (sp >= 0) query = query.substring(0, sp);
        }
        handleSet(client, query, authHeader);
        return;
    }

    // Route /notify : vérification IP source
    IPAddress remote = client.remoteIP();
    if (cfg_secCheck && cfg_ipCount > 0) {
        bool allowed = false;
        for (int i = 0; i < cfg_ipCount; i++) {
            if (remote == cfg_ips[i]) { allowed = true; break; }
        }
        if (!allowed) {
            Serial.printf("[WARN] IP refusée : %s\n", remote.toString().c_str());
            client.print("HTTP/1.0 403 Forbidden\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: 10\r\n"
                         "Connection: close\r\n\r\n"
                         "Forbidden\n");
            return;
        }
    }

    // Vérification Basic Auth caméra
    if (cfg_secAuth) {
        String expected = base64Encode(cfg_user + ":" + cfg_password);
        if (authHeader != expected) {
            Serial.printf("[WARN] Auth caméra échouée : %s\n", remote.toString().c_str());
            client.print("HTTP/1.0 401 Unauthorized\r\n"
                         "WWW-Authenticate: Basic realm=\"modbus-bridge\"\r\n"
                         "Content-Type: text/plain\r\n"
                         "Content-Length: 13\r\n"
                         "Connection: close\r\n\r\n"
                         "Unauthorized\n");
            return;
        }
    }

    int  memento;
    bool on;
    if (!parseRequest(firstLine, memento, on)) {
        client.print("HTTP/1.0 400 Bad Request\r\n"
                     "Content-Type: text/plain\r\n"
                     "Content-Length: 21\r\n"
                     "Connection: close\r\n\r\n"
                     "Parametres invalides\n");
        return;
    }

    // Répondre immédiatement à la caméra, enqueue la commande Modbus
    client.print("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 3\r\nConnection: close\r\n\r\nOK\n");
    client.clear();
    client.stop();

    ModbusCmd cmd = { memento, on };
    if (xQueueSend(modbusQueue, &cmd, 0) != pdTRUE) {
        Serial.printf("  [WARN] File pleine, M%d %s ignoré\n", memento, on ? "ON" : "OFF");
    } else {
        Serial.printf("  [QUEUE] M%d %s en attente (%d/%d)\n", memento, on ? "ON" : "OFF",
                      (int)uxQueueMessagesWaiting(modbusQueue), QUEUE_SIZE);
    }
}

// -----------------------------------------------------------------------
// setup / loop
// -----------------------------------------------------------------------
void setup()
{
    Serial.begin(115200);
    Serial.println("\n== modbus-bridge ESP32-POE-ISO ==");

    // BUT1 (GPIO34) maintenu au boot → reset NVS aux valeurs par défaut
    pinMode(BUT1_PIN, INPUT);
    if (digitalRead(BUT1_PIN) == LOW) {
        resetToDefaults();
    } else {
        loadConfig();
    }

    modbusQueue = xQueueCreate(QUEUE_SIZE, sizeof(ModbusCmd));
    xTaskCreate(modbusTask, "modbus", 4096, nullptr, 1, nullptr);

    Network.onEvent(onEthEvent);
    ETH.begin(ETH_PHY_TYPE,
              ETH_PHY_ADDR,
              ETH_PHY_MDC,
              ETH_PHY_MDIO,
              ETH_PHY_POWER,
              ETH_CLK_MODE);
    if (!cfg_dhcp) {
        ETH.config(cfg_localIP, cfg_gateway, cfg_subnet);
    }

    Serial.print("Attente du lien Ethernet");
    while (!eth_connected) {
        Serial.print('.');
        delay(500);
    }
    Serial.println();

    server = new NetworkServer(cfg_listenPort);
    server->begin();
    Serial.printf("Bridge HTTP actif sur %s:%u\n",
                  ETH.localIP().toString().c_str(),
                  cfg_listenPort);
    Serial.printf("LOGO! cible : %s:%u\n",
                  cfg_modbusIP.toString().c_str(),
                  cfg_modbusPort);
}

void loop()
{
    NetworkClient client = server->accept();
    if (client) {
        handleClient(client);
    } else {
        delay(1);  // yield au scheduler FreeRTOS
    }
}
