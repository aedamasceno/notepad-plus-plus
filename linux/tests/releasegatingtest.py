#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
spec = (root / "packaging/textpinnacle.spec").read_text()
workflow_path = root / ".github/workflows/linux.yml"

assert "-DBUILD_TESTING:BOOL=ON" in spec, "RPM build must explicitly enable tests"
check = spec.split("%check", 1)[1].split("%files", 1)[0]
assert "%ctest" in check and "--output-on-failure" in check, "RPM %check must run CTest"
assert "desktop-file-validate" in check and "appstream-util validate-relax" in check

assert workflow_path.exists(), "dedicated Linux workflow is required"
workflow = workflow_path.read_text()
for required in (
    "pull_request:", "push:", "fedora:", "CMAKE_BUILD_TYPE=Debug",
    "cmake --build build-linux", "ctest --test-dir build-linux --output-on-failure",
    "build-linux/linux/textpinnacle", "cmake --install build-linux",
    "desktop-file-validate", "appstream-util validate-relax",
    "textpinnacle.desktop", "textpinnacle.png",
    "io.github.aedamasceno.textpinnacle.metainfo.xml",
    "useradd --create-home", "textpinnacle-ci", "runuser --user textpinnacle-ci",
    "HOME=/home/textpinnacle-ci", "TMPDIR=/tmp/textpinnacle-ci",
):
    assert required in workflow, f"Linux workflow missing: {required}"
assert workflow.index("runuser --user textpinnacle-ci") < workflow.index(
    "ctest --test-dir build-linux --output-on-failure"
), "CTest must execute through the unprivileged CI user"
print("Linux release gates are present")
