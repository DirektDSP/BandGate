#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${MOONBASE_ACCOUNT_ID:-}" || -z "${MOONBASE_API_KEY:-}" || -z "${MOONBASE_PRODUCT_ID:-}" ]]; then
  echo "Missing required env vars: MOONBASE_ACCOUNT_ID, MOONBASE_API_KEY, MOONBASE_PRODUCT_ID"
  exit 1
fi

ARTIFACT_ROOT="${1:-moonbase-artifacts}"
VERSION_STR="${2:-}"
PUBLISH_IMMEDIATELY="${PUBLISH_IMMEDIATELY:-true}"

if [[ -z "$VERSION_STR" ]]; then
  echo "Usage: $0 <artifact-root> <version>"
  exit 1
fi

if [[ ! -d "$ARTIFACT_ROOT" ]]; then
  echo "Artifact directory not found: $ARTIFACT_ROOT"
  exit 1
fi

if ! [[ "$VERSION_STR" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
  echo "Version must be semver major.minor.patch, got: $VERSION_STR"
  exit 1
fi

MAJOR="${BASH_REMATCH[1]}"
MINOR="${BASH_REMATCH[2]}"
PATCH="${BASH_REMATCH[3]}"
BASE_URL="https://${MOONBASE_ACCOUNT_ID}.moonbase.sh/api"
UPLOAD_TMP_DIR="$(mktemp -d)"
DOWNLOADS_JSONL="${UPLOAD_TMP_DIR}/downloads.jsonl"
PACKAGE_STAGING_DIR="${UPLOAD_TMP_DIR}/packages"
touch "$DOWNLOADS_JSONL"
mkdir -p "$PACKAGE_STAGING_DIR"

cleanup() {
  rm -rf "$UPLOAD_TMP_DIR"
}
trap cleanup EXIT

detect_platform() {
  local input="$1"
  local lowered
  lowered="$(echo "$input" | tr '[:upper:]' '[:lower:]')"
  if [[ "$lowered" == *windows* || "$lowered" == *.exe || "$lowered" == *.msi ]]; then
    echo "Windows"
  elif [[ "$lowered" == *mac* || "$lowered" == *darwin* || "$lowered" == *.dmg || "$lowered" == *.pkg || "$lowered" == *.app || "$lowered" == *.component ]]; then
    echo "Mac"
  elif [[ "$lowered" == *linux* || "$lowered" == *.appimage || "$lowered" == *.deb || "$lowered" == *.rpm || "$lowered" == *.so ]]; then
    echo "Linux"
  else
    echo "Universal"
  fi
}

prepare_and_upload() {
  local source_path="$1"
  local display_name="$2"
  local platform="$3"
  local upload_path="$source_path"

  if [[ -d "$source_path" ]]; then
    upload_path="${UPLOAD_TMP_DIR}/${display_name}.zip"
    (cd "$(dirname "$source_path")" && zip -r -q "$upload_path" "$(basename "$source_path")")
    display_name="${display_name}.zip"
  fi

  local content_type
  content_type="application/octet-stream"

  local prep_response
  prep_response="$(curl --fail-with-body -sS -X POST \
    "${BASE_URL}/downloads/prepare?contentType=application%2Foctet-stream" \
    -H "Api-Key: ${MOONBASE_API_KEY}")"

  local upload_url
  upload_url="$(python3 -c 'import json,sys;print(json.load(sys.stdin)["url"])' <<<"$prep_response")"
  local key
  key="$(python3 -c 'import json,sys;print(json.load(sys.stdin)["key"])' <<<"$prep_response")"

  curl --fail-with-body -sS -X PUT \
    -H "Content-Type: ${content_type}" \
    --upload-file "$upload_path" \
    "$upload_url" >/dev/null

  python3 - "$display_name" "$platform" "$key" >>"$DOWNLOADS_JSONL" <<'PY'
import json, sys
name, platform, key = sys.argv[1], sys.argv[2], sys.argv[3]
print(json.dumps({"name": name, "platform": platform, "key": key}))
PY

  echo "Uploaded: $display_name ($platform)"
}

while IFS= read -r -d '' path; do
  artifact_dir_name="$(basename "$path")"
  platform="$(detect_platform "$artifact_dir_name")"
  platform_staging_dir="${PACKAGE_STAGING_DIR}/${platform}"
  mkdir -p "$platform_staging_dir"

  # Copy all files from each artifact bundle into a per-platform staging area.
  # Preserve symlinks/directories so plugin bundles remain intact before zipping.
  cp -a "${path}/." "$platform_staging_dir/"
done < <(find "$ARTIFACT_ROOT" -mindepth 1 -maxdepth 1 -type d -print0)

while IFS= read -r -d '' platform_dir; do
  platform="$(basename "$platform_dir")"
  package_name="DirektDSP-BandGate-${VERSION_STR}-${platform}.zip"
  package_path="${UPLOAD_TMP_DIR}/${package_name}"
  (cd "$platform_dir" && zip -r -q "$package_path" .)
  prepare_and_upload "$package_path" "$package_name" "$platform"
done < <(find "$PACKAGE_STAGING_DIR" -mindepth 1 -maxdepth 1 -type d -print0)

if [[ ! -s "$DOWNLOADS_JSONL" ]]; then
  echo "No artifacts found to upload under $ARTIFACT_ROOT"
  exit 1
fi

release_payload="$(
python3 - "$MAJOR" "$MINOR" "$PATCH" "$DOWNLOADS_JSONL" <<'PY'
import json, sys
major, minor, patch, jsonl = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
downloads = []
with open(jsonl, "r", encoding="utf-8") as fh:
    for line in fh:
        line = line.strip()
        if line:
            downloads.append(json.loads(line))
payload = {
    "version": {"major": major, "minor": minor, "patch": patch},
    "downloads": downloads
}
print(json.dumps(payload))
PY
)"

curl --fail-with-body -sS -X POST \
  "${BASE_URL}/products/${MOONBASE_PRODUCT_ID}/releases/new?publishImmediately=${PUBLISH_IMMEDIATELY}" \
  -H "Api-Key: ${MOONBASE_API_KEY}" \
  -H "Content-Type: application/json" \
  -d "$release_payload" >/dev/null

echo "Moonbase release created for version ${VERSION_STR}"
