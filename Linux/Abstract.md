Abstract:
Piloter les entrées binaires (mementos) M1 et M2 via Modbus TCP.
M1 et M2 sont contrôlés via des trames Modbus TCP.
Renforcer une distribution raspbian pour supporter le script python

Détail de la trame Modbus TCP:
 - Transaction Identifier: 2 bytes (0x0001)
 - Protocol Identifier: 2 bytes (0x0000)
 - Length: 2 bytes (0x0006)
 - Unit Identifier: 1 byte (0x01)
 - Function Code: 1 byte (0x05)
 - Address: 2 bytes (0x2040 pour M1, 0x2041 pour M2)
 - Value: 2 bytes (0xFF00 pour ON, 0x0000 pour OFF)


M1 ON:
'\x00\x01\x00\x00\x00\x06\x01\x05\x20\x40\xFF\x00'
00 01 00 00 00 06 01 05 20 40 FF 00
M1 OFF:
'\x00\x01\x00\x00\x00\x06\x01\x05\x20\x40\x00\x00'
00 01 00 00 00 06 01 05 20 40 00 00
M2 ON:
'\x00\x01\x00\x00\x00\x06\x01\x05\x20\x41\xFF\x00'
M2 OFF:
'\x00\x01\x00\x00\x00\x06\x01\x05\x20\x41\x00\x00'


Commandes de test (Linux):
TEST CONNEXION MODBUS
nc -zv 192.168.129.199 510 2>&1

M1 ON
printf '\x00\x01\x00\x00\x00\x06\x01\x05\x20\x40\xFF\x00' | nc -w2 192.168.129.199 510 | od -A x -t x1z
M1 OFF
printf '\x00\x01\x00\x00\x00\x06\x01\x05\x20\x40\x00\x00' | nc -w2 192.168.129.199 510 | od -A x -t x1z

M2 ON
printf '\x00\x01\x00\x00\x00\x06\x01\x05\x20\x41\xFF\x00' | nc -w2 192.168.129.199 510 | od -A x -t x1z
M2 OFF
printf '\x00\x01\x00\x00\x00\x06\x01\x05\x20\x41\x00\x00' | nc -w2 192.168.129.199 510 | od -A x -t x1z

(Tésté et fonctionnel)

Configuration IP notify Mobotix:
 Destination address: ip host:510
#   Data protocol: Raw TCP/IP
#   Data to send:
#     M1 ON: 00 01 00 00 00 06 01 05 20 40 FF 00
(En cours)

Préparation d'une distribution linux minimaliste pour fonctionner sur un ODROID XU4
 - Réduction de la puissance CPU sur 'on demand'
 - Création d'un compte user (user) et un admin (administrateurs)
 - Définir un mot de passe root sécurisé
 - Désinstaller SUDO
 - Configuration SSH
   - no root access
   - utilisateurs autorisés: 
   - Authentification par clé uniquement (PasswordAuthentication no)
   - Clé autorisée : 
   - AllowTcpForwarding no
   - X11Forwarding no
   - Algorithmes de chiffrement modernes uniquement (ciphers/MACs)
 - setup network
     - eth0  (réception IP notify)
     - eth1  (USB NIC INTERFACE -> MODBUS)
     - désactiver IP V6
     - Désactiver le routage IP (net.ipv4.ip_forward=0)
     - net.ipv4.conf.all.rp_filter=1
     - net.ipv4.tcp_syncookies=1
     - fs.suid_dumpable=0 (désactiver core dumps)
 - Désactiver les services inutiles (avahi-daemon, bluetooth, cups)
 - Désactiver le montage automatique des périphériques USB
 - Désactiver les consoles physiques série (getty)
 - Setup firewall pour sécuriser la machine (fermeture de tout ce qui est inutile, laisser NTP, SSH, modbus-bridge in/out)
 - Setup fail2ban (ssh et modbus_bridge)
 - Désactiver les mises à jour automatiques (réseau isolé, non connecté à Internet)
 - Synchro NTP sur 
 - Réduire les écritures au minimum !
   - journald : Storage=volatile, limiter la taille max des logs
   - logrotate agressif
 - Service modbus-bridge.service
   - Exécuter sous un utilisateur dédié non-privilégié
   - Sandboxing systemd : NoNewPrivileges=true, ProtectSystem=strict, ProtectHome=true, PrivateTmp=true
 - Installation du service modbus-bridge.service
 
 Concrètement:
> Toutes les commandes sont exécutées en tant que **root** (sudo est désinstallé).

**1. CPU governor 'ondemand'**
```bash
apt install cpufrequtils
echo 'GOVERNOR="ondemand"' > /etc/default/cpufrequtils
systemctl enable cpufrequtils
```

**2. Création des comptes**
```bash
useradd -m -s /bin/bash user
useradd -m -s /bin/bash admin
passwd user
passwd admin
```

**3. Mot de passe root**
```bash
passwd root
```

**4. Désinstaller sudo**
```bash
apt remove --purge sudo -y
```

**5. Configuration SSH**

Modifier `/etc/ssh/sshd_config` :
```

systemctl restart sshd
```

**6. Configuration réseau**

`/etc/network/interfaces` :
```
auto lo
iface lo inet loopback

auto eth0
iface eth0 inet static
    address 
    netmask 

auto eth1
iface eth1 inet static
    address 
    netmask 
```

`/etc/sysctl.d/99-hardening.conf` :
```
net.ipv6.conf.all.disable_ipv6 = 1
net.ipv6.conf.default.disable_ipv6 = 1
net.ipv4.ip_forward = 0
net.ipv4.conf.all.rp_filter = 1
net.ipv4.tcp_syncookies = 1
fs.suid_dumpable = 0
```
```bash
sysctl --system
```

**7. Désactiver les services inutiles**
```bash
systemctl disable --now avahi-daemon bluetooth cups 2>/dev/null || true
apt remove --purge avahi-daemon bluez cups -y
```

**8. Désactiver le montage automatique USB**
```bash
cat > /etc/udev/rules.d/85-no-automount.rules << 'EOF'
SUBSYSTEM=="block", SUBSYSTEMS=="usb", ENV{UDISKS_AUTO}="0", ENV{UDISKS_IGNORE}="1"
EOF
udevadm control --reload-rules
```

**9. Désactiver les consoles série**
```bash
# Port série standard
systemctl disable --now serial-getty@ttyS0.service
systemctl mask serial-getty@ttyS0.service
# ODROID XU4 - UART Samsung (ttySAC)
for i in 0 1 2 3; do
    systemctl disable --now serial-getty@ttySAC${i}.service 2>/dev/null || true
    systemctl mask serial-getty@ttySAC${i}.service 2>/dev/null || true
done
```

**10. Firewall (iptables)**

`/etc/iptables/modbus-bridge.rules` :
```
*filter
:INPUT DROP [0:0]
:FORWARD DROP [0:0]
:OUTPUT ACCEPT [0:0]
# Loopback
-A INPUT -i lo -j ACCEPT
# Connexions établies / liées
-A INPUT -m state --state ESTABLISHED,RELATED -j ACCEPT
# SSH depuis eth0 (réseau 10.2.1.x)
-A INPUT -i eth0 -p tcp --dport 22 -m state --state NEW -j ACCEPT
# Modbus TCP entrant depuis eth0 (IP notify Mobotix -> bridge)
-A INPUT -i eth0 -p tcp --dport 510 -m state --state NEW -j ACCEPT
COMMIT
```
```bash
apt install iptables-persistent -y
iptables-restore < /etc/iptables/modbus-bridge.rules
netfilter-persistent save
```

**11. fail2ban**
```bash
apt install fail2ban -y
```

`/etc/fail2ban/jail.local` :
```ini
[DEFAULT]
bantime  = 3600
findtime = 600
maxretry = 5

[sshd]
enabled  = true
port     = ssh
logpath  = /var/log/auth.log

[modbus-bridge]
enabled  = true
port     = 510
logpath  = /var/log/syslog
maxretry = 10
bantime  = 7200
```

`/etc/fail2ban/filter.d/modbus-bridge.conf` :
```ini
[Definition]
failregex = .*modbus.bridge.*error.*<HOST>.*
            .*modbus.bridge.*refused.*<HOST>.*
ignoreregex =
```
```bash
systemctl enable --now fail2ban
```

**12. Désactiver les mises à jour automatiques**
```bash
systemctl disable --now unattended-upgrades apt-daily.timer apt-daily-upgrade.timer 2>/dev/null || true
apt remove --purge unattended-upgrades -y
```

**13. NTP sur **

`/etc/systemd/timesyncd.conf` :
```ini
[Time]
NTP=
FallbackNTP=
```
```bash
timedatectl set-ntp true
systemctl restart systemd-timesyncd
timedatectl status
```

**14. Réduire les écritures**

`/tmp` et `/var/tmp` en RAM — ajouter dans `/etc/fstab` :
```
tmpfs  /tmp     tmpfs  defaults,noatime,nosuid,nodev,noexec,size=64M  0 0
tmpfs  /var/tmp tmpfs  defaults,noatime,nosuid,nodev,noexec,size=32M  0 0
```

`/etc/systemd/journald.conf` :
```ini
[Journal]
Storage=volatile
SystemMaxUse=20M
RuntimeMaxUse=20M
```

`/etc/logrotate.d/custom` :
```
/var/log/*.log {
    daily
    rotate 3
    compress
    missingok
    notifempty
}
```
```bash
systemctl restart systemd-journald
```

**15. Création de l'utilisateur dédié modbus-bridge**
```bash
useradd -r -s /usr/sbin/nologin -d /opt/modbus-bridge modbus-bridge
```

**16. Installation du service modbus-bridge**
```bash
mkdir -p /opt/modbus-bridge
cp modbus_bridge.py /opt/modbus-bridge/
chown -R modbus-bridge:modbus-bridge /opt/modbus-bridge
chmod 750 /opt/modbus-bridge
chmod 640 /opt/modbus-bridge/modbus_bridge.py
cp modbus-bridge.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable --now modbus-bridge
systemctl status modbus-bridge
```
