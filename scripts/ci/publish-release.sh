#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail

slug="$1"
channel="$2"
linux_asset="dist/release/$slug-$channel-linux.zip"
windows_asset="dist/release/$slug-$channel-windows.zip"
test -s "$linux_asset"
test -s "$windows_asset"
test "$(git rev-parse HEAD)" = "$GITHUB_SHA"
python3 - "$GITHUB_SHA" "$channel" "$linux_asset" "$windows_asset" <<'PYZIP'
import sys
import zipfile
sha, channel, *paths = sys.argv[1:]
for platform, path in zip(("linux", "windows"), paths, strict=True):
    with zipfile.ZipFile(path) as archive:
        readme = archive.read("README.md").decode()
        if f"`{channel}` · {platform} x86 · [{sha}]" not in readme:
            raise SystemExit(f"Identidad de release inesperada: {path}")
PYZIP

latest=true
prerelease=false
if [[ "$GITHUB_REF_TYPE" == tag ]]; then
    tag="$GITHUB_REF_NAME"
    test "$tag" = "v$channel"
    title="$tag"
    test "$(git rev-parse "refs/tags/$tag^{commit}")" = "$GITHUB_SHA"
else
    remote_head="$(git ls-remote --exit-code origin "$GITHUB_REF" | cut -f1)"
    test "$remote_head" = "$GITHUB_SHA"
    case "$GITHUB_REF_NAME:$channel" in
        main:latest|master:latest) title=Latest ;;
        develop:develop) title=Develop; prerelease=true; latest=false ;;
        *) exit 1 ;;
    esac
    tag="channel/$channel"
fi

existing="$(gh api --paginate "repos/$GITHUB_REPOSITORY/releases" --jq '.[] | [.tag_name, .draft] | @tsv')"
if [[ "$GITHUB_REF_TYPE" == tag ]] && grep -Fxq -- "$tag"$'\tfalse' <<<"$existing"; then
    echo "La release versionada $tag ya existe." >&2
    exit 1
fi

if [[ "$GITHUB_REF_TYPE" == branch ]]; then
    remote_tag="$(git ls-remote --refs origin "refs/tags/$tag" | cut -f1)"
    git push "--force-with-lease=refs/tags/$tag:$remote_tag" origin "$GITHUB_SHA:refs/tags/$tag"
fi
if cut -f1 <<<"$existing" | grep -Fx -- "$tag" >/dev/null; then
    gh release delete "$tag" --yes
fi

notes="$(mktemp)"
trap 'rm -f "$notes"' EXIT
{
    printf 'Distribución %s.\n\n' "$title"
    printf -- '- UTC: %s\n' "$(date -u +'%Y-%m-%dT%H:%M:%SZ')"
    printf -- '- Commit: [%s](%s/%s/commit/%s)\n' "$GITHUB_SHA" "$GITHUB_SERVER_URL" "$GITHUB_REPOSITORY" "$GITHUB_SHA"
    printf -- '- CI: [#%s, intento %s](%s/%s/actions/runs/%s/attempts/%s)\n' \
        "$GITHUB_RUN_ID" "$GITHUB_RUN_ATTEMPT" "$GITHUB_SERVER_URL" "$GITHUB_REPOSITORY" "$GITHUB_RUN_ID" "$GITHUB_RUN_ATTEMPT"
} >"$notes"
if [[ "$GITHUB_REF_TYPE" == tag ]]; then
    printf '\n[Historial de cambios](%s/%s/blob/%s/CHANGELOG.md)\n' \
        "$GITHUB_SERVER_URL" "$GITHUB_REPOSITORY" "$GITHUB_SHA" >>"$notes"
fi
gh release create "$tag" "$linux_asset" "$windows_asset" --draft --verify-tag --title "$title" --notes-file "$notes"
gh release edit "$tag" --draft=false "--prerelease=$prerelease" "--latest=$latest"
