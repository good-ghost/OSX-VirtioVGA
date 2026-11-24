#include "TestMain.hpp"

#include "vv/VirtioIrq.hpp"

namespace v = vv::virtio;

VV_TEST(irq_bootArg_parsing)
{
    VV_EXPECT(v::irqModeFromBootArg(false, 0) == v::IrqMode::MsiX);
    VV_EXPECT(v::irqModeFromBootArg(true, 0) == v::IrqMode::MsiX);
    VV_EXPECT(v::irqModeFromBootArg(true, 1) == v::IrqMode::Intx);
    VV_EXPECT(v::irqModeFromBootArg(true, 2) == v::IrqMode::Poll);
    VV_EXPECT(v::irqModeFromBootArg(true, 3) == v::kDefaultIrqMode);
    VV_EXPECT(v::irqModeFromBootArg(true, -1) == v::kDefaultIrqMode);
}

VV_TEST(irq_vectors_separateWhenThreeSources)
{
    VV_EXPECT(v::configVectorFor(v::IrqMode::MsiX) == 0);
    VV_EXPECT(v::queueVectorFor(v::IrqMode::MsiX, v::gpu::kControlQueue, 3) == 1);
    VV_EXPECT(v::queueVectorFor(v::IrqMode::MsiX, v::gpu::kCursorQueue, 3) == 2);
    VV_EXPECT(v::queueVectorFor(v::IrqMode::MsiX, 7, 3) == v::pci::kMsiNoVector);
}

VV_TEST(irq_vectors_sharedWhenOneSource)
{
    // WI-1.005 실측: macOS가 이 장치에 MSI-X 소스를 1개만 붙인다
    VV_EXPECT(v::queueVectorFor(v::IrqMode::MsiX, v::gpu::kControlQueue, 1) == 0);
    VV_EXPECT(v::queueVectorFor(v::IrqMode::MsiX, v::gpu::kCursorQueue, 1) == 0);
    VV_EXPECT(v::queueVectorFor(v::IrqMode::MsiX, v::gpu::kCursorQueue, 2) == 0);
}

VV_TEST(irq_vectors_noVectorOutsideMsix)
{
    VV_EXPECT(v::configVectorFor(v::IrqMode::Intx) == v::pci::kMsiNoVector);
    VV_EXPECT(v::queueVectorFor(v::IrqMode::Intx, v::gpu::kControlQueue, 3) == v::pci::kMsiNoVector);
    VV_EXPECT(v::queueVectorFor(v::IrqMode::Poll, v::gpu::kControlQueue, 1) == v::pci::kMsiNoVector);
}

VV_TEST(irq_effectiveMode_fallsBack)
{
    VV_EXPECT(v::effectiveIrqMode(v::IrqMode::MsiX, 1, false) == v::IrqMode::MsiX);
    VV_EXPECT(v::effectiveIrqMode(v::IrqMode::MsiX, 0, true) == v::IrqMode::Intx);
    VV_EXPECT(v::effectiveIrqMode(v::IrqMode::MsiX, 0, false) == v::IrqMode::Poll);
    VV_EXPECT(v::effectiveIrqMode(v::IrqMode::Intx, 1, false) == v::IrqMode::Poll);  // 실측 환경: 레거시 없음
    VV_EXPECT(v::effectiveIrqMode(v::IrqMode::Intx, 1, true) == v::IrqMode::Intx);
    VV_EXPECT(v::effectiveIrqMode(v::IrqMode::Poll, 3, true) == v::IrqMode::Poll);
}

VV_TEST(irq_isrDecode)
{
    VV_EXPECT(v::isrHasQueueEvent(0x1) && !v::isrHasConfigEvent(0x1));
    VV_EXPECT(!v::isrHasQueueEvent(0x2) && v::isrHasConfigEvent(0x2));
    VV_EXPECT(v::isrHasQueueEvent(0x3) && v::isrHasConfigEvent(0x3));
    VV_EXPECT(!v::isrHasQueueEvent(0x0) && !v::isrHasConfigEvent(0x0));
}
