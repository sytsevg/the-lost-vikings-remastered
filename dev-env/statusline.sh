#!/usr/bin/env bash
# Status line: context / 5h session / 7d all-models.
# Input: JSON on stdin from Claude Code status line contract.

input=$(cat)
[[ -n "$STATUSLINE_DEBUG" ]] && printf '%s' "$input" > /tmp/statusline-input.json
printf '%s' "$input" > /tmp/statusline-last.json 2>/dev/null  # last input, so a session can read its own usage
now=${STATUSLINE_NOW:-$(date +%s)}

round_pct() {
  local raw=$1
  if [[ -z "$raw" || "$raw" == "null" ]]; then
    printf -- '--'
  else
    printf '%.0f' "$raw"
  fi
}

# Scale a raw token count: 847, 1.2k, 12k, 123k, 1.2M, 12M ...
fmt_tokens() {
  local t=$1
  if [[ -z "$t" || "$t" == "null" ]]; then
    printf -- '--'
    return
  fi
  awk -v t="$t" 'BEGIN {
    if (t < 1000)            { printf "%d", t }
    else if (t < 1000000)    { v = t / 1000;    u = "k" }
    else                     { v = t / 1000000; u = "M" }
    if (u != "") {
      if (v < 9.95) printf "%.1f%s", v, u
      else          printf "%.0f%s", v, u
    }
  }'
}

# Scale a token count to a whole unit, for the context-window denominator:
# 200000 -> 200k, 1000000 -> 1M.
fmt_tokens_max() {
  local t=$1
  if [[ -z "$t" || "$t" == "null" ]]; then
    printf -- '--'
    return
  fi
  awk -v t="$t" 'BEGIN {
    if (t < 1000)         { printf "%d", t }
    else if (t < 1000000) { printf "%.0fk", t / 1000 }
    else                  { printf "%.0fM", t / 1000000 }
  }'
}

# $1 = resets_at (unix epoch), $2 = "hm" or "dhm"
#
# At most two units, starting from the largest non-zero one: a leading zero
# unit is dropped, a trailing one is kept ([1h 0m] and [3d 0h] are reachable).
# Minutes are never shown alongside days. Under a minute, count in seconds.
fmt_reset() {
  local resets_at=$1 mode=$2
  [[ -z "$resets_at" || "$resets_at" == "null" ]] && return
  local remaining=$(( resets_at - now ))
  (( remaining <= 0 )) && return
  if (( remaining < 60 )); then
    printf ' [%ds]' "$remaining"
    return
  fi
  local total_min=$(( (remaining + 30) / 60 ))
  if [[ "$mode" == "hm" ]]; then
    local h=$(( total_min / 60 ))
    local m=$(( total_min % 60 ))
    if (( h >= 1 )); then
      printf ' [%dh %dm]' "$h" "$m"
    else
      printf ' [%dm]' "$m"
    fi
  else
    local d=$(( total_min / 1440 ))
    local h=$(( (total_min % 1440) / 60 ))
    local m=$(( total_min % 60 ))
    if (( d >= 1 )); then
      printf ' [%dd %dh]' "$d" "$h"
    elif (( h >= 1 )); then
      printf ' [%dh %dm]' "$h" "$m"
    else
      printf ' [%dm]' "$m"
    fi
  fi
}

# label + pct% + reset suffix, all in $color. When pct > 85 the percentage
# (number and % sign) is shown inverse video, black on $color's own background.
render() {
  local label=$1 pct=$2 color=$3 suffix=$4
  local on=$'\033['"$color"'m' off=$'\033[0m' bg=''
  if [[ "$pct" =~ ^[0-9]+$ ]] && (( pct > 85 )); then
    bg=$'\033[30;4'"${color: -1}"'m'
  fi
  printf '%s%s %s%s%%%s%s%s%s' \
    "$on" "$label" "$bg" "$pct" "$off" "$on" "$suffix" "$off"
}

# model segment: "Opus [xhigh]" all in green. Effort is absent (not pending)
# on models without the effort parameter, so no placeholder is shown then.
render_model() {
  local model=$1 effort=$2
  local green=$'\033[32m' off=$'\033[0m'
  [[ -z "$model" || "$model" == "null" ]] && model='--'
  if [[ -n "$effort" && "$effort" != "null" ]]; then
    printf '%s%s [%s]%s' "$green" "$model" "$effort" "$off"
  else
    printf '%s%s%s' "$green" "$model" "$off"
  fi
}

# ctx segment: "ctx 123k/1M" in cyan; inverse video (black on cyan) behind the
# used count when > 150k — an absolute degradation threshold, not a fraction of
# the window, so it is deliberately independent of the denominator.
render_ctx() {
  local tokens=$1 size=$2
  local cyan=$'\033[36m' off=$'\033[0m' bg=''
  if [[ -n "$tokens" && "$tokens" != "null" ]] && (( tokens > 150000 )); then
    bg=$'\033[30;46m'
  fi
  printf '%sctx %s%s%s%s/%s%s' \
    "$cyan" "$bg" "$(fmt_tokens "$tokens")" "$off" \
    "$cyan" "$(fmt_tokens_max "$size")" "$off"
}

model=$(jq -r  '.model.display_name // empty' <<<"$input")
effort=$(jq -r '.effort.level       // empty' <<<"$input")

ctx_tokens=$(jq -r '.context_window.total_input_tokens // 0'      <<<"$input")
ctx_size=$(jq -r   '.context_window.context_window_size // empty' <<<"$input")
five_pct=$(round_pct  "$(jq -r '.rate_limits.five_hour.used_percentage // empty' <<<"$input")")
seven_pct=$(round_pct "$(jq -r '.rate_limits.seven_day.used_percentage // empty' <<<"$input")")

five_reset=$(jq -r  '.rate_limits.five_hour.resets_at  // empty' <<<"$input")
seven_reset=$(jq -r '.rate_limits.seven_day.resets_at  // empty' <<<"$input")

printf '%s  %s  %s  %s' \
  "$(render_ctx "$ctx_tokens" "$ctx_size")" \
  "$(render 5h  "$five_pct"  33 "$(fmt_reset "$five_reset"  hm)")" \
  "$(render 7d  "$seven_pct" 35 "$(fmt_reset "$seven_reset" dhm)")" \
  "$(render_model "$model" "$effort")"