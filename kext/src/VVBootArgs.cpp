#include "VVBootArgs.hpp"

#include <pexpert/pexpert.h>

namespace vv {

bool isDriverDisabledByBootArg()
{
    // 값 없는 플래그는 PE_parse_boot_argn이 1로 채운다. 버퍼 크기만 맞으면 된다.
    int flag = 0;
    return PE_parse_boot_argn(kDisableBootArg, &flag, sizeof(flag));
}

} // namespace vv
