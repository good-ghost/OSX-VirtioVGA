#!/usr/bin/env python3
"""macOS 커널 패닉 보고서(.panic)를 요약하고, 이 kext의 백트레이스 프레임을 소스 줄로 바꾼다 (WI-0.004).

사용법:
  python3 -I scripts/panic-summary.py <Kernel-*.panic> [--dsym <DWARF 파일>]
  인자를 생략하면 /Library/Logs/DiagnosticReports/의 가장 최근 Kernel-*.panic을 쓴다.

보고서 형식(macOS 15 실측): 1행은 메타데이터 JSON, 2행부터는 본문 JSON이고 본문의
"macOSPanicString"에 패닉 문자열이 있다. 패닉 문자열의 "Kernel Extensions in backtrace:" 절에
"<bundle id>(<버전>)[<UUID>]@<시작>-><끝>" 형식으로 kext 로드 범위가 나온다.
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_DSYM = ROOT / "build/VirtioVideo.kext.dSYM/Contents/Resources/DWARF/VirtioVideo"
DSYM_ARCHIVE = ROOT / "build/dsym-archive"  # scripts/build.sh가 <UUID>.dSYM으로 보관한다
DWARF_SUBPATH = "Contents/Resources/DWARF/VirtioVideo"
PANIC_DIR = pathlib.Path("/Library/Logs/DiagnosticReports")
BUNDLE_ID = "local.virtiovideo.VirtioVideo"
SUMMARY_LINES = 3

KEXT_RANGE_RE = re.compile(
    re.escape(BUNDLE_ID) + r"\(([^)]*)\)\[([0-9A-Fa-f-]+)\]@(0x[0-9a-f]+)->(0x[0-9a-f]+)"
)
FRAME_RE = re.compile(r"^(0x[0-9a-f]+) : (0x[0-9a-f]+)", re.MULTILINE)


def latest_panic() -> pathlib.Path:
    """가장 최근 커널 패닉 보고서 경로를 돌려준다.

    Returns:
        수정 시각이 가장 늦은 Kernel-*.panic 경로.
    """
    reports = sorted(PANIC_DIR.glob("Kernel-*.panic"), key=lambda p: p.stat().st_mtime)
    if not reports:
        sys.exit(f"패닉 보고서가 없다: {PANIC_DIR}/Kernel-*.panic")
    return reports[-1]


def read_panic(path: pathlib.Path) -> tuple[dict[str, object], str]:
    """보고서를 읽어 메타데이터와 패닉 문자열을 돌려준다.

    Args:
        path: .panic 파일 경로.

    Returns:
        (1행 메타데이터 dict, 패닉 문자열).
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    header_line, _, body_text = text.partition("\n")
    try:
        header = json.loads(header_line)
        body = json.loads(body_text)
    except json.JSONDecodeError as error:
        raise SystemExit(f"보고서 JSON을 읽지 못했다 ({path}): {error}") from error
    panic_string = body.get("macOSPanicString") or body.get("panicString") or ""
    return header, str(panic_string)


def dsym_uuid(dsym: pathlib.Path) -> str:
    """dSYM의 x86_64 UUID를 돌려준다. 찾지 못하면 빈 문자열."""
    result = subprocess.run(["dwarfdump", "--uuid", str(dsym)], capture_output=True, text=True, check=True)
    match = re.search(r"UUID: ([0-9A-F-]+)", result.stdout)
    return match.group(1) if match else ""


def symbolicate(dsym: pathlib.Path, load_address: str, addresses: list[str]) -> list[str]:
    """kext 로드 주소 기준으로 주소들을 소스 위치로 바꾼다.

    Args:
        dsym: DWARF 파일 경로.
        load_address: 패닉 보고서의 kext 시작 주소.
        addresses: 바꿀 반환 주소 목록.

    Returns:
        주소마다 atos 출력 한 줄.
    """
    result = subprocess.run(
        ["atos", "-o", str(dsym), "-arch", "x86_64", "-l", load_address, *addresses],
        capture_output=True, text=True, check=True,
    )
    return result.stdout.splitlines()


def main() -> int:
    """명령행 진입점. 종료 코드 0 = 정상, 1 = 심볼 변환 불가."""
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("panic", nargs="?", type=pathlib.Path)
    parser.add_argument("--dsym", type=pathlib.Path, default=DEFAULT_DSYM)
    args = parser.parse_args()

    path = args.panic or latest_panic()
    header, panic_string = read_panic(path)
    print(f"file: {path}")
    print(f"time: {header.get('timestamp', '?')}  os: {header.get('os_version', '?')}")
    print("panic:")
    for line in panic_string.splitlines()[:SUMMARY_LINES]:
        print(f"  {line}")

    kext = KEXT_RANGE_RE.search(panic_string)
    if kext is None:
        print(f"{BUNDLE_ID}: 백트레이스에 없음")
        return 0

    version, uuid, start, end = kext.groups()
    print(f"{BUNDLE_ID}: 버전 {version}, UUID {uuid}, 로드 범위 {start}->{end}")
    frames = [ret for _, ret in FRAME_RE.findall(panic_string) if int(start, 16) <= int(ret, 16) <= int(end, 16)]
    if not frames:
        print("  이 kext 범위의 프레임 없음")
        return 0

    dsym = args.dsym
    if not dsym.exists() or dsym_uuid(dsym).upper() != uuid.upper():
        # 다른 빌드의 dSYM으로 바꾸면 엉뚱한 줄이 나온다. 보관소에서 같은 UUID를 찾는다.
        dsym = DSYM_ARCHIVE / f"{uuid.upper()}.dSYM" / DWARF_SUBPATH
    if not dsym.exists():
        print(f"  UUID {uuid}의 dSYM이 없다 ({args.dsym}, {DSYM_ARCHIVE}) — 주소만 출력: {' '.join(frames)}")
        return 1
    print(f"  dSYM: {dsym}")
    for address, symbol in zip(frames, symbolicate(dsym, start, frames)):
        print(f"  {address}  {symbol}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
