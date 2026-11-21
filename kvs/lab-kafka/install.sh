#!/usr/bin/env bash
# install.sh — установка обёртки lab_kafka и стенда для практики 10.
#
#   ./install.sh A      # вариант A, KVS,   топик kvs.events
#   ./install.sh B      # вариант B, CHAT,  топик chat.events
#   ./install.sh C      # вариант C, TASKQ, топик taskq.events
#
# Запускать от своего пользователя, не через sudo: пароль спросят там,
# где он действительно нужен (установка в /usr/local и в /opt/lab-env).
# Повторный запуск безопасен.

set -euo pipefail

KIT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$(mktemp -d /tmp/lab-kafka-build.XXXXXX)"
ENV_DIR=/opt/lab-env
trap 'rm -rf "$BUILD_DIR"' EXIT

ok()   { printf '  \033[32m✓\033[0m %s\n' "$*"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$*"; }
fail() { printf '  \033[31m✗\033[0m %s\n' "$*"; exit 1; }
step() { printf '\n\033[1m%s\033[0m\n' "$*"; }

# --- Вариант ------------------------------------------------------------------
case "${1:-}" in
    A|a) VARIANT=A_KVS;   TOPIC=kvs.events   ;;
    B|b) VARIANT=B_CHAT;  TOPIC=chat.events  ;;
    C|c) VARIANT=C_TASKQ; TOPIC=taskq.events ;;
    *)   echo "использование: ./install.sh A|B|C   (буква вашего варианта)"; exit 2 ;;
esac
[[ $EUID -ne 0 ]] || fail "запускайте без sudo, от своего пользователя"

# --- 1. Что нужно для сборки ----------------------------------------------------
step "1. Проверка окружения"
missing=()
for tool in cmake ninja c++ pkg-config docker; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
((${#missing[@]} == 0)) || fail "нет программ: ${missing[*]}. Образ развёрнут не по инструкции — обратитесь к преподавателю"

cmake_ver=$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+')
[[ "$(printf '%s\n3.28\n' "$cmake_ver" | sort -V | head -1)" == "3.28" ]] \
    || fail "нужен CMake не старше 3.28, установлен $cmake_ver"
ok "CMake $cmake_ver, Ninja, компилятор $(c++ -dumpfullversion 2>/dev/null || c++ --version | head -1)"

if ! pkg-config --exists rdkafka; then
    warn "нет librdkafka — ставлю пакет librdkafka-dev"
    sudo apt-get install -y librdkafka-dev
fi
ok "librdkafka $(pkg-config --modversion rdkafka)"

docker compose version >/dev/null 2>&1 || fail "нет docker compose (пакет docker-compose-v2)"
if ! docker info >/dev/null 2>&1; then
    if id -nG | grep -qw docker; then
        fail "Docker не отвечает: sudo systemctl start docker"
    else
        fail "пользователь не в группе docker: sudo usermod -aG docker \$USER, затем перезайдите в систему"
    fi
fi
ok "Docker $(docker version --format '{{.Server.Version}}'), $(docker compose version --short 2>/dev/null || echo compose)"

# --- 2. Сборка и самопроверка ----------------------------------------------------
step "2. Сборка обёртки"
cmake -S "$KIT_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR"
ok "собрано"

if [[ -x "$BUILD_DIR/lab_kafka_selftest" ]]; then
    echo "  самопроверка на макете кластера, около минуты..."
    if ctest --test-dir "$BUILD_DIR" --output-on-failure >"$BUILD_DIR/ctest.log" 2>&1; then
        ok "самопроверка прошла"
    else
        cat "$BUILD_DIR/ctest.log"
        fail "самопроверка не прошла — пришлите этот вывод преподавателю"
    fi
else
    warn "самопроверка не собрана (в librdkafka нет rdkafka_mock.h) — пропускаю"
fi

# --- 3. Установка ----------------------------------------------------------------
step "3. Установка в /usr/local"
sudo cmake --install "$BUILD_DIR" --prefix /usr/local >/dev/null
sudo install -m 755 "$KIT_DIR/tools/lab-stand" /usr/local/bin/lab-stand
ok "заголовок:  /usr/local/include/lab/kafka/event_bus.hpp"
ok "заготовка:  /usr/local/share/lab_kafka/examples/kafka_event_publisher_$VARIANT.cpp"
ok "утилиты:    lab_kafka_probe, lab-stand"

if [[ -d /usr/local/include/kvs/kafka ]]; then
    warn "найдена старая обёртка kvs_kafka; она больше не нужна, но и не мешает"
fi

# --- 4. Стенд ----------------------------------------------------------------------
step "4. Стенд в $ENV_DIR"
sudo mkdir -p "$ENV_DIR"
sudo chown "$USER:$USER" "$ENV_DIR"

new_compose="$KIT_DIR/stand/docker-compose.$VARIANT.yml"
if [[ -f "$ENV_DIR/docker-compose.yml" ]] && ! cmp -s "$new_compose" "$ENV_DIR/docker-compose.yml"; then
    backup="$ENV_DIR/docker-compose.yml.$(date +%Y%m%d-%H%M%S).bak"
    (cd "$ENV_DIR" && docker compose down >/dev/null 2>&1 || true)
    mv "$ENV_DIR/docker-compose.yml" "$backup"
    warn "прежний docker-compose.yml сохранён как $backup"
fi
cp "$new_compose" "$ENV_DIR/docker-compose.yml"
printf 'LAB_VARIANT=%s\nLAB_TOPIC=%s\n' "$VARIANT" "$TOPIC" > "$ENV_DIR/lab-variant"
ok "docker-compose.yml варианта ${VARIANT%%_*}, топик $TOPIC"

# Контейнер lab-kafka от прежней установки стенда (например, из /opt/kvs-env)
# занял бы и имя, и порт 9092.
other=$(docker ps -a --filter name='^lab-kafka$' \
        --format '{{.Label "com.docker.compose.project.working_dir"}}' 2>/dev/null || true)
if [[ -n "$other" && "$other" != "$ENV_DIR" ]]; then
    warn "контейнер lab-kafka остался от стенда в $other — удаляю его"
    docker rm -f lab-kafka >/dev/null
fi
busy=$(docker ps --filter publish=9092 --format '{{.Names}}' | grep -vx lab-kafka || true)
[[ -z "$busy" ]] || warn "порт 9092 занят контейнером $busy — остановите его: docker stop $busy"

# --- Итог ---------------------------------------------------------------------------
step "Готово. Проверьте стенд:"
cat <<EOF
  lab-stand up       # поднять Kafka и создать топик $TOPIC (первый раз скачает образ)
  lab-stand check    # должно закончиться строкой СТЕНД РАБОТАЕТ
  lab-stand down     # остановить до занятия

Последнюю строку вывода lab-stand check отправьте преподавателю.
EOF
