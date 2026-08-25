// acfx-loopback.swift — single-device full-duplex CoreAudio loopback tester.
//
// Opens ONE AUHAL unit on a named audio device with BOTH input and output
// enabled, so capture and playback share the SAME device clock (one IOProc /
// one clock domain) — the reliable analogue of a DAW aggregate, unlike two
// independent ffmpeg/sox streams. Generates a sine tone on output, captures the
// returning (effected) input, then measures the captured signal's dominant
// frequency (pitch-correctness) + RMS + a crude in-band SNR.
//
// Usage: acfx-loopback <device-name> <toneHz> <seconds> [sampleRate]
// Exit 0 = measured PASS (pitch within tolerance, signal present), 1 = FAIL.

import Foundation
import AudioToolbox
import CoreAudio

func die(_ msg: String) -> Never { FileHandle.standardError.write((msg + "\n").data(using: .utf8)!); exit(1) }

let args = CommandLine.arguments
let deviceName = args.count > 1 ? args[1] : "acfx Audio"
let toneHz     = args.count > 2 ? (Double(args[2]) ?? 440) : 440
let seconds    = args.count > 3 ? (Double(args[3]) ?? 5) : 5
let sampleRate = args.count > 4 ? (Double(args[4]) ?? 48000) : 48000

// ---- globals reachable from the @convention(c) callbacks ----
var gAU: AudioUnit? = nil
var gPhase: Double = 0
let gInc: Double = 2.0 * Double.pi * toneHz / sampleRate
var gCaptured = [Float]()
let kMaxFrames = 8192
let gScratch0 = UnsafeMutableRawPointer.allocate(byteCount: kMaxFrames*4, alignment: 16)
let gScratch1 = UnsafeMutableRawPointer.allocate(byteCount: kMaxFrames*4, alignment: 16)
let gABL = AudioBufferList.allocate(maximumBuffers: 2)

// ---- find device id by name ----
func findDevice(_ name: String) -> AudioDeviceID? {
    var size = UInt32(0)
    var addr = AudioObjectPropertyAddress(mSelector: kAudioHardwarePropertyDevices,
                                          mScope: kAudioObjectPropertyScopeGlobal,
                                          mElement: kAudioObjectPropertyElementMain)
    AudioObjectGetPropertyDataSize(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size)
    let count = Int(size) / MemoryLayout<AudioDeviceID>.size
    var ids = [AudioDeviceID](repeating: 0, count: count)
    AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &addr, 0, nil, &size, &ids)
    for id in ids {
        var nsize = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        var cfname: Unmanaged<CFString>? = nil
        var naddr = AudioObjectPropertyAddress(mSelector: kAudioObjectPropertyName,
                                               mScope: kAudioObjectPropertyScopeGlobal,
                                               mElement: kAudioObjectPropertyElementMain)
        if AudioObjectGetPropertyData(id, &naddr, 0, nil, &nsize, &cfname) == noErr,
           let n = cfname?.takeRetainedValue() as String?, n.contains(name) { return id }
    }
    return nil
}

guard let dev = findDevice(deviceName) else { die("device not found: \(deviceName)") }

var desc = AudioComponentDescription(componentType: kAudioUnitType_Output,
                                     componentSubType: kAudioUnitSubType_HALOutput,
                                     componentManufacturer: kAudioUnitManufacturer_Apple,
                                     componentFlags: 0, componentFlagsMask: 0)
guard let comp = AudioComponentFindNext(nil, &desc) else { die("no HALOutput component") }
var unit: AudioUnit? = nil
guard AudioComponentInstanceNew(comp, &unit) == noErr, let au = unit else { die("AudioComponentInstanceNew failed") }
gAU = au

func setProp(_ id: AudioUnitPropertyID, _ scope: AudioUnitScope, _ elem: AudioUnitElement, _ val: UnsafeRawPointer, _ size: UInt32) {
    let s = AudioUnitSetProperty(au, id, scope, elem, val, size)
    if s != noErr { die("AudioUnitSetProperty id=\(id) scope=\(scope) elem=\(elem) -> \(s)") }
}

var enable: UInt32 = 1
setProp(kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &enable, 4)
setProp(kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &enable, 4)
var devVar = dev
setProp(kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &devVar, UInt32(MemoryLayout<AudioDeviceID>.size))

var asbd = AudioStreamBasicDescription(mSampleRate: sampleRate,
    mFormatID: kAudioFormatLinearPCM,
    mFormatFlags: kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved,
    mBytesPerPacket: 4, mFramesPerPacket: 1, mBytesPerFrame: 4,
    mChannelsPerFrame: 2, mBitsPerChannel: 32, mReserved: 0)
setProp(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &asbd, UInt32(MemoryLayout<AudioStreamBasicDescription>.size))
setProp(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &asbd, UInt32(MemoryLayout<AudioStreamBasicDescription>.size))

// output render callback: fill both channels with the tone
let renderCB: AURenderCallback = { (_, _, _, _, inNumberFrames, ioData) -> OSStatus in
    guard let io = ioData else { return noErr }
    let abl = UnsafeMutableAudioBufferListPointer(io)
    let n = Int(inNumberFrames)
    for b in 0..<abl.count {
        guard let p = abl[b].mData?.assumingMemoryBound(to: Float.self) else { continue }
        var ph = gPhase
        for f in 0..<n { p[f] = Float(sin(ph) * 0.5); ph += gInc }
        if b == abl.count - 1 { gPhase = ph.truncatingRemainder(dividingBy: 2.0 * Double.pi) }
    }
    return noErr
}

// input callback: pull the captured (effected) input from the device
let inputCB: AURenderCallback = { (_, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, _) -> OSStatus in
    let n = Int(inNumberFrames)
    if n > kMaxFrames { return noErr }
    gABL[0] = AudioBuffer(mNumberChannels: 1, mDataByteSize: UInt32(n*4), mData: gScratch0)
    gABL[1] = AudioBuffer(mNumberChannels: 1, mDataByteSize: UInt32(n*4), mData: gScratch1)
    let st = AudioUnitRender(gAU!, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, gABL.unsafeMutablePointer)
    if st == noErr {
        let p = gScratch0.assumingMemoryBound(to: Float.self)
        for f in 0..<n { gCaptured.append(p[f]) }
    }
    return noErr
}

gCaptured.reserveCapacity(Int(sampleRate * seconds) + kMaxFrames)
var rcb = AURenderCallbackStruct(inputProc: renderCB, inputProcRefCon: nil)
setProp(kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &rcb, UInt32(MemoryLayout<AURenderCallbackStruct>.size))
var icb = AURenderCallbackStruct(inputProc: inputCB, inputProcRefCon: nil)
setProp(kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &icb, UInt32(MemoryLayout<AURenderCallbackStruct>.size))

guard AudioUnitInitialize(au) == noErr else { die("AudioUnitInitialize failed") }
guard AudioOutputUnitStart(au) == noErr else { die("AudioOutputUnitStart failed") }
FileHandle.standardError.write("streaming \(toneHz) Hz for \(seconds)s through \(deviceName)...\n".data(using: .utf8)!)
Thread.sleep(forTimeInterval: seconds)
AudioOutputUnitStop(au)
AudioUnitUninitialize(au)

// ---- analyze ----
let s = gCaptured
if s.count < Int(sampleRate) { die("captured too few samples (\(s.count)) — device likely not streaming") }
let start = Int(sampleRate * 0.5)
let x = Array(s[start...])
let n = Double(x.count)
let rms = sqrt(x.reduce(0.0){$0 + Double($1)*Double($1)} / n)
func goertzel(_ x: [Float], _ f: Double, _ sr: Double) -> Double {
    let w = 2.0 * Double.pi * f / sr, cw = 2.0 * cos(w)
    var s1 = 0.0, s2 = 0.0
    for v in x { let s0 = Double(v) + cw*s1 - s2; s2 = s1; s1 = s0 }
    return sqrt(s1*s1 + s2*s2 - cw*s1*s2)
}
// coarse peak
var bestF = 0.0, bestMag = -1.0, f = 50.0
while f < sampleRate/2 - 50 { let m = goertzel(x, f, sampleRate); if m > bestMag { bestMag = m; bestF = f }; f += 5.0 }

// least-squares residual at a candidate frequency (fits A*cos + B*sin, returns residual RMS)
func lsqResidual(_ freq: Double) -> Double {
    let wf = 2.0 * Double.pi * freq / sampleRate
    var sc = 0.0, ss = 0.0, cc = 0.0, sn2 = 0.0, cs = 0.0
    for (i, v) in x.enumerated() {
        let c = cos(wf*Double(i)), sn = sin(wf*Double(i)), d = Double(v)
        sc += d*c; ss += d*sn; cc += c*c; sn2 += sn*sn; cs += c*sn
    }
    let det = cc*sn2 - cs*cs
    let A = det != 0 ? (sc*sn2 - ss*cs)/det : 0
    let B = det != 0 ? (ss*cc - sc*cs)/det : 0
    var resSq = 0.0
    for (i, v) in x.enumerated() { let fit = A*cos(wf*Double(i)) + B*sin(wf*Double(i)); let r = Double(v)-fit; resSq += r*r }
    return sqrt(resSq / n)
}
// FINE frequency search: minimize the LSQ residual over bestF +/- 3 Hz at 0.01 Hz — this
// jointly nails the true fundamental and the true residual (a coarse freq error would
// otherwise leave a beat residual that masquerades as noise).
var rbest = bestF, minResid = Double.greatestFiniteMagnitude
var g = bestF - 3
while g <= bestF + 3 { let r = lsqResidual(g); if r < minResid { minResid = r; rbest = g }; g += 0.01 }
let residRms = minResid
let thdn_db = rms > 0 ? 20*log10(residRms / rms) : 0   // more negative = cleaner
let centsOff = 1200.0 * log2(rbest / toneHz)
print(String(format: "captured_samples=%d", s.count))
print(String(format: "rms=%.5f", rms))
print(String(format: "played_hz=%.1f detected_hz=%.1f cents_off=%.1f", toneHz, rbest, centsOff))
print(String(format: "thd_plus_n_db=%.1f  (residual after removing fundamental; more negative = cleaner)", thdn_db))
// Thresholds separate a HEALTHY round-trip from the original defect class:
//  - the async pitch-down bug produced errors of many semitones -> huge cents;
//  - "digital noise"/garbage produces a residual within a few dB of the signal.
// A healthy round-trip on THIS device sits near -3 cents / ~-25 dB THD+N, the
// latter being the known TASK-39 boundary-misalignment zero-fill floor (audio
// intact, per that item's live-verified disposition).
let signalPresent = rms > 0.002        // not silence
let pitchOk = abs(centsOff) < 25       // no audible pitch shift (bug was >> this)
let toneDominant = thdn_db < -15       // fundamental clearly dominates (excludes noise/garbage)
if signalPresent && pitchOk && toneDominant { print("RESULT=PASS"); exit(0) }
print("RESULT=FAIL signal_present=\(signalPresent) pitch_ok=\(pitchOk) tone_dominant=\(toneDominant)")
exit(1)
