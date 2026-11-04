#!/bin/bash
# Проверка перед публикацией: в файлах и истории коммитов не должно быть личных данных.
# Запускается в CI на каждой сборке; можно запустить и локально: bash scripts/privacy-check.sh
set -u
cd "$(dirname "$0")/.."
fail=0

# что считается утечкой
SECRET_RE='privaterelay\.appleid\.com|claude\.ai/(code|chat|share)|session_[0-9A-Za-z]{20,}|ghp_[0-9A-Za-z]{30,}|github_pat_[0-9A-Za-z_]{30,}|sk-ant-[0-9A-Za-z_-]{20,}|sk-[0-9A-Za-z]{32,}|AKIA[0-9A-Z]{16}|xox[abposr]-[0-9A-Za-z-]{10,}|-----BEGIN [A-Z ]*PRIVATE KEY-----'
# почты, которые можно показывать
ALLOWED_EMAIL_RE='^(noreply@anthropic\.com|noreply@github\.com|[A-Za-z0-9._+-]+@users\.noreply\.github\.com)$'

echo "== files"
if git grep -nIE "$SECRET_RE" -- . ':!scripts/privacy-check.sh'; then
    echo "!! secrets or private links found in files"; fail=1
fi
# любые адреса почты в файлах, кроме разрешённых
while read -r mail; do
    [ -z "$mail" ] && continue
    if ! [[ "$mail" =~ $ALLOWED_EMAIL_RE ]]; then echo "!! email in files: $mail"; fail=1; fi
done < <(git grep -hoIE '[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}' -- . ':!scripts/privacy-check.sh' | sort -u)

echo "== commits"
# авторы и коммиттеры — только анонимные адреса
while read -r mail; do
    if ! [[ "$mail" =~ $ALLOWED_EMAIL_RE ]]; then echo "!! commit email: $mail"; fail=1; fi
done < <(git log HEAD --tags --format='%ae%n%ce' | sort -u)
# тексты коммитов
if git log HEAD --tags --format='%H %B' | grep -nE "$SECRET_RE"; then
    echo "!! secrets or private links found in commit messages"; fail=1
fi

[ $fail -eq 0 ] && echo "privacy check: OK"
exit $fail
