#!/usr/bin/env bash
# 把编译产物作为 asset 上传到 Gitea Release。
# 用法:
#   GITEA_TOKEN=xxx PKG_VERSION=<tag> ./scripts/upload_release.sh <asset-file> [asset-name]
# 环境变量:
#   GITEA_URL   默认 http://10.168.1.242:3000
#   GITEA_REPO  默认 srdev/stock_gui
#   PKG_VERSION release tag (通常用短 sha)
set -euo pipefail

BASE="${GITEA_URL:-http://10.168.1.242:3000}"
repo="${GITEA_REPO:-srdev/stock_gui}"
token="${GITEA_TOKEN:?set GITEA_TOKEN}"
tag="${PKG_VERSION:?set PKG_VERSION}"
asset="${1:?usage: upload_release.sh <asset-file> [asset-name]}"
name="${2:-$(basename "$asset")}"

[ -f "$asset" ] || { echo "missing asset: $asset" >&2; exit 1; }
if printf '%s' "$tag" | grep -Eq '^[0-9a-f]{40}$'; then tag="${tag:0:7}"; fi

api="${BASE}/api/v1/repos/${repo}"
if printf '%s' "$token" | grep -q ':'; then AUTH=(-u "$token"); else AUTH=(-u "x-access-token:${token}"); fi

release_id_for () {
  curl -s "${AUTH[@]}" "${api}/releases/tags/${1}" 2>/dev/null | grep -o '"id":[0-9]*' | head -1 | grep -o '[0-9]*' || true
}

rid="$(release_id_for "$tag")"
if [ -n "$rid" ]; then
  curl -s "${AUTH[@]}" -X DELETE "${api}/releases/${rid}" >/dev/null
  echo "replaced existing release ${tag}"
fi

sha="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
built="$(date +%Y-%m-%dT%H:%M:%S%z)"
payload="$(printf '{"tag_name":"%s","name":"%s %s %s","body":"commit %s | built %s | asset %s","target_commitish":"master"}' \
  "$tag" "stock_gui" "$sha" "$built" "$sha" "$built" "$name")"

new="$(curl -s "${AUTH[@]}" -H 'Content-Type: application/json' -X POST "${api}/releases" --data-binary "$payload" || true)"
rid="$(printf '%s' "$new" | grep -o '"id":[0-9]*' | head -1 | grep -o '[0-9]*')"
if [ -z "$rid" ]; then echo "failed to create release"; exit 1; fi
echo "created release ${tag} (id ${rid})"

code="$(curl -s -o /dev/null -w '%{http_code}' "${AUTH[@]}" -H 'Content-Type: application/octet-stream' --data-binary "@${asset}" "${api}/releases/${rid}/assets?name=${name}")"
echo "uploaded ${name} -> HTTP ${code}"
[ "$code" = "201" ] || { echo "upload failed"; exit 1; }

echo
echo "Download (需登录, 私有仓库):"
echo "  ${BASE}/${repo}/releases/download/${tag}/${name}"
