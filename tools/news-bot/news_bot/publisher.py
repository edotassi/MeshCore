"""Pubblicazione MQTT (paho-mqtt 2.x) con attesa dell'ack QoS 1."""

from __future__ import annotations

import logging
import threading

import paho.mqtt.client as mqtt

log = logging.getLogger(__name__)


class MqttPublisher:
    """Connessione lazy: si collega solo al primo publish. Riconnette da sola."""

    def __init__(self, host, port, user, password, topic, timeout=15):
        self.host, self.port, self.topic, self.timeout = host, port, topic, timeout
        self._connected = threading.Event()
        self._started = False
        self._client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="news-bot")
        if user:
            self._client.username_pw_set(user, password)
        self._client.reconnect_delay_set(min_delay=1, max_delay=30)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            log.error("connessione MQTT rifiutata: %s", reason_code)
        else:
            log.info("connesso a MQTT %s:%d", self.host, self.port)
            self._connected.set()

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        self._connected.clear()
        log.warning("disconnesso da MQTT: %s", reason_code)

    def _ensure_connected(self) -> bool:
        if not self._started:
            self._client.connect_async(self.host, self.port, keepalive=60)
            self._client.loop_start()
            self._started = True
        return self._connected.wait(self.timeout)

    def publish(self, payload: str) -> bool:
        """True solo se il broker ha confermato (PUBACK)."""
        if not self._ensure_connected():
            log.error("broker MQTT non raggiungibile (%s:%d)", self.host, self.port)
            return False
        info = self._client.publish(self.topic, payload.encode("utf-8"), qos=1, retain=False)
        try:
            info.wait_for_publish(timeout=self.timeout)
        except (ValueError, RuntimeError) as exc:
            log.error("publish fallito: %s", exc)
            return False
        if not info.is_published():
            log.error("nessun ack dal broker entro %ds", self.timeout)
            return False
        return True

    def close(self) -> None:
        if self._started:
            self._client.disconnect()
            self._client.loop_stop()
            self._started = False


class DryRunPublisher:
    def __init__(self, out=print):
        self._out = out

    def publish(self, payload: str) -> bool:
        self._out("--- [dry-run] messaggio che verrebbe pubblicato ---")
        self._out(payload)
        return True

    def close(self) -> None:
        pass
