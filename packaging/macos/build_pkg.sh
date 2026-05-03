#!/usr/bin/env bash
# Build a flat macOS product archive (.pkg) from CI-built plugin bundles.
# Uses pkgbuild + productbuild (same installer format as WhiteBox Packages; open this .pkg in Packages.app to inspect).
set -euo pipefail

VERSION="${VERSION:?set VERSION}"
ARTIFACTS_DIR="${ARTIFACTS_DIR:?set ARTIFACTS_DIR}"
OUTDIR="${OUTDIR:?set OUTDIR}"
PRODUCT_NAME="${PRODUCT_NAME:-BandGate}"
BUNDLE_ID="${BUNDLE_ID:-com.direktdsp.bandgate}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKDIR="$(mktemp -d)"
PKGS="$WORKDIR/pkgs"
RES="$WORKDIR/resources"
trap 'rm -rf "$WORKDIR"' EXIT

mkdir -p "$PKGS" "$RES" "$OUTDIR"
cp -f "$SCRIPT_DIR/../resources/EULA" "$RES/EULA"
cp -f "$SCRIPT_DIR/../resources/README" "$RES/README"

shopt -s nullglob
vst3s=( "$ARTIFACTS_DIR"/*.vst3 )
aus=( "$ARTIFACTS_DIR"/*.component )
claps=( "$ARTIFACTS_DIR"/*.clap )
apps=( "$ARTIFACTS_DIR"/*.app )

if [[ ${#vst3s[@]} -ne 1 ]]; then
  echo "Expected exactly one .vst3 in $ARTIFACTS_DIR, got: ${vst3s[*]-}"
  exit 1
fi
if [[ ${#aus[@]} -lt 1 ]]; then
  echo "Expected at least one .component in $ARTIFACTS_DIR, got: ${aus[*]-}"
  exit 1
fi
if [[ ${#claps[@]} -ne 1 ]]; then
  echo "Expected exactly one .clap in $ARTIFACTS_DIR, got: ${claps[*]-}"
  exit 1
fi
if [[ ${#apps[@]} -ne 1 ]]; then
  echo "Expected exactly one .app in $ARTIFACTS_DIR, got: ${apps[*]-}"
  exit 1
fi

stage_pkg() {
  local kind="$1" identifier="$2" outfile="$3"
  local root="$WORKDIR/root_${kind}"
  rm -rf "$root"
  mkdir -p "$root"

  case "$kind" in
    vst3)
      mkdir -p "$root/Library/Audio/Plug-Ins/VST3"
      cp -R "${vst3s[0]}" "$root/Library/Audio/Plug-Ins/VST3/"
      ;;
    au)
      mkdir -p "$root/Library/Audio/Plug-Ins/Components"
      for c in "${aus[@]}"; do
        cp -R "$c" "$root/Library/Audio/Plug-Ins/Components/"
      done
      ;;
    clap)
      mkdir -p "$root/Library/Audio/Plug-Ins/CLAP"
      cp -R "${claps[0]}" "$root/Library/Audio/Plug-Ins/CLAP/"
      ;;
    app)
      mkdir -p "$root/Applications"
      cp -R "${apps[0]}" "$root/Applications/"
      ;;
    *)
      echo "Unknown kind: $kind"
      exit 1
      ;;
  esac

  pkgbuild --root "$root" --identifier "$identifier" --version "$VERSION" --install-location / \
    "$PKGS/$outfile"
}

stage_pkg vst3 "${BUNDLE_ID}.vst3" "${PRODUCT_NAME}.vst3.pkg"
stage_pkg au "${BUNDLE_ID}.au" "${PRODUCT_NAME}.au.pkg"
stage_pkg clap "${BUNDLE_ID}.clap" "${PRODUCT_NAME}.clap.pkg"
stage_pkg app "${BUNDLE_ID}.app" "${PRODUCT_NAME}.app.pkg"

DIST_SRC="$SCRIPT_DIR/distribution.xml.template"
DIST_OUT="$WORKDIR/Distribution.xml"
sed -e "s/@VERSION@/${VERSION}/g" \
  -e "s/@PRODUCT_NAME@/${PRODUCT_NAME}/g" \
  -e "s/@BUNDLE_ID@/${BUNDLE_ID}/g" \
  "$DIST_SRC" >"$DIST_OUT"

OUT_PKG="$OUTDIR/DirektDSP-BandGate-${VERSION}-macOS.pkg"
SIGN_ARGS=()
if [[ -n "${MACOS_INSTALLER_SIGN_IDENTITY:-}" ]]; then
  SIGN_ARGS=(--sign "$MACOS_INSTALLER_SIGN_IDENTITY")
fi

productbuild "${SIGN_ARGS[@]}" \
  --distribution "$DIST_OUT" \
  --package-path "$PKGS" \
  --resources "$RES" \
  "$OUT_PKG"

echo "Built $OUT_PKG"
