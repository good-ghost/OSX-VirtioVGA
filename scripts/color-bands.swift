// AC-04 확인 (WI-5.010). 주 화면 전체에 빨강·초록·파랑·흰색 세로 띠를 띄운다.
// WindowServer → 드라이버 → 호스트로 가는 실제 화면 경로에서 색이 뒤바뀌지 않는지 호스트 창에서 눈으로 본다.
// 사용법: color-bands [표시 초, 기본 30]. 아무 키나 누르면 일찍 닫힌다.
import AppKit

let defaultSeconds = 30.0
let seconds = CommandLine.arguments.count >= 2 ? (Double(CommandLine.arguments[1]) ?? defaultSeconds) : defaultSeconds
let bandColors: [NSColor] = [.init(srgbRed: 1, green: 0, blue: 0, alpha: 1),
                             .init(srgbRed: 0, green: 1, blue: 0, alpha: 1),
                             .init(srgbRed: 0, green: 0, blue: 1, alpha: 1),
                             .init(srgbRed: 1, green: 1, blue: 1, alpha: 1)]

final class BandsView: NSView {
    override func draw(_ dirtyRect: NSRect) {
        let width = bounds.width / CGFloat(bandColors.count)
        for (index, color) in bandColors.enumerated() {
            color.setFill()
            NSRect(x: CGFloat(index) * width, y: 0, width: width, height: bounds.height).fill()
        }
    }
    override var acceptsFirstResponder: Bool { true }
    override func keyDown(with event: NSEvent) { NSApp.terminate(nil) }
}

let app = NSApplication.shared
app.setActivationPolicy(.regular)
guard let screen = NSScreen.main else { exit(1) }
let window = NSWindow(contentRect: screen.frame, styleMask: .borderless, backing: .buffered, defer: false, screen: screen)
window.level = .screenSaver
window.contentView = BandsView(frame: screen.frame)
window.makeKeyAndOrderFront(nil)
window.makeFirstResponder(window.contentView)
app.activate(ignoringOtherApps: true)
DispatchQueue.main.asyncAfter(deadline: .now() + seconds) { NSApp.terminate(nil) }
app.run()
