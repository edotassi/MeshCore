FROM eclipse-mosquitto:2.0.20

COPY mosquitto.conf /mosquitto/config/mosquitto.conf
COPY docker-entrypoint.sh /usr/local/bin/news-entrypoint.sh
RUN chmod +x /usr/local/bin/news-entrypoint.sh

ENTRYPOINT ["/usr/local/bin/news-entrypoint.sh"]
CMD ["/usr/sbin/mosquitto", "-c", "/mosquitto/config/mosquitto.conf"]
