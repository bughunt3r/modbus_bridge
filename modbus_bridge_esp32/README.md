# Mobotix → Siemens LOGO! 8.4 Bridge

Bridge HTTP→Modbus TCP sur **Olimex ESP32-POE-ISO**, permettant à des caméras **Mobotix** de piloter les mementos d'un automate **Siemens LOGO! 8.4** via la fonction IP Notify.

---

## Contexte et problème résolu

Les caméras Mobotix disposent d'une fonction **IP Notify** capable d'envoyer une requête HTTP GET lors d'un événement (détection de mouvement, entrée numérique, etc.).

Le LOGO! 8.4 de Siemens accepte les commandes via **Modbus TCP** (trames binaires sur TCP port 510). L'objectif est d'agir sur les **mementos** internes du LOGO! (M1..M64) : des bits d'état que le programme LOGO! peut lire et utiliser dans sa logique (déclenchement d'alarme, ouverture de porte, activation d'une sortie, etc.).

Ces deux protocoles sont incompatibles directement : Mobotix ne sait envoyer que du HTTP, pas des trames Modbus TCP brutes. Ce projet implémente un bridge intermédiaire qui :

1. Reçoit la requête HTTP GET de la caméra
2. Répond immédiatement `200 OK` à la caméra (pour ne pas bloquer ses notifications)
3. Envoie la trame Modbus FC05 correspondante au LOGO! de façon asynchrone pour mettre le memento cible à `1` (ON) ou `0` (OFF)

> **Ce projet n'est pas un bridge Modbus TCP universel.**
Il a été spécifiquement conçu pour l'intégration Mobotix IP Notify → mementos Siemens LOGO! 8.4 mais ne s'y limite pas non plus.

---

## Matériel requis

```
┌──────────────────┬────────────────────────────────────────────────────────────┐
│ Composant        │ Référence                                                  │
├──────────────────┼────────────────────────────────────────────────────────────┤
│ Microcontrôleur  │ Olimex ESP32-POE(-ISO) (obligatoire — PoE, LAN8720A)       │
│ Automate         │ Siemens LOGO! 8.4 (testé avec firmware ≥ 8.4)              │
│ Caméra           │ Mobotix avec fonction IP Notify (testé Mx6, Mx7)           │
│ Alimentation     │ PoE (802.3af) — aucune alimentation externe nécessaire     │
└──────────────────┴────────────────────────────────────────────────────────────┘
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
        v  (port 510, Modbus TCP)
[Siemens LOGO! 8.4]
   - Memento M1..M64 mis à 0 ou 1

```

---

## Installation

### Dépendances Arduino

- **Arduino IDE** ≥ 2.x
- **Board package** : `esp32 by Espressif` ≥ 3.0.0
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
   - `DHCP`- true ou false
   - `DEFAULT_LOCAL_IP` — IP souhaitée pour le bridge
   - `DEFAULT_GATEWAY` / `DEFAULT_SUBNET`
   - `DEFAULT_LISTEN_PORT` - Port d'écoue pour le bridge
   - `DEFAULT_MODBUS_IP` — IP du LOGO!
   - `DEFAULT_MODBUS_PORT` - Port découte modbus du LOGO! (510)
   - `DEFAULT_USER` / `DEFAULT_USER`
   - `ADMIN_USER` / `ADMIN_PASSWORD` — mot de passe admin pour la route `/set`
   - `DEFAULT_USER` / `DEFAULT_PASSWORD` — credentials caméra
3. Connecter l'Olimex via USB (seulement pour le premier flash)
4. Flasher (*Croquis → Téléverser*)

Après le premier flash, l'ESP32 est alimenté par PoE et **la connexion USB n'est plus nécessaire**. La configuration se fait entièrement via HTTP.

---

## Configuration réseau LOGO!

Dans LOGO!Soft Comfort ou l'interface web du LOGO! :
- Activer **Modbus TCP Server**
- Port : `510`
- L'ESP32 doit être sur le même sous-réseau

---

## Configuration Mobotix (IP Notify)

Dans l'interface web de la caméra :
- *Setup → Event Control → IP Notify*
- **IP NOTIFY TYPE** : `Custom configuration`
- **Destination** : `<IP_ESP32>:8080`
- **Data protocol** :
        `HTTP/1.0 Request`  **Transfer protocol**
        `/notify`           **CGI Path**
        `user:password`     **HTTP Authentication** *(optionnel)*
- **Data type** : `Plein text`
    **Message**
    - Pour M1 ON : `memento=1&action=ON`
    - Pour M1 OFF : `memento=1&action=OFF`

---

## API HTTP

### `GET /notify` — Commande memento

| Paramètre | Valeurs      | Description                       |
|-----------|--------------|-----------------------------------|
| `memento` | 1..64        | Numéro du memento LOGO! (M1..M64) |
| `action`  | `ON` / `OFF` | Mettre le memento à 1 ou 0        |

```
GET /notify?memento=1&action=ON
→ M1 mis à 1 (coil Modbus 0x2040)

GET /notify?memento=2&action=OFF
→ M2 mis à 0 (coil Modbus 0x2041)
```

Protégée par IP whitelist et/ou Basic Auth selon configuration.

---

### `GET /set` — Configuration runtime

Protégée par **Basic Auth admin** (credentials compilés : `ADMIN_USER` / `ADMIN_PASSWORD`).

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
| `modbus-ip=x.x.x.x`             | IP du LOGO!                          | Non      |
| `modbus-port=N`                 | Port Modbus du LOGO!                 | Non      |
| `ip=x.x.x.x`                    | IP locale du bridge                  | **Oui**  |
| `subnet=x.x.x.x`                | Masque réseau                        | **Oui**  |
| `gateway=x.x.x.x`               | Passerelle                           | **Oui**  |
| `dhcp=0\|1`                     | Activer/désactiver DHCP              | **Oui**  |
| `port=N`                        | Port HTTP d'écoute                   | **Oui**  |

Les paramètres avec reboot déclenchent un `ESP.restart()` automatique 500 ms après la réponse.

Tous les paramètres sont persistés en **NVS** (flash interne, ~100 000 cycles d'écriture).

#### Exemples

```bash
# Activer l'auth caméra avec IP whitelist
curl -u admin:monpass "http://192.168.1.10:8080/set?checkAuth=1&checkIP=1&clearIP=1&ip1=192.168.1.36"

# Changer l'IP du LOGO! (effet immédiat, sans reboot)
curl -u admin:monpass "http://192.168.1.10:8080/set?modbus-ip=192.168.1.100"

# Changer l'IP du bridge (reboot automatique)
curl -u admin:monpass "http://192.168.1.10:8080/set?ip=192.168.1.20&gateway=192.168.1.1"
```

---

## Reset matériel (BUT1)

Maintenir le bouton **BUT1** (GPIO34) enfoncé au **démarrage** efface toute la configuration NVS et restaure les valeurs compilées par défaut (`DEFAULT_*`).

Utile si l'ESP32 n'est plus accessible sur le réseau suite à un mauvais paramètre `ip` ou `port`.

---

## Adressage Modbus LOGO! 8.4

| Memento | Adresse coil Modbus |
|---------|---------------------|
| M1      | `0x2040`            |
| M2      | `0x2041`            |
| M(n)    | `0x2040 + (n-1)`    |
| M64     | `0x207F`            |

Fonction Modbus utilisée : **FC05** (Write Single Coil).

---

## Sécurité

- **IP whitelist** (`checkIP`) : seules les IP déclarées peuvent envoyer des notifications
- **Basic Auth caméra** (`checkAuth`) : credentials vérifiés sur `/notify`
- **Basic Auth admin** (fixe, compilée) : protège `/set` contre toute modification non autorisée
- Les deux mécanismes sont indépendants et combinables
- Les credentials admin ne sont **pas** modifiables via `/set` et ne sont **pas** stockés en NVS

---

## Licence

MIT

## Author
Delatte Jean-Louis (02/04/2026)
