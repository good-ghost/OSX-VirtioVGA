// VirtioGpuDevice 구현 파일들(VirtioGpuDevice.cpp, VirtioGpuDeviceCommands.cpp)이 같이 쓰는 상수.
// 클래스 바깥에 공개할 것이 아니라 이 디렉터리 안에서만 포함한다.
#pragma once

#include <IOKit/IOLib.h>

#include "vv/VirtioGpuSpec.hpp"

namespace vv::gpudevice {

// 속성·로그 요약 문자열 버퍼 크기.
inline constexpr size_t kSummaryBytes = 128;

// 명령 응답 대기 한도 (REQ-024). QEMU는 수 ms 안에 응답하므로 넉넉히 잡는다.
inline constexpr uint32_t kCommandTimeoutMs = 1000;
// 명령 버퍼: 앞쪽에 요청, kResponseOffset부터 응답. 가장 큰 응답(RespEdid, 1056바이트)도 들어간다.
inline constexpr uint32_t kCommandBufferBytes = PAGE_SIZE;
inline constexpr uint32_t kResponseOffset = 1024;
inline constexpr uint32_t kMaxRequestBytes = kResponseOffset;
static_assert(sizeof(vv::virtio::gpu::RespEdid) <= kCommandBufferBytes - kResponseOffset,
              "응답 영역이 가장 큰 응답보다 작다");

} // namespace vv::gpudevice
