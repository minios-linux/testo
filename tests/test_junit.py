from common import *
from pathlib import Path
import xml.etree.ElementTree as ET

PREFIX = "junit_qa_"
REPORT = Path("/tmp/testo-junit-qa.xml")


def run_report(args, should_fail=False):
    if REPORT.exists():
        REPORT.unlink()
    cmd = f"testo run junit/basic.testo --prefix {PREFIX} --assume-yes --junit-report {REPORT} {args}"
    if should_fail:
        must_fail(cmd)
    else:
        must_succeed(cmd)
    assert REPORT.is_file()
    return ET.parse(REPORT).getroot()


def test_junit_report():
    must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
    try:
        suite = run_report("--test-spec pass_case --invalidate pass_case")
        assert suite.attrib["tests"] == "1"
        assert suite.attrib["failures"] == "0"
        assert suite.attrib["skipped"] == "0"
        assert suite.find("testcase").attrib["name"] == "pass_case"

        suite = run_report("--test-spec pass_case")
        skipped = suite.find("testcase/skipped")
        assert skipped is not None
        assert skipped.attrib["message"] == "test pass_case is up-to-date"
        assert skipped.text == "This test is cached"

        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
        suite = run_report("--test-spec fail_case --invalidate fail_case", should_fail=True)
        failure = suite.find("testcase/failure")
        assert suite.attrib["failures"] == "1"
        assert failure is not None
        assert "Test fail_case FAILED" in failure.attrib["message"]
        assert "intentional failure" in (failure.text or "")

        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
        suite = run_report("--invalidate fail_case", should_fail=True)
        cases = {case.attrib["name"]: case for case in suite.findall("testcase")}
        assert suite.attrib["tests"] == "3"
        assert suite.attrib["failures"] == "1"
        assert suite.attrib["skipped"] == "1"
        child = cases["child_skip"].find("skipped")
        assert child is not None
        assert "Parent test fail_case has status failed" in child.attrib["message"]
        assert suite.find("system-out") is not None
    finally:
        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
        if REPORT.exists():
            REPORT.unlink()
