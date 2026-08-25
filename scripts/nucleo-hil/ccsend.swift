import CoreMIDI
import Foundation

// usage: ccsend <cc> <value> [channel=0]   (sends to the first destination whose name contains "acfx")
let args = CommandLine.arguments
guard args.count >= 3, let cc = UInt8(args[1]), let val = UInt8(args[2]) else {
    FileHandle.standardError.write("usage: ccsend <cc> <value> [channel]\n".data(using:.utf8)!); exit(2)
}
let ch = args.count >= 4 ? (UInt8(args[3]) ?? 0) : 0

var client = MIDIClientRef()
MIDIClientCreate("ccsend" as CFString, nil, nil, &client)
var outPort = MIDIPortRef()
MIDIOutputPortCreate(client, "out" as CFString, &outPort)

let n = MIDIGetNumberOfDestinations()
var dest: MIDIEndpointRef = 0
var found = ""
for i in 0..<n {
    let d = MIDIGetDestination(i)
    var nameRef: Unmanaged<CFString>?
    MIDIObjectGetStringProperty(d, kMIDIPropertyDisplayName, &nameRef)
    let name = nameRef?.takeRetainedValue() as String? ?? ""
    if name.lowercased().contains("acfx") { dest = d; found = name; break }
}
guard dest != 0 else {
    var names = [String]()
    for i in 0..<n { var r:Unmanaged<CFString>?; MIDIObjectGetStringProperty(MIDIGetDestination(i),kMIDIPropertyDisplayName,&r); names.append((r?.takeRetainedValue() as String?) ?? "?") }
    FileHandle.standardError.write("no acfx MIDI destination. found: \(names)\n".data(using:.utf8)!); exit(1)
}
var packet = MIDIPacketList()
let cur = MIDIPacketListInit(&packet)
let bytes: [UInt8] = [0xB0 | (ch & 0x0F), cc & 0x7F, val & 0x7F]
_ = MIDIPacketListAdd(&packet, 1024, cur, 0, bytes.count, bytes)
MIDISend(outPort, dest, &packet)
print("sent CC \(cc)=\(val) ch\(ch) -> \(found)")
