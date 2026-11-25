// AC-07 측정 (WI-5.007). 화면 캡처 PNG에서 배경·테두리·메뉴 막대 픽셀을 재고 Metal 장치 수를 센다.
// REQUIREMENT §2.5와 같은 방법(픽셀 측정)을 HiDPI 캡처에도 쓸 수 있게 point 단위로 바꿔 출력한다.
//
// 사용법: ac07-measure <캡처.png> <point 폭>
//   point 폭은 화면의 UI 폭(예: 1280). 캡처 픽셀 폭 / point 폭 = 배율.
import CoreGraphics
import Foundation
import ImageIO
import Metal

struct RGB: Equatable, CustomStringConvertible {
    let r: Int, g: Int, b: Int
    var description: String { "RGB(\(r),\(g),\(b))" }
}

func load(_ path: String) -> (CGImage, [UInt8], Int)? {
    guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { return nil }
    let width = image.width, height = image.height
    var pixels = [UInt8](repeating: 0, count: width * height * 4)
    guard let context = CGContext(data: &pixels, width: width, height: height, bitsPerComponent: 8,
                                  bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
    context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
    return (image, pixels, width)
}

let args = CommandLine.arguments
guard args.count == 3, let pointWidth = Int(args[2]), pointWidth > 0,
      let (image, pixels, width) = load(args[1]) else {
    FileHandle.standardError.write("usage: ac07-measure <capture.png> <point-width>\n".data(using: .utf8)!)
    exit(2)
}
let height = image.height
let scale = max(width / pointWidth, 1)

// point 좌표(왼쪽 위 원점)의 픽셀 색. 배율 영역의 왼쪽 위 픽셀을 쓴다.
func color(_ px: Int, _ py: Int) -> RGB {
    let x = min(px * scale, width - 1), y = min(py * scale, height - 1)
    let i = (y * width + x) * 4
    return RGB(r: Int(pixels[i]), g: Int(pixels[i + 1]), b: Int(pixels[i + 2]))
}

let pointHeight = height / scale
let midY = pointHeight / 2
let midX = pointWidth / 2
let center = color(midX, midY)
// 왼쪽 끝에서 가운데로 가며 가운데 색과 같아지는 첫 위치 = 테두리 폭(point).
var border = 0
while border < midX && color(border, midY) != center { border += 1 }
let menuSamples = [color(midX, 4), color(pointWidth / 4, 12), color(pointWidth * 3 / 4, 20)]
let menuUniform = menuSamples.allSatisfy { $0 == menuSamples[0] }

print("capture: \(width)x\(height) px, scale \(scale), \(pointWidth)x\(pointHeight) pt")
print("center: \(center)")
print("left edge (x=0): \(color(0, midY)), bottom edge (y=\(pointHeight - 1)): \(color(midX, pointHeight - 1))")
print("border: \(border) pt\(border > 0 ? " color \(color(0, midY))" : " (none)")")
print("menu bar samples: \(menuSamples.map(\.description).joined(separator: " ")) uniform=\(menuUniform)")
print("metal devices: \(MTLCopyAllDevices().count)")
