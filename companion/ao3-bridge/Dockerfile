# syntax=docker/dockerfile:1
FROM python:3.12-slim
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1 BRIDGE_DATA=/data
WORKDIR /app
COPY ao3-bridge/requirements.txt ./requirements.txt
RUN --mount=type=secret,id=proxy_ca \
    if [ -f /run/secrets/proxy_ca ]; then export PIP_CERT=/run/secrets/proxy_ca; fi; \
    pip install --no-cache-dir -r requirements.txt
RUN useradd --uid 10001 --create-home bridge && mkdir /data && chown bridge:bridge /data
COPY --chmod=0444 optimizer.py ./optimizer.py
COPY --chmod=0444 ao3-bridge/ao3.py ao3-bridge/bridge.py ao3-bridge/index.html ao3-bridge/app.js ./
USER 10001:10001
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=5s CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/healthz', timeout=3)"
CMD ["python", "bridge.py"]
