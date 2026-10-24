# The ghost relay (apworld/las/ghost_relay.py) as a container image (docs/ghost-relay.md). Build from the repository
# root: docker build -f docker/ghost-relay/Dockerfile -t las-ghost-relay .
# Arguments after the image name go to the relay (--password, --trusted-proxy, --nogroups, ...).
FROM python:3.12-alpine
ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
COPY apworld/las/ghost_relay.py /app/ghost_relay.py
USER 65534:65534
EXPOSE 43390
HEALTHCHECK --interval=30s --timeout=5s \
    CMD ["python", "-c", "import socket; socket.create_connection(('127.0.0.1', 43390), 3).close()"]
ENTRYPOINT ["python", "/app/ghost_relay.py", "--host", "0.0.0.0", "--port", "43390"]
