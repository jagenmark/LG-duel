#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
STATE_DIR="${ROOT_DIR}/.server"
PID_FILE="${STATE_DIR}/lg-duel-server.pid"
LOG_FILE="${STATE_DIR}/lg-duel-server.log"
PORT_FILE="${STATE_DIR}/lg-duel-server.port"
SERVER="${ROOT_DIR}/build/default/lg_duel_server"
PROBE="${ROOT_DIR}/build/default/lg_duel_server_probe"
SERVICE_NAME="lg-duel-server.service"
ACTION="${1:-status}"
PORT="${2:-27960}"

has_user_service() {
  systemctl --user cat "${SERVICE_NAME}" >/dev/null 2>&1
}

read_pid() {
  if [[ -f "${PID_FILE}" ]]; then
    cat "${PID_FILE}"
  fi
}

is_running() {
  local pid
  pid="$(read_pid)"
  [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null
}

build_server() {
  cmake --preset default
  cmake --build --preset default --target lg_duel_server lg_duel_server_probe
}

start_server() {
  if has_user_service && [[ "${PORT}" == "27960" ]]; then
    build_server
    systemctl --user start "${SERVICE_NAME}"
    sleep 0.3
    if ! systemctl --user is-active --quiet "${SERVICE_NAME}"; then
      systemctl --user status --no-pager "${SERVICE_NAME}" >&2
      return 1
    fi
    "${PROBE}" 127.0.0.1 "${PORT}"
    echo "LG Duel server is running as user service ${SERVICE_NAME} on UDP ${PORT}."
    return 0
  fi

  if is_running; then
    echo "LG Duel server is already running (PID $(read_pid))."
    return 0
  fi

  if [[ ! "${PORT}" =~ ^[0-9]+$ ]] || ((PORT < 1 || PORT > 65535)); then
    echo "Invalid UDP port: ${PORT}" >&2
    return 2
  fi

  mkdir -p "${STATE_DIR}"
  build_server
  if "${PROBE}" 127.0.0.1 "${PORT}"; then
    echo "An LG Duel server is already responding on UDP ${PORT}."
    echo "It was not started by this control script, so no PID or log was adopted."
    return 0
  fi

  : >"${LOG_FILE}"
  nohup stdbuf -oL -eL "${SERVER}" "${PORT}" </dev/null >>"${LOG_FILE}" 2>&1 &
  local pid=$!
  echo "${pid}" >"${PID_FILE}"
  echo "${PORT}" >"${PORT_FILE}"

  sleep 0.3
  if ! kill -0 "${pid}" 2>/dev/null; then
    echo "Server failed to start. Log:" >&2
    cat "${LOG_FILE}" >&2
    rm -f "${PID_FILE}"
    return 1
  fi

  if ! "${PROBE}" 127.0.0.1 "${PORT}"; then
    echo "Server process started, but its UDP handshake check failed." >&2
    return 1
  fi

  echo "LG Duel server running on UDP ${PORT} (PID ${pid})."
  echo "Log: ${LOG_FILE}"
}

stop_server() {
  if has_user_service && systemctl --user is-active --quiet "${SERVICE_NAME}"; then
    systemctl --user stop "${SERVICE_NAME}"
    echo "LG Duel user service stopped."
    return 0
  fi

  if ! is_running; then
    echo "LG Duel server is not running."
    rm -f "${PID_FILE}"
    return 0
  fi

  local pid
  pid="$(read_pid)"
  kill "${pid}"
  for _ in {1..30}; do
    if ! kill -0 "${pid}" 2>/dev/null; then
      rm -f "${PID_FILE}"
      echo "LG Duel server stopped."
      return 0
    fi
    sleep 0.1
  done

  echo "Server did not stop within 3 seconds (PID ${pid})." >&2
  return 1
}

show_status() {
  local port="${PORT}"
  if [[ -f "${PORT_FILE}" ]]; then
    port="$(cat "${PORT_FILE}")"
  fi

  if has_user_service && systemctl --user is-active --quiet "${SERVICE_NAME}"; then
    echo "LG Duel user service is active on UDP 27960."
    "${PROBE}" 127.0.0.1 27960
  elif is_running; then
    echo "LG Duel server is running (PID $(read_pid), UDP ${port})."
    "${PROBE}" 127.0.0.1 "${port}"
  elif "${PROBE}" 127.0.0.1 "${port}"; then
    echo "An externally started LG Duel server is responding on UDP ${port}."
  else
    echo "LG Duel server is not running."
    return 1
  fi
}

case "${ACTION}" in
  start)
    start_server
    ;;
  stop)
    stop_server
    ;;
  restart)
    stop_server
    start_server
    ;;
  status)
    show_status
    ;;
  logs)
    if has_user_service; then
      journalctl --user-unit "${SERVICE_NAME}" --no-pager -n 100
    elif [[ -f "${LOG_FILE}" ]]; then
      tail -n 100 "${LOG_FILE}"
    else
      echo "No server log exists yet."
    fi
    ;;
  *)
    echo "Usage: $0 {start|stop|restart|status|logs} [udp-port]" >&2
    exit 2
    ;;
esac
