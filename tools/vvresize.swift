// 창 크기 자동 전환 에이전트 (WI-6.001, REQ-043). 로그인 세션에서 LaunchAgent로 돈다.
//
// 드라이버는 호스트 창 크기가 바뀌면 그 크기의 모드를 목록에 넣고 IOGraphics에 연결 변경을 알린다. 하지만 WindowServer는
// 디스플레이마다 저장된 모드를 복원할 뿐 새 모드로 바꾸지 않는다(0.0.44 실측). 그래서 이 에이전트가 드라이버가 알린 창 크기
// (VVAutoResizeState의 last=)를 주기적으로 읽고, 새 알림이면 그 픽셀 크기의 1:1 모드로 바꾼다.
// 디스플레이 재구성 알림(CGDisplayRegisterReconfigurationCallback)은 이 경우 오지 않았다(0.0.44 실측: 원래 모드로
// 돌아오는 재구성이라 변화 없음으로 보는 것으로 추정). 그래서 속성을 주기적으로 읽는다.
//
// - 드라이버 설정(VVAutoResize, boot-arg vvideo_autoresize)이 꺼져 있으면 아무것도 하지 않는다.
// - 알림 번호(connect-changes)가 처리한 것보다 클 때만 움직인다. 사용자가 시스템 설정에서 고른 모드를 덮어쓰지 않기 위해서다.
// - 전환은 디스플레이 설정에 저장한다(.permanently). 세션에만 적용하면 다음 연결 변경 때 WindowServer가 저장된 모드로
//   한 번 되돌아가 화면이 한 번 더 깜박였다(0.0.46 실측: 100 → 5 → 100).
// IORegistry는 읽기만 하고 root 권한이 필요 없다.
import CoreGraphics
import Foundation
import IOKit

let pollSeconds = 1.0  // 드라이버가 이미 600 ms 동안 이벤트가 멎기를 기다린 뒤 알리므로 1초면 충분하다

struct ResizeState {
    let enabled: Bool
    let sequence: Int
    let width: Int
    let height: Int
}

func log(_ message: String) {
    let stamp = ISO8601DateFormatter().string(from: Date())
    print("\(stamp) \(message)")
    fflush(stdout)
}

// VirtioGpuDevice는 registerService를 하지 않아 매칭 조회로 찾을 수 없다. 서비스 평면을 돈다 (vvstat과 같다).
// 매번 평면 전체를 돌지 않게 찾은 항목을 들고 있다가 읽기에 실패하면(드라이버 재시작 등) 다시 찾는다.
var cachedEntry: io_registry_entry_t = 0

func findGpuEntry() -> io_registry_entry_t {
    var iterator: io_iterator_t = 0
    guard IORegistryCreateIterator(kIOMainPortDefault, kIOServicePlane, IOOptionBits(kIORegistryIterateRecursively),
                                   &iterator) == KERN_SUCCESS else { return 0 }
    defer { IOObjectRelease(iterator) }
    while case let entry = IOIteratorNext(iterator), entry != 0 {
        if IOObjectConformsTo(entry, "VirtioGpuDevice") != 0 {
            return entry
        }
        IOObjectRelease(entry)
    }
    return 0
}

func readProperty(_ key: String) -> String? {
    for _ in 0..<2 {
        if cachedEntry == 0 {
            cachedEntry = findGpuEntry()
            if cachedEntry == 0 { return nil }
        }
        let value = IORegistryEntryCreateCFProperty(cachedEntry, key as CFString, kCFAllocatorDefault, 0)
        if let text = value?.takeRetainedValue() as? String {
            return text
        }
        IOObjectRelease(cachedEntry)
        cachedEntry = 0
    }
    return nil
}

// 예: "on handler=enabled connect-changes=2 last=1712x984"
func parseState(_ text: String) -> ResizeState? {
    var sequence: Int?
    var size: [Int] = []
    for field in text.split(separator: " ") {
        if field.hasPrefix("connect-changes=") {
            sequence = Int(field.dropFirst("connect-changes=".count))
        } else if field.hasPrefix("last=") {
            size = field.dropFirst("last=".count).split(separator: "x").compactMap { Int($0) }
        }
    }
    guard let sequence, size.count == 2 else { return nil }
    return ResizeState(enabled: text.hasPrefix("on "), sequence: sequence, width: size[0], height: size[1])
}

func applyMode(width: Int, height: Int) {
    let display = CGMainDisplayID()
    if let current = CGDisplayCopyDisplayMode(display),
       current.pixelWidth == width, current.pixelHeight == height, current.width == width {
        log("already \(width)x\(height)")
        return
    }
    let options = [kCGDisplayShowDuplicateLowResolutionModes as String: true] as CFDictionary
    let modes = (CGDisplayCopyAllDisplayModes(display, options) as? [CGDisplayMode]) ?? []
    guard let mode = modes.first(where: {
        $0.pixelWidth == width && $0.pixelHeight == height && $0.width == width && $0.isUsableForDesktopGUI()
    }) else {
        log("no 1:1 mode \(width)x\(height) in \(modes.count) modes")
        return
    }
    var config: CGDisplayConfigRef?
    guard CGBeginDisplayConfiguration(&config) == .success else { return }
    guard CGConfigureDisplayWithDisplayMode(config, display, mode, nil) == .success else {
        CGCancelDisplayConfiguration(config)
        log("configure \(width)x\(height) failed")
        return
    }
    let result = CGCompleteDisplayConfiguration(config, .permanently)
    log("switch to \(width)x\(height) (mode id \(mode.ioDisplayModeID)) -> \(result.rawValue)")
}

// VVCurrentMode 예: "1600x900 changes=6". 드라이버가 받은 모드 전환 횟수다.
func modeChanges() -> Int? {
    guard let text = readProperty("VVCurrentMode"), let field = text.split(separator: " ").first(where: {
        $0.hasPrefix("changes=")
    }) else { return nil }
    return Int(field.dropFirst("changes=".count))
}

// 연결 변경 뒤 IOGraphics가 한 번, WindowServer가 저장된 모드로 또 한 번 모드를 바꾼다(0.0.44 실측). 그 전에 바꾸면
// WindowServer의 복원에 덮어써지므로, 드라이버의 모드 전환 횟수가 이만큼 연속으로 그대로일 때 바꾼다.
let quietPolls = 2

var handledSequence = readProperty("VVAutoResizeState").flatMap(parseState)?.sequence ?? 0
var pendingTarget: ResizeState?
var lastChanges: Int?
var quietCount = 0

func check() {
    if let text = readProperty("VVAutoResizeState"), let state = parseState(text), state.sequence > handledSequence {
        handledSequence = state.sequence
        if state.enabled {
            pendingTarget = state  // 더 새 알림이 오면 앞의 것을 버린다
            quietCount = 0
        } else {
            log("driver auto resize off, ignoring announcement \(state.sequence)")
        }
    }
    let changes = modeChanges()
    if changes != lastChanges {
        lastChanges = changes
        quietCount = 0
        return
    }
    quietCount += 1
    guard let target = pendingTarget, quietCount >= quietPolls else { return }
    pendingTarget = nil
    applyMode(width: target.width, height: target.height)
}

log("vvresize started, handled=\(handledSequence)")
let timer = Timer(timeInterval: pollSeconds, repeats: true) { _ in check() }
RunLoop.main.add(timer, forMode: .common)
RunLoop.main.run()
