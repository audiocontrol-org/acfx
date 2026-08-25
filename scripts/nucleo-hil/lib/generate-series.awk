# generate-series.awk
#
# Deterministic, reproducible synthetic per-USB-frame packet-size-series
# generator for the T024 (US5) packet-capture evaluator's fixtures. This
# is TEST-FIXTURE TOOLING ONLY — it is never invoked by
# evaluate-packet-capture.sh or lib/packet-capture-core.awk, and produces
# no output the evaluator treats as anything other than an ordinary
# capture. No randomness: given the same arguments it always produces
# byte-identical output, so the committed fixtures/packet-capture/*.series
# files are reproducible from fixtures/packet-capture/generate-fixtures.sh.
#
# Usage (N lines, one packet size per line, written to stdout):
#   awk -v MODE=healthy -v RATE=48000 -v SUBSLOT=2 -v CHANNELS=2 -v N=1000 \
#       -f generate-series.awk
#   awk -v MODE=naive_alt -v RATE=44100 -v SUBSLOT=2 -v CHANNELS=2 -v N=1000 \
#       -f generate-series.awk
#   awk -v MODE=zlp_short -v RATE=48000 -v SUBSLOT=2 -v CHANNELS=2 -v N=200 \
#       -v ZLP_AT="20,75,150" -v SHORT_AT="40,110" -f generate-series.awk
#
# MODE=healthy    — replays the SAME rational phase accumulator the device
#                    uses (spec FR-002: acc += rate; frames = acc/1000;
#                    acc %= 1000) — the exact SOF-derived schedule.
# MODE=naive_alt  — deliberately WRONG: a strict 44/45 alternation
#                    (44,45,44,45,...), which averages 44.5 kHz instead of
#                    exactly RATE/1000. This is the precise anti-pattern
#                    FR-002 calls out by name. Every individual packet
#                    size is still "nominal" (44 or 45 frames), so this
#                    isolates a schedule/drift failure from a ZLP/short
#                    failure — used for the OUT-drift fixture (FR-003).
# MODE=zlp_short  — the healthy schedule with ZLPs (size forced to 0) and
#                    "short" packets (size reduced by one frame_bytes
#                    below the schedule's nominal size, so it stays
#                    nonzero but not in the nominal set) injected at the
#                    1-based USB-frame indices listed in ZLP_AT / SHORT_AT
#                    (comma-separated, may be empty).

BEGIN {
    if (CHANNELS == "") { CHANNELS = 2 }
    frame_bytes = CHANNELS * SUBSLOT
    acc = 0

    nzlp = split(ZLP_AT, zlp_idx, ",")
    for (i = 1; i <= nzlp; i++) {
        if (zlp_idx[i] != "") { is_zlp[zlp_idx[i] + 0] = 1 }
    }
    nshort = split(SHORT_AT, short_idx, ",")
    for (i = 1; i <= nshort; i++) {
        if (short_idx[i] != "") { is_short[short_idx[i] + 0] = 1 }
    }

    for (f = 1; f <= N; f++) {
        if (MODE == "healthy") {
            acc += RATE
            frames = int(acc / 1000)
            acc = acc % 1000
            size = frames * frame_bytes
        } else if (MODE == "naive_alt") {
            frames = (f % 2 == 1) ? 44 : 45
            size = frames * frame_bytes
        } else if (MODE == "zlp_short") {
            acc += RATE
            frames = int(acc / 1000)
            acc = acc % 1000
            size = frames * frame_bytes
            if (f in is_zlp) {
                size = 0
            } else if (f in is_short) {
                size = size - frame_bytes
            }
        } else {
            print "unknown MODE: " MODE > "/dev/stderr"
            exit 1
        }
        print size
    }
}
