// 주 디스플레이 모드를 하나 고른다 (WI-5.004 측정용). 사용법: set-mode <폭>x<높이> [hidpi]
// 폭·높이는 UI 크기(point). hidpi를 주면 픽셀이 두 배인 모드를, 아니면 1:1 모드를 고른다.
import CoreGraphics
import Foundation

let args = CommandLine.arguments
let parts = args.count >= 2 ? args[1].split(separator: "x").compactMap { Int($0) } : []
guard parts.count == 2 else {
    FileHandle.standardError.write("usage: set-mode <width>x<height> [hidpi]\n".data(using: .utf8)!)
    exit(2)
}
let wantHiDPI = args.count >= 3 && args[2] == "hidpi"
let display = CGMainDisplayID()
let options = [kCGDisplayShowDuplicateLowResolutionModes as String: true] as CFDictionary
let modes = (CGDisplayCopyAllDisplayModes(display, options) as? [CGDisplayMode]) ?? []
guard let mode = modes.first(where: {
    $0.width == parts[0] && $0.height == parts[1] && ($0.pixelWidth > $0.width) == wantHiDPI && $0.isUsableForDesktopGUI()
}) else {
    FileHandle.standardError.write("set-mode: no such mode\n".data(using: .utf8)!)
    exit(1)
}
var config: CGDisplayConfigRef?
guard CGBeginDisplayConfiguration(&config) == .success,
      CGConfigureDisplayWithDisplayMode(config, display, mode, nil) == .success,
      CGCompleteDisplayConfiguration(config, .forSession) == .success else {
    FileHandle.standardError.write("set-mode: configuration failed\n".data(using: .utf8)!)
    exit(1)
}
print("mode \(mode.width)x\(mode.height) pixels \(mode.pixelWidth)x\(mode.pixelHeight)")
