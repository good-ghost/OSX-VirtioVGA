// AC-03 확인 (WI-5.010). 주 디스플레이의 모드를 하나씩 바꾸고, 바꾼 뒤 화면 캡처의 픽셀 크기가 모드의 픽셀 크기와 같은지 본다.
// 사용법: ac03-modes <캡처 저장 폴더>
// 끝나면 시작할 때의 모드로 되돌린다. 하나라도 다르면 종료 코드 1.
import CoreGraphics
import Foundation
import ImageIO

let args = CommandLine.arguments
guard args.count == 2 else {
    FileHandle.standardError.write("usage: ac03-modes <capture-dir>\n".data(using: .utf8)!)
    exit(2)
}
let captureDir = URL(fileURLWithPath: args[1], isDirectory: true)
try? FileManager.default.createDirectory(at: captureDir, withIntermediateDirectories: true)

let settleMicros: useconds_t = 2_500_000  // 모드 전환 뒤 화면이 다시 그려질 때까지 기다리는 시간
let display = CGMainDisplayID()
guard let original = CGDisplayCopyDisplayMode(display) else { exit(1) }
let options = [kCGDisplayShowDuplicateLowResolutionModes as String: true] as CFDictionary
var seen = Set<String>()
let modes = ((CGDisplayCopyAllDisplayModes(display, options) as? [CGDisplayMode]) ?? []).filter {
    $0.isUsableForDesktopGUI() && seen.insert("\($0.width)x\($0.height)/\($0.pixelWidth)x\($0.pixelHeight)").inserted
}

func apply(_ mode: CGDisplayMode) -> Bool {
    var config: CGDisplayConfigRef?
    guard CGBeginDisplayConfiguration(&config) == .success else { return false }
    guard CGConfigureDisplayWithDisplayMode(config, display, mode, nil) == .success else {
        CGCancelDisplayConfiguration(config)
        return false
    }
    return CGCompleteDisplayConfiguration(config, .forSession) == .success
}

// screencapture로 화면을 저장하고 그 파일의 픽셀 크기를 읽는다.
func capture(_ name: String) -> (Int, Int)? {
    let file = captureDir.appendingPathComponent(name)
    let task = Process()
    task.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
    task.arguments = ["-x", file.path]
    guard (try? task.run()) != nil else { return nil }
    task.waitUntilExit()
    guard let source = CGImageSourceCreateWithURL(file as CFURL, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { return nil }
    return (image.width, image.height)
}

print("modes: \(modes.count)")
var failures = 0
for mode in modes {
    let label = "\(mode.width)x\(mode.height) (pixels \(mode.pixelWidth)x\(mode.pixelHeight))"
    let switched = apply(mode)
    usleep(settleMicros)
    let size = capture("ac03-\(mode.pixelWidth)x\(mode.pixelHeight)-\(mode.width)x\(mode.height).png")
    let ok = switched && size?.0 == mode.pixelWidth && size?.1 == mode.pixelHeight
    if !ok { failures += 1 }
    print("\(label) -> capture \(size.map { "\($0.0)x\($0.1)" } ?? "none") \(ok ? "ok" : "FAIL")")
}
let restored = apply(original)
usleep(settleMicros)
print("restore \(original.width)x\(original.height) -> \(restored ? "ok" : "FAIL")")
print("failures=\(failures)")
exit(failures == 0 && restored ? 0 : 1)
