// 모드 전환 반복 시험 (WI-5.002, NFR-002). 새 의존성 없이 CoreGraphics로 해상도를 바꾼다.
//
// 사용법: mode-cycle <횟수> [간격 ms]
// 주 디스플레이의 모드 목록(HiDPI 포함)을 차례로 돌며 바꾸고, 바뀐 뒤 현재 모드가 요청과 같은지 확인한다.
// 끝나면 시작할 때의 모드로 되돌린다. 실패 수가 0이 아니면 종료 코드 1.
import CoreGraphics
import Foundation

struct ModeKey: Hashable {
    let width: Int
    let height: Int
    let pixelWidth: Int
    let pixelHeight: Int
}

func key(_ mode: CGDisplayMode) -> ModeKey {
    ModeKey(width: mode.width, height: mode.height, pixelWidth: mode.pixelWidth, pixelHeight: mode.pixelHeight)
}

func describe(_ mode: CGDisplayMode) -> String {
    "\(mode.width)x\(mode.height) (pixels \(mode.pixelWidth)x\(mode.pixelHeight))"
}

let arguments = CommandLine.arguments
guard arguments.count >= 2, let total = Int(arguments[1]), total > 0 else {
    FileHandle.standardError.write("usage: mode-cycle <count> [interval-ms]\n".data(using: .utf8)!)
    exit(2)
}
let intervalMs = arguments.count >= 3 ? max(Int(arguments[2]) ?? 1500, 200) : 1500

let display = CGMainDisplayID()
guard let original = CGDisplayCopyDisplayMode(display) else {
    FileHandle.standardError.write("mode-cycle: no current mode\n".data(using: .utf8)!)
    exit(1)
}
let options = [kCGDisplayShowDuplicateLowResolutionModes as String: true] as CFDictionary
let allModes = (CGDisplayCopyAllDisplayModes(display, options) as? [CGDisplayMode]) ?? []
var seen = Set<ModeKey>()
var modes: [CGDisplayMode] = []
for mode in allModes where mode.isUsableForDesktopGUI() && !seen.contains(key(mode)) {
    seen.insert(key(mode))
    modes.append(mode)
}
guard modes.count >= 2 else {
    FileHandle.standardError.write("mode-cycle: fewer than 2 modes\n".data(using: .utf8)!)
    exit(1)
}
print("start \(describe(original)), \(modes.count) modes, \(total) switches, interval \(intervalMs) ms")

func apply(_ mode: CGDisplayMode) -> CGError {
    var config: CGDisplayConfigRef?
    var error = CGBeginDisplayConfiguration(&config)
    guard error == .success else { return error }
    error = CGConfigureDisplayWithDisplayMode(config, display, mode, nil)
    guard error == .success else {
        CGCancelDisplayConfiguration(config)
        return error
    }
    return CGCompleteDisplayConfiguration(config, .forSession)
}

var failures = 0
for index in 0..<total {
    let target = modes[index % modes.count]
    let error = apply(target)
    usleep(useconds_t(intervalMs * 1000))
    let current = CGDisplayCopyDisplayMode(display).map(key)
    let ok = error == .success && current == key(target)
    if !ok {
        failures += 1
    }
    print("\(index + 1)/\(total) \(describe(target)) -> \(ok ? "ok" : "FAIL error=\(error.rawValue)")")
}
let restored = apply(original)
usleep(useconds_t(intervalMs * 1000))
print("restore \(describe(original)) -> \(restored == .success ? "ok" : "error \(restored.rawValue)")")
print("failures=\(failures)")
exit(failures == 0 && restored == .success ? 0 : 1)
