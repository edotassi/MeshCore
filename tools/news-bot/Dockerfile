FROM python:3.12-slim

ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 PIP_NO_CACHE_DIR=1

RUN useradd --system --uid 10001 --create-home newsbot \
    && mkdir /data && chown newsbot /data

WORKDIR /app
COPY requirements.txt .
RUN pip install -r requirements.txt

COPY news_bot ./news_bot
COPY feeds.yaml .

USER newsbot
VOLUME /data
ENV STATE_PATH=/data/state.db FEEDS_FILE=/app/feeds.yaml

CMD ["python", "-m", "news_bot"]
