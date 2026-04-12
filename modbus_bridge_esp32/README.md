# Mobotix → PLC Modbus TCP Bridge

Bridge HTTP→Modbus TCP sur Olimex ESP32-POE-ISO, permettant à des caméras Mobotix de piloter les sorties ou bits internes d'un automate via la fonction IP Notify.

Conçu initialement pour le Siemens LOGO! 8.4, le bridge est compatible avec tout automate supportant Modbus TCP FC05 (Schneider M340/M580, Wago, Beckhoff, Omron, Mitsubishi, Delta…) — les paramètres Modbus sont configurables à chaud sans recompilation.

---

## Contexte et problème résolu

Les caméras Mobotix disposent d'une fonction IP Notify capable d'envoyer une requête HTTP GET lors d'un événement (détection de mouvement, entrée numérique, etc.).

La plupart des automates industriels modernes acceptent les commandes via Modbus TCP (trames binaires standard sur TCP port 502, ou 510 pour le LOGO! Siemens). L'objectif est d'écrire un coil (bit) sur l'automate cible : sortie physique, relais interne, mémento… selon la configuration du projet automate.

Ces deux protocoles sont incompatibles directement : Mobotix ne sait envoyer que du HTTP, pas des trames Modbus TCP brutes. Ce projet implémente un bridge intermédiaire qui :

1. Reçoit la requête HTTP GET de la caméra
2. Répond immédiatement `200 OK` à la caméra (pour ne pas bloquer ses notifications)
3. Envoie la trame Modbus FC05 *(Write Single Coil)* correspondante à l'automate de façon asynchrone pour mettre le coil cible à `1` (ON) ou `0` (OFF)

> Le bridge a été testé sur Siemens LOGO! 8.4. Il est également utilisé avec d'autres automates.
> Tout automate supportant Modbus TCP FC05 est théoriquement compatible — voir la [table de référence](#table-de-référence-modbus-par-automate) ci-dessous.

---

## Matériel requis

```
┌──────────────────┬────────────────────────────────────────────────────────────────────────────────┐
│ Composant        │ Référence                                                                      │
├──────────────────┼────────────────────────────────────────────────────────────────────────────────┤
│ Microcontrôleur  │ Olimex ESP32-POE(-ISO) (obligatoire — PoE, LAN8720A)                           │
|                  | ISO = Isolation galvanique (permet le monitoring USB malgré l'alimentation POE |
│ Automate         │ Tout automate Modbus TCP FC05 (testé : Siemens LOGO! 8.4)                      │
│ Caméra           │ Mobotix avec fonction IP Notify (testé Mx6, Mx7)                               │
│ Alimentation     │ PoE (802.3af) — aucune alimentation externe nécessaire                         │
└──────────────────┴────────────────────────────────────────────────────────────────────────────────┘
```

## Architecture

```
[Caméra Mobotix]
   IP Notify : HTTP GET /notify?memento=1&action=ON
        |
        v  (port 8080)
    (
      Si votre PLC ne le fait pas via votre programmation, sans attendre remettre le memento à 0
      IP Notify : HTTP GET /notify?memento=1&action=OFF
        |
        v  (port 8080)
    )
[Olimex ESP32-POE-ISO]
   - Répond 200 OK immédiatement
   - Enqueue la commande (FreeRTOS queue)
   - modbusTask envoie la trame Modbus FC05
        |
        v  (port 502 standard, ou 510 pour LOGO!, Modbus TCP)
[Automate cible]
   - Coil N mis à 0 ou 1 (sortie, relais interne, mémento…)

```

---

## Installation

### Dépendances Arduino

- Arduino IDE ≥ 2.x
- Board package : `esp32 by Espressif` ≥ 3.0.0
  - carte Olimex-POE-ISO (avec isolation galvanique): https://www.olimex.com/Products/IoT/ESP32/ESP32-POE/open-source-hardware
  - Doc Olimex:  https://www.olimex.com/Products/Duino/_resources/Arduino_instructions.pdf
  - Dans Arduino IDE : *Fichier → Préférences → URL supplémentaires* :
    Implémenter les URL préconisées par Olimex    
  - Puis sélectionnez votre carte Olimex

### Sélection de la carte

*Outils → Type de carte → Gestionnaire de cartes* → sélectionner ESP32 ARDUINO -> OLIMEX ESP32-POE-ISO

### Compilation et flash

1. Ouvrir `modbus_bridge_esp32/modbus_bridge_esp32.ino` dans Arduino IDE
2. Adapter les valeurs par défaut dans le code (section `DEFAULT_*`) :
   - `DHCP` — true ou false
   - `DEFAULT_LOCAL_IP` — IP souhaitée pour le bridge
   - `DEFAULT_GATEWAY` / `DEFAULT_SUBNET`
   - `DEFAULT_LISTEN_PORT` — port d'écoute du bridge
   - `DEFAULT_MODBUS_IP` — IP de l'automate
   - `DEFAULT_MODBUS_PORT` — port Modbus TCP de l'automate (`510` pour LOGO!, `502` pour tous les autres)
   - `DEFAULT_MODBUS_COIL_BASE` — adresse de base du premier coil (`0x2040` pour LOGO!, `0x0000` pour la majorité des autres automates — voir [table de référence](#table-de-référence-modbus-par-automate))
   - `DEFAULT_MODBUS_UNIT_ID` — Unit ID Modbus (`0x01` pour la majorité, `0xFF` pour les automates Schneider/Modicon)
   - `ADMIN_USER` / `ADMIN_PASSWORD` — mot de passe admin pour la route `/set`
   - `DEFAULT_USER` / `DEFAULT_PASSWORD` — credentials caméra
3. Connecter l'Olimex via USB (seulement pour le premier flash)
4. Flasher (*Croquis → Téléverser*)

Après le premier flash, l'ESP32 est alimenté par PoE et la connexion USB n'est plus nécessaire. La configuration se fait entièrement via HTTP.

---

## Configuration de l'automate

### Siemens LOGO! 8.4
Dans LOGO!Soft Comfort ou l'interface web du LOGO! :
- Activer **Modbus TCP Server**
- Port : `510` *(non standard)*
- L'ESP32 doit être sur le même sous-réseau

### Schneider M340 / M580
Dans EcoStruxure Control Expert (Unity Pro) :
- Activer le service **Modbus TCP** dans la configuration réseau de la CPU
- Port : `502` *(standard)*
- Identifier les adresses Modbus des sorties `%Q` ou bits internes `%M` dans la table de mappage du projet
- Unit ID Schneider : `0xFF` (255)

### Autres automates
Voir la [table de référence](#table-de-référence-modbus-par-automate) en bas de ce document.

---

## Configuration Mobotix (IP Notify)

Dans l'interface web de la caméra :
- *Setup → Event Control → IP Notify*
- IP NOTIFY TYPE : `Custom configuration`
- Destination : `<IP_ESP32>:8080`
- Data protocol :
        `HTTP/1.0 Request`  Transfer protocol
        `/notify`           CGI Path
        `user:password`     HTTP Authentication *(optionnel)*
- Data type : `Plein text`
    Message
    - Pour M1 ON : `memento=1&action=ON`
    - Pour M1 OFF : `memento=1&action=OFF`

---

## API HTTP

### `GET /notify` — Commande memento

| Paramètre | Valeurs      | Description                                              |
|-----------|--------------|----------------------------------------------------------|
| `memento` | 1..64        | Numéro du coil (index 1-basé, relatif à `coil-base`)     |
| `action`  | `ON` / `OFF` | Mettre le coil à 1 ou 0                                  |

```
GET /notify?memento=1&action=ON
→ coil (coil-base + 0) mis à 1  — ex. 0x2040 pour LOGO!, 0x0000 pour M340

GET /notify?memento=2&action=OFF
→ coil (coil-base + 1) mis à 0
```

Protégée par IP whitelist et/ou Basic Auth selon configuration.

---

### `GET /set` — Configuration runtime

Protégée par Basic Auth admin (credentials compilés : `ADMIN_USER` / `ADMIN_PASSWORD`).

```bash
curl -u admin:ADMIN_PASSWORD "http://<IP_ESP32>:8080/set?paramètre=valeur"
```
| Paramètre                       | Effet                                | Reboot ? |
|---------------------------------|--------------------------------------|----------|
| `user=val`                      | Nom d'utilisateur caméra             | Non      |
| `password=val`                  | Mot de passe caméra                  | Non      |
| `checkAuth=0\|1`                | Activer/désactiver Basic Auth caméra | Non      |
| `checkIP=0\|1`                  | Activer/désactiver whitelist IP      | Non      |
| `clearIP=1`                     | Vider la liste des IP autorisées     | Non      |
| `ip1=x.x.x.x` .. `ip10=x.x.x.x` | Ajouter des IP autorisées            | Non      |
| `modbus-ip=x.x.x.x`             | IP de l'automate                     | Non      |
| `modbus-port=N`                 | Port Modbus TCP de l'automate        | Non      |
| `modbus-coil-base=N`            | Adresse de base coil (déc. ou 0xHEX) | Non      |
| `modbus-unit-id=N`              | Unit ID Modbus [0-255]               | Non      |
| `ip=x.x.x.x`                    | IP locale du bridge                  | Oui      |
| `subnet=x.x.x.x`                | Masque réseau                        | Oui      |
| `gateway=x.x.x.x`               | Passerelle                           | Oui      |
| `dhcp=0\|1`                     | Activer/désactiver DHCP              | Oui      |
| `port=N`                        | Port HTTP d'écoute                   | Oui      |

Les paramètres avec reboot déclenchent un `ESP.restart()` automatique 500 ms après la réponse.

Tous les paramètres sont persistés en NVS (flash interne, ~100 000 cycles d'écriture mais inutile d'en abuser).

#### Exemples

```bash
# Activer l'auth caméra avec IP whitelist
curl -u admin:monpass "http://192.168.1.10:8080/set?checkAuth=1&checkIP=1&clearIP=1&ip1=192.168.1.36"

# Reconfigurer pour un Schneider M340 (depuis un projet LOGO!)
curl -u admin:monpass "http://192.168.1.10:8080/set?modbus-ip=192.168.1.50&modbus-port=502&modbus-coil-base=0x0000&modbus-unit-id=255"

# Changer l'IP de l'automate (effet immédiat, sans reboot)
curl -u admin:monpass "http://192.168.1.10:8080/set?modbus-ip=192.168.1.100"

# Changer l'IP du bridge (reboot automatique)
curl -u admin:monpass "http://192.168.1.10:8080/set?ip=192.168.1.20&gateway=192.168.1.1"
```

---

## Reset matériel (BUT1)

Maintenir le bouton BUT1 (GPIO34) enfoncé au démarrage efface toute la configuration NVS et restaure les valeurs compilées par défaut (`DEFAULT_*`).

Utile si l'ESP32 n'est plus accessible sur le réseau suite à un mauvais paramètre `ip` ou `port`.

---

## Table de référence Modbus par automate

Fonction Modbus utilisée : **FC05 (Write Single Coil)**.

L'adresse effective du coil N est : `coil-base + (N - 1)`

| Fabricant       | Modèle                        | Port  | CoilBase | Unit ID | Remarques |
|-----------------|-------------------------------|-------|----------|---------|---|
| Siemens         | LOGO! 8.4 (0BA8)              | **510** | **0x2040** | 0x01  | Mémentos M1..M64 — port non standard ! |
| Siemens         | S7-1200 / S7-1500             | 502   | 0x0000   | 0x01    | Adresses libres via bloc MB_SERVER |
| Schneider       | M340, M580                    | 502   | 0x0000   | **0xFF** | Sorties %Q — conf. Unity Pro / EcoStruxure |
| Schneider       | Modicon M221, M241/M251       | 502   | 0x0000   | **0xFF** | Sorties %Q — EcoStruxure Machine Expert |
| Schneider       | Zelio Logic SR2/SR3           | 502   | 0x0000   | 0xFF    | Sorties Q1.. |
| Wago            | 750-352/362, PFC200           | 502   | 0x0000   | 0x01    | Process image sorties physiques |
| Beckhoff        | CX/BK + Modbus TCP            | 502   | 0x0000   | 0x01    | Dépend du mappage EtherCAT → Modbus |
| Delta           | DVP-EH2/SX2/SS2               | 502   | 0x0000   | 0x01    | Bobines sorties Y0.. |
| Omron           | CP1E/CP1L/NX/NJ               | 502   | 0x0000   | 0x01    | CIO bits — vérifier mappage area |
| Mitsubishi      | iQ-F (FX5U), Q series         | 502   | 0x0000   | 0x01    | Bobines Y (sorties) ou relais M |
| Phoenix Contact | AXC F 1152 / 2152             | 502   | 0x0000   | 0x01    | Sorties configurables dans PLCnext |
| Moxa            | ioLogik E1210/E1212           | 502   | 0x0000   | 0x01    | Digital outputs DO0.. |

> **Note :** `0x0000` est l'adresse standard pour la quasi-totalité des automates modernes.
> `0x2040` est spécifique au LOGO! 8.4 Siemens.
> De nombreux automates permettent de remapper leurs adresses Modbus dans leur outil de configuration — toujours vérifier la documentation du projet automate.

### Adressage LOGO! 8.4 (référence rapide)

| Mémento | Adresse coil Modbus |
|---------|---------------------|
| M1      | `0x2040`            |
| M2      | `0x2041`            |
| M(n)    | `0x2040 + (n-1)`    |
| M64     | `0x207F`            |

---

## Sécurité

- IP whitelist (`checkIP`) : seules les IP déclarées peuvent envoyer des notifications
- Basic Auth caméra (`checkAuth`) : credentials vérifiés sur `/notify`
- Basic Auth admin (fixe, compilée) : protège `/set` contre toute modification non autorisée
- Les deux mécanismes sont indépendants et combinables
- Les credentials admin ne sont pas modifiables via `/set` et ne sont pas stockés en NVS

---

## Licence

MIT

## Author
Delatte Jean-Louis (02/04/2026)
