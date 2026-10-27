#!/bin/sh
# Genera password file e ACL dalle variabili d'ambiente, poi avvia mosquitto.
set -eu

: "${MQTT_PUBLISHER_USER:?manca MQTT_PUBLISHER_USER}"
: "${MQTT_PUBLISHER_PASSWORD:?manca MQTT_PUBLISHER_PASSWORD}"
: "${MQTT_BBS_USER:?manca MQTT_BBS_USER}"
: "${MQTT_BBS_PASSWORD:?manca MQTT_BBS_PASSWORD}"
TOPIC="${NEWS_TOPIC:-bbs/news}"

if [ "$MQTT_PUBLISHER_USER" = "$MQTT_BBS_USER" ]; then
    echo "MQTT_PUBLISHER_USER e MQTT_BBS_USER devono essere diversi" >&2
    exit 1
fi
for v in "$MQTT_PUBLISHER_USER" "$MQTT_BBS_USER" "$TOPIC"; do
    case "$v" in
        *[[:space:]:\#+]*|"") echo "valore non ammesso (spazi, ':', '#', '+' o vuoto): '$v'" >&2; exit 1 ;;
    esac
done

CONF=/mosquitto/config
PASSWD="$CONF/passwd"
ACL="$CONF/acl"

rm -f "$PASSWD"
mosquitto_passwd -b -c "$PASSWD" "$MQTT_PUBLISHER_USER" "$MQTT_PUBLISHER_PASSWORD"
mosquitto_passwd -b "$PASSWD" "$MQTT_BBS_USER" "$MQTT_BBS_PASSWORD"

cat > "$ACL" <<EOT
# generato all'avvio: non modificare
user $MQTT_PUBLISHER_USER
topic write $TOPIC

user $MQTT_BBS_USER
topic read $TOPIC
EOT

chown mosquitto:mosquitto "$PASSWD" "$ACL" 2>/dev/null || true
chmod 0600 "$PASSWD" "$ACL"
chown -R mosquitto:mosquitto /mosquitto/data 2>/dev/null || true

echo "mosquitto: utenti configurati (publisher=$MQTT_PUBLISHER_USER write, bbs=$MQTT_BBS_USER read) topic=$TOPIC"
exec "$@"
