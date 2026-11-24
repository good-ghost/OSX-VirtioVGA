// 커널 빌드 설정(-mkernel)에서도 사양 헤더의 static_assert가 통과하는지 확인하기 위한 번역 단위.
// 코드를 만들지 않는다. P1에서 전송 계층이 이 헤더를 직접 쓰기 시작하면 이 파일은 지운다.
#include "vv/VirtioGpuSpec.hpp"
#include "vv/VirtioSpec.hpp"
