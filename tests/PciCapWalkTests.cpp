#include "TestMain.hpp"

#include <cstring>

#include "vv/PciCapWalk.hpp"

namespace {

// 256바이트 PCI 설정 공간 흉내. 리틀 엔디언으로 쓰고 읽는다.
struct FakeConfig {
    uint8_t bytes[vv::pci::kConfigSpaceBytes] = {};

    uint8_t read8(uint8_t offset) const { return bytes[offset]; }
    uint16_t read16(uint8_t offset) const
    {
        return static_cast<uint16_t>(bytes[offset] | (bytes[(offset + 1) & 0xFF] << 8));
    }
    uint32_t read32(uint8_t offset) const
    {
        uint32_t value = 0;
        for (int i = 3; i >= 0; --i) {
            value = (value << 8) | bytes[(offset + i) & 0xFF];
        }
        return value;
    }
    void write32(uint8_t offset, uint32_t value)
    {
        for (int i = 0; i < 4; ++i) {
            bytes[(offset + i) & 0xFF] = static_cast<uint8_t>(value >> (8 * i));
        }
    }
    void addVirtioCap(uint8_t at, uint8_t next, uint8_t cfgType, uint8_t bar, uint32_t off, uint32_t len,
                      uint8_t capLen = 16)
    {
        bytes[at] = vv::virtio::pci::kCapIdVendorSpecific;
        bytes[at + 1] = next;
        bytes[at + 2] = capLen;
        bytes[at + 3] = cfgType;
        bytes[at + 4] = bar;
        write32(static_cast<uint8_t>(at + 8), off);
        write32(static_cast<uint8_t>(at + 12), len);
    }
};

auto reader8(const FakeConfig &c)
{
    return [&c](uint8_t o) { return c.read8(o); };
}
auto reader32(const FakeConfig &c)
{
    return [&c](uint8_t o) { return c.read32(o); };
}

// QEMU virtio-pci modern 장치와 같은 모양: common/notify/isr/device/pci cfg + MSI-X
FakeConfig qemuLikeConfig()
{
    FakeConfig c;
    c.bytes[vv::pci::kStatusRegister] = static_cast<uint8_t>(vv::pci::kStatusCapabilitiesList);
    // 겹치지 않게 배치: msix(0xA0,12) → common(0x90,16) → isr(0x80,16) → device(0x70,16)
    //                   → notify(0x58,20) → pci cfg(0x40,20)
    c.bytes[vv::pci::kCapabilitiesPointer] = 0xA0;
    c.bytes[0xA0] = vv::pci::kCapIdMsix;
    c.bytes[0xA1] = 0x90;
    c.bytes[0xA2] = 0x02;  // table size - 1 = 2 → 3 vectors
    c.bytes[0xA3] = 0x80;  // enable
    c.addVirtioCap(0x90, 0x80, 1, 2, 0x0000, 0x1000);
    c.addVirtioCap(0x80, 0x70, 3, 2, 0x1000, 0x1000);
    c.addVirtioCap(0x70, 0x58, 4, 2, 0x2000, 0x1000);
    c.addVirtioCap(0x58, 0x40, 2, 2, 0x3000, 0x1000, 20);
    c.write32(0x58 + 16, 4);
    c.addVirtioCap(0x40, 0x00, 5, 0, 0, 0, 20);
    return c;
}

} // namespace

VV_TEST(pciCaps_qemuLike_findsAllVirtioCapsAndMsix)
{
    const FakeConfig c = qemuLikeConfig();
    VV_EXPECT(vv::pci::hasCapabilityList([&c](uint8_t o) { return c.read16(o); }));

    vv::pci::VirtioCap caps[8] = {};
    const size_t n = vv::pci::findVirtioCaps(reader8(c), reader32(c), caps, 8);
    VV_EXPECT(n == 5);
    VV_EXPECT(caps[0].cfgType == 1 && caps[0].bar == 2 && caps[0].offset == 0 && caps[0].length == 0x1000);
    VV_EXPECT(caps[3].cfgType == 2 && caps[3].notifyOffMultiplier == 4);

    const uint8_t msix = vv::pci::findCapability(reader8(c), vv::pci::kCapIdMsix);
    VV_EXPECT(msix == 0xA0);
    const uint16_t control = c.read16(static_cast<uint8_t>(msix + vv::pci::kMsixControlOffset));
    VV_EXPECT((control & vv::pci::kMsixControlEnable) != 0);
    VV_EXPECT((control & vv::pci::kMsixControlTableSizeMask) + 1 == 3);
}

VV_TEST(pciCaps_cyclicList_stops)
{
    FakeConfig c;
    c.bytes[vv::pci::kCapabilitiesPointer] = 0x40;
    c.addVirtioCap(0x40, 0x50, 1, 2, 0, 0x100);
    c.addVirtioCap(0x50, 0x40, 4, 2, 0x100, 0x100);  // 0x40으로 되돌아간다
    vv::pci::VirtioCap caps[8] = {};
    VV_EXPECT(vv::pci::findVirtioCaps(reader8(c), reader32(c), caps, 8) == 2);
}

VV_TEST(pciCaps_pointerIntoHeader_ignored)
{
    FakeConfig c;
    c.bytes[vv::pci::kCapabilitiesPointer] = 0x20;  // 헤더 안을 가리키는 잘못된 포인터
    vv::pci::VirtioCap caps[8] = {};
    VV_EXPECT(vv::pci::findVirtioCaps(reader8(c), reader32(c), caps, 8) == 0);
}

VV_TEST(pciCaps_invalidCaps_skipped)
{
    FakeConfig c;
    c.bytes[vv::pci::kCapabilitiesPointer] = 0x40;
    c.addVirtioCap(0x40, 0x50, 1, 6, 0, 0x100);          // BAR 6: 거절
    c.addVirtioCap(0x50, 0x60, 1, 2, 0, 0x100, 8);       // cap_len 8: 거절
    c.addVirtioCap(0xF8, 0x00, 1, 2, 0, 0x100);          // 끝(0xF8+16)이 256을 넘음: 거절
    c.addVirtioCap(0x60, 0xF8, 4, 2, 0x100, 0x100);      // 유효
    vv::pci::VirtioCap caps[8] = {};
    const size_t n = vv::pci::findVirtioCaps(reader8(c), reader32(c), caps, 8);
    VV_EXPECT(n == 1);
    VV_EXPECT(caps[0].cfgType == 4);
}

VV_TEST(pciCaps_outputFull_stopsAtMax)
{
    const FakeConfig c = qemuLikeConfig();
    vv::pci::VirtioCap caps[2] = {};
    VV_EXPECT(vv::pci::findVirtioCaps(reader8(c), reader32(c), caps, 2) == 2);
}
