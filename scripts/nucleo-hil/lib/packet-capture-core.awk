# packet-capture-core.awk
#
# Pure computational core of the T024 (US5) dual-direction packet-capture
# evaluator (FR-002, FR-003, FR-013). This is the "PURE function/module"
# half of the evaluator called out by the task brief: given ONLY a
# per-USB-frame packet-size series, it infers the selected rate/format and
# reports the full metric set, with no board, no serial device, and no
# side effects beyond stdout. evaluate-packet-capture.sh (the CLI wrapper)
# invokes this once per direction and never duplicates its logic.
#
# Input (stdin or a file argument, one direction at a time): one line per
# observed USB (SOF) frame, IN FRAME ORDER, each line a single
# non-negative integer -- the isochronous packet size in bytes observed on
# the wire for that USB frame (0 = zero-length packet / ZLP). This is the
# T002 capture backend's per-direction output contract: a raw,
# frame-ordered, gap-free packet-size series, one file per direction.
#
# Invocation:
#   awk -v CHANNELS=2 -f packet-capture-core.awk <series-file>
#
# CHANNELS defaults to 2 (stereo) if unset -- this project only ever
# advertises stereo PCM (spec FR-005) -- but is passed explicitly rather
# than hardcoded so this stays a pure function of its declared inputs.
#
# Output: a flat key=value report on stdout, one key per line (mirrors
# adapters/nucleo/support/diagnostic-serializer.h's wire-format
# convention, reused here for its wrapper-parseable simplicity). OR, on a
# fatal input problem, EXACTLY one line "error=<description>" and nothing
# else. No fallback, no mock data: malformed or unusable input is a hard,
# described error, never silently coerced to zero or dropped.
#
# Reported keys (contract: specs/synchronous-usb-audio-transport/
# contracts/usb-transport-contract.md "Packet-capture verification
# contract (FR-013)"):
#   rate_hz, subslot_bytes, bit_depth, usb_frames, audio_frames,
#   effective_fps, zlp_count, non_nominal_count, schedule_expected_total,
#   schedule_mismatch_count, schedule_first_mismatch_frame,
#   hist_<size>=<count> (one line per distinct packet size, ascending)
#
# The schedule check replays the SAME rational phase accumulator the
# device uses (spec FR-002: `acc += rate_hz; frames = acc / 1000;
# acc %= 1000`) and compares it against the ACTUAL per-frame audio-frame
# count at every single USB frame -- not merely the aggregate total -- so
# a compensating drift (e.g. a naive 44/45 alternation that averages
# 44.5 kHz instead of exactly 44.1 kHz) cannot hide behind a coincidentally
# correct running total.

BEGIN {
    if (CHANNELS == "") { CHANNELS = 2 }
    fatal = 0
    n = 0
    nd = 0
}

{
    if (fatal) { next }
    line = $0
    gsub(/\r$/, "", line)
    if (line !~ /^[0-9]+$/) {
        printf "error=malformed input at line %d: \"%s\" is not a non-negative integer packet size\n", NR, line
        fatal = 1
        next
    }
    n++
    v = line + 0
    val[n] = v
    if (!(v in hist)) {
        nd++
        sizes[nd] = v
    }
    hist[v]++
}

END {
    if (fatal) { exit }
    if (n == 0) {
        print "error=empty packet-size series: no USB frames observed"
        exit
    }

    # Known nominal per-packet byte sizes for this project's four
    # supported (rate, subslot) combinations, at CHANNELS-channel stereo
    # PCM (spec FR-004/FR-005):
    #   48000 Hz / 2-byte subslot (16-bit): every USB frame carries
    #     exactly 48 audio frames -> ONE nominal size.
    #   48000 Hz / 3-byte subslot (24-bit, packed): ditto.
    #   44100 Hz / 2-byte subslot (16-bit): the 44/45 rational-accumulator
    #     schedule -> TWO nominal sizes.
    #   44100 Hz / 3-byte subslot (24-bit, packed): ditto.
    ncand = 0
    ncand++; cand_rate[ncand] = 48000; cand_subslot[ncand] = 2; cand_lo[ncand] = 48; cand_hi[ncand] = 48
    ncand++; cand_rate[ncand] = 48000; cand_subslot[ncand] = 3; cand_lo[ncand] = 48; cand_hi[ncand] = 48
    ncand++; cand_rate[ncand] = 44100; cand_subslot[ncand] = 2; cand_lo[ncand] = 44; cand_hi[ncand] = 45
    ncand++; cand_rate[ncand] = 44100; cand_subslot[ncand] = 3; cand_lo[ncand] = 44; cand_hi[ncand] = 45

    best = 0
    best_score = -1
    for (c = 1; c <= ncand; c++) {
        fb = CHANNELS * cand_subslot[c]
        size_lo = cand_lo[c] * fb
        size_hi = cand_hi[c] * fb
        score = hist[size_lo] + 0
        if (size_hi != size_lo) { score += hist[size_hi] + 0 }
        if (score > best_score) { best_score = score; best = c }
    }

    if (best == 0 || best_score == 0) {
        print "error=cannot infer sample rate / subslot size: no packet size in the series matches any known nominal UAC2 stereo packet size (192, 288, 176, 180, 264, or 270 bytes)"
        exit
    }

    rate = cand_rate[best]
    subslot = cand_subslot[best]
    bitdepth = subslot * 8
    frame_bytes = CHANNELS * subslot
    nom_lo = cand_lo[best] * frame_bytes
    nom_hi = cand_hi[best] * frame_bytes

    zlp = 0
    nonnominal = 0
    audio_total = 0
    mismatch = 0
    first_mismatch = 0
    acc = 0
    expected_total = 0

    for (i = 1; i <= n; i++) {
        s = val[i]
        if (s == 0) {
            zlp++
            actual_frames = 0
        } else if (s == nom_lo || s == nom_hi) {
            actual_frames = s / frame_bytes
        } else {
            nonnominal++
            actual_frames = int(s / frame_bytes)
        }
        audio_total += actual_frames

        acc += rate
        expected_frames = int(acc / 1000)
        acc = acc % 1000
        expected_total += expected_frames

        if (actual_frames != expected_frames) {
            mismatch++
            if (first_mismatch == 0) { first_mismatch = i }
        }
    }

    printf "rate_hz=%d\n", rate
    printf "subslot_bytes=%d\n", subslot
    printf "bit_depth=%d\n", bitdepth
    printf "usb_frames=%d\n", n
    printf "audio_frames=%d\n", audio_total
    printf "effective_fps=%.6f\n", (audio_total * 1000.0) / n
    printf "zlp_count=%d\n", zlp
    printf "non_nominal_count=%d\n", nonnominal
    printf "schedule_expected_total=%d\n", expected_total
    printf "schedule_mismatch_count=%d\n", mismatch
    printf "schedule_first_mismatch_frame=%d\n", first_mismatch

    # Manual ascending sort (portable across awk implementations -- no
    # gawk-only asorti/asort; this repo's macOS dev host ships BWK awk,
    # not gawk). nd is small (a handful of distinct packet sizes), so an
    # O(nd^2) bubble sort is more than adequate.
    for (a = 1; a <= nd; a++) {
        for (b = a + 1; b <= nd; b++) {
            if (sizes[b] < sizes[a]) {
                tmp = sizes[a]; sizes[a] = sizes[b]; sizes[b] = tmp
            }
        }
    }
    for (k = 1; k <= nd; k++) {
        printf "hist_%d=%d\n", sizes[k], hist[sizes[k]]
    }
}
