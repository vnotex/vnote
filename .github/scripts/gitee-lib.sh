# shellcheck shell=bash
#
# Shared helpers for .github/workflows/gitee-mirror.yml.
#
# Source it (do NOT execute it) from a `run:` step that has already run
# `set -euo pipefail`:
#
#     . .github/scripts/gitee-lib.sh
#
# Requires in the environment: GITEE_USERNAME, GITEE_TOKEN, GITEE_OWNER,
# GITEE_REPO, RUNNER_TEMP.
#
# Provides:
#   $REMOTE                              - credential-free Gitee git remote
#   push_tag_idempotent <tag> <sha>
#   api <METHOD> <path> [curl args...]   -> $API_CODE, $API_BODY
#   api_is_2xx / api_require_2xx <what>
#   gitee_find_release_id <tag>          -> $FOUND_RELEASE_ID, rc 0/1/2
#   gitee_create_release <tag> <name> <body> <target_commitish>
#   gitee_patch_release <id> <tag> <name> <body>

GITEE_API="https://gitee.com/api/v5"

# -----------------------------------------------------------------------------
# git auth: GIT_ASKPASS, never credentials embedded in a remote URL.
# -----------------------------------------------------------------------------
cat > "$RUNNER_TEMP/askpass.sh" <<'ASKPASS'
#!/usr/bin/env bash
case "$1" in
  Username*) printf '%s' "$GITEE_USERNAME" ;;
  Password*) printf '%s' "$GITEE_TOKEN" ;;
esac
ASKPASS
chmod +x "$RUNNER_TEMP/askpass.sh"
export GIT_ASKPASS="$RUNNER_TEMP/askpass.sh"
export GIT_TERMINAL_PROMPT=0
REMOTE="https://gitee.com/${GITEE_OWNER}/${GITEE_REPO}.git"

# -----------------------------------------------------------------------------
# API auth mode.
#
# Gitee's OpenAPI v5 declares `access_token` as a query/form parameter, but it
# also accepts an `Authorization: token <pat>` header. The header keeps the
# token out of every URL. Run 30447287744 confirmed the header works for this
# token, so `header` is the expected mode; the query fallback is retained for
# the case where a future token or endpoint rejects it.
# -----------------------------------------------------------------------------
if [ -r "$RUNNER_TEMP/gitee-auth-mode" ]; then
  GITEE_AUTH_MODE="$(cat "$RUNNER_TEMP/gitee-auth-mode")"
else
  if _probe_code="$(curl -sS -o /dev/null -w '%{http_code}' \
      --retry 3 --retry-delay 5 \
      -H "Authorization: token ${GITEE_TOKEN}" \
      "${GITEE_API}/user")"; then
    :
  else
    _probe_code=000
  fi
  if [ "$_probe_code" = "200" ]; then
    GITEE_AUTH_MODE=header
  else
    GITEE_AUTH_MODE=query
    echo "WARNING: Authorization header probe returned HTTP ${_probe_code};" \
         "falling back to the access_token query parameter"
  fi
  unset _probe_code
  printf '%s' "$GITEE_AUTH_MODE" > "$RUNNER_TEMP/gitee-auth-mode"
  echo "gitee api auth mode: $GITEE_AUTH_MODE"
fi

# -----------------------------------------------------------------------------
# Idempotent release-tag push.
#
# Job A (refs/tags/v*) and Job B (the one release tag) sit in different
# concurrency groups, so they can run at the same time and both try to create
# the SAME immutable tag. Both intend the identical object, but a simultaneous
# receive-pack can fail one side's ref lock. Treat that as success when the
# remote already holds the expected commit. Never force-update a release tag.
# -----------------------------------------------------------------------------
push_tag_idempotent() {  # $1 = tag name, $2 = expected commit sha
  local tag="$1" expected="$2" remote_sha
  if git push "$REMOTE" "refs/tags/${tag}:refs/tags/${tag}"; then
    return 0
  fi
  # Peeled ref first (annotated tags), then the tag ref itself (lightweight).
  remote_sha="$(git ls-remote "$REMOTE" "refs/tags/${tag}^{}" | cut -f1)"
  if [ -z "$remote_sha" ]; then
    remote_sha="$(git ls-remote "$REMOTE" "refs/tags/${tag}" | cut -f1)"
  fi
  if [ "$remote_sha" = "$expected" ]; then
    echo "tag $tag already on Gitee at $expected (concurrent push) - ok"
    return 0
  fi
  echo "tag $tag push failed; remote=${remote_sha:-<none>} expected=$expected" >&2
  return 1
}

# -----------------------------------------------------------------------------
# API helper.
#
# Status and body are captured separately on purpose: --fail / --fail-with-body
# conflate a transport failure with a legitimate 404, and 404 is a normal answer
# to "does this release exist yet?".
#
# There is deliberately NO --retry here. curl retries transport failures and
# 408/429/5xx regardless of HTTP method, so it would silently re-send a
# POST /releases or an attachment upload that Gitee had already accepted. Add
# `--retry 3 --retry-delay 5` at GET call sites only.
# -----------------------------------------------------------------------------
api() {  # api <METHOD> <path> [curl args...]  -> $API_CODE, $API_BODY
  local method="$1" path="$2"
  shift 2
  local url="${GITEE_API}${path}"
  local -a hdr=()
  if [ "$GITEE_AUTH_MODE" = "header" ]; then
    hdr=(-H "Authorization: token ${GITEE_TOKEN}")
  else
    # Gitee declares access_token as a query parameter for GET/DELETE but as a
    # formData parameter for the multipart POST/PATCH endpoints. Honour that
    # split instead of assuming the query string is accepted everywhere: detect
    # a multipart request from the caller's own curl arguments.
    local a multipart=0
    for a in "$@"; do
      case "$a" in
        -F|--form|--form-string) multipart=1; break ;;
      esac
    done
    if [ "$multipart" = "1" ]; then
      hdr=(--form-string "access_token=${GITEE_TOKEN}")
    else
      # GET / DELETE, and the JSON create/patch fallback (which also carries the
      # token in its body).
      case "$url" in
        *\?*) url="${url}&access_token=${GITEE_TOKEN}" ;;
        *)    url="${url}?access_token=${GITEE_TOKEN}" ;;
      esac
    fi
  fi
  local code
  : > "$RUNNER_TEMP/api_body.json"
  if code="$(curl -sS -o "$RUNNER_TEMP/api_body.json" -w '%{http_code}' \
      -X "$method" ${hdr[@]+"${hdr[@]}"} "$@" "$url" < /dev/null)"; then
    API_CODE="$code"
  else
    # Transport failure. Never assume the request did not reach Gitee: the
    # caller must re-query before re-sending a mutating call.
    API_CODE=000
  fi
  API_BODY="$(cat "$RUNNER_TEMP/api_body.json" 2>/dev/null || true)"
}

api_is_2xx() {
  case "${API_CODE:-000}" in
    2??) return 0 ;;
    *)   return 1 ;;
  esac
}

api_require_2xx() {  # $1 = human description of the call
  if api_is_2xx; then
    return 0
  fi
  echo "gitee api call failed: $1 (HTTP ${API_CODE:-000})" >&2
  printf '%s\n' "${API_BODY:-}" >&2
  return 1
}

# -----------------------------------------------------------------------------
# Releases.
#
# --form-string (not -F) for the text fields: -F treats a value starting with
# `@` or `<` as a file path, and changelog text legitimately contains both.
#
# Swagger is self-contradictory about the content type (`consumes:
# application/json` while listing the parameters as `formData`). Run
# 30447448351 settled it: a multipart PATCH was parsed and reached Gitee's
# business validation (it answered `{"message":"发行版不存在"}`), so multipart
# IS accepted. The JSON retry is kept only for a 415, the unambiguous
# "unsupported media type" signal. A 400 is a semantic rejection - retrying it
# as JSON would only hide the real error message.
# -----------------------------------------------------------------------------
_gitee_json_auth_arg() {  # echoes nothing in header mode
  if [ "$GITEE_AUTH_MODE" != "header" ]; then
    printf '%s' "$GITEE_TOKEN"
  fi
}

# Gitee answers GET /releases/tags/{tag} with HTTP 200 and a literal `null`
# body when the release does not exist - it never returns 404 for this
# endpoint. "Found" therefore means: 2xx AND a JSON object carrying an id.
#
# Sets the global FOUND_RELEASE_ID (deliberately a global, not stdout: a
# command substitution would run `api` in a subshell and lose $API_CODE /
# $API_BODY, which the caller needs for diagnostics).
#
# Returns 0 = found, 1 = definitely absent, 2 = API error.
gitee_find_release_id() {  # $1 = tag
  local tag="$1"
  FOUND_RELEASE_ID=""
  api GET "/repos/${GITEE_OWNER}/${GITEE_REPO}/releases/tags/${tag}" \
    --retry 3 --retry-delay 5
  if ! api_is_2xx; then
    if [ "${API_CODE:-000}" = "404" ]; then
      return 1
    fi
    return 2
  fi
  # A parse failure must NOT be reported as "absent": that would drive the
  # caller into an unnecessary create attempt. Surface it as an API error.
  if ! FOUND_RELEASE_ID="$(printf '%s' "$API_BODY" \
      | jq -r 'if type == "object" then (.id // empty) else empty end')"; then
    echo "could not parse the release lookup response for $tag" >&2
    FOUND_RELEASE_ID=""
    return 2
  fi
  if [ -z "$FOUND_RELEASE_ID" ] || [ "$FOUND_RELEASE_ID" = "null" ]; then
    FOUND_RELEASE_ID=""
    return 1
  fi
  return 0
}

gitee_create_release() {  # $1 tag, $2 name, $3 body, $4 target_commitish
  local tag="$1" name="$2" body="$3" target="$4" json
  api POST "/repos/${GITEE_OWNER}/${GITEE_REPO}/releases" \
    --form-string "tag_name=${tag}" \
    --form-string "name=${name}" \
    --form-string "body=${body}" \
    --form-string "target_commitish=${target}" \
    --form-string "prerelease=false"
  if [ "${API_CODE:-000}" = "415" ]; then
    echo "multipart create rejected (HTTP 415); retrying with a JSON body"
    json="$(jq -n --arg t "$tag" --arg n "$name" --arg b "$body" \
                  --arg c "$target" --arg k "$(_gitee_json_auth_arg)" \
      '{tag_name:$t, name:$n, body:$b, target_commitish:$c, prerelease:false}
       + (if $k == "" then {} else {access_token:$k} end)')"
    api POST "/repos/${GITEE_OWNER}/${GITEE_REPO}/releases" \
      -H 'Content-Type: application/json' --data-binary "$json"
  fi
}

gitee_patch_release() {  # $1 id, $2 tag, $3 name, $4 body
  # tag_name / name / body are ALL required by Gitee on PATCH; resend every one.
  local id="$1" tag="$2" name="$3" body="$4" json
  api PATCH "/repos/${GITEE_OWNER}/${GITEE_REPO}/releases/${id}" \
    --form-string "tag_name=${tag}" \
    --form-string "name=${name}" \
    --form-string "body=${body}"
  if [ "${API_CODE:-000}" = "415" ]; then
    echo "multipart update rejected (HTTP 415); retrying with a JSON body"
    json="$(jq -n --arg t "$tag" --arg n "$name" --arg b "$body" \
                  --arg k "$(_gitee_json_auth_arg)" \
      '{tag_name:$t, name:$n, body:$b}
       + (if $k == "" then {} else {access_token:$k} end)')"
    api PATCH "/repos/${GITEE_OWNER}/${GITEE_REPO}/releases/${id}" \
      -H 'Content-Type: application/json' --data-binary "$json"
  fi
}

# -----------------------------------------------------------------------------
# Attachments are NOT handled here.
#
# Gitee's POST .../attach_files sustained well under 50 KB/s from a
# GitHub-hosted runner (run 30447854128 spent 55 minutes without finishing the
# first 164 MiB asset, against a ~400 MiB payload), so mirroring binaries is not
# viable inside a workflow job. The maintainer attaches the artifacts to the
# Gitee release by hand; the workflow owns the release object and its notes
# only, and never touches attachments.
# -----------------------------------------------------------------------------
