from common import *
from pathlib import Path
import xml.etree.ElementTree as ET

PREFIX = "repeat_failed_qa_"
REPORT = Path("/tmp/testo-repeat-failed-qa.xml")


def test_repeat_failed():
    must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
    try:
        if REPORT.exists():
            REPORT.unlink()
        out, _ = must_fail(
            f"testo run repeat_failed/basic.testo --prefix {PREFIX} --test-spec fail_case "
            f"--repeat-failed 2 --junit-report {REPORT} --assume-yes --disable-timestamps"
        )
        assert out.count("Retrying failed test.") == 2
        assert out.count("Restoring snapshot initial") == 2
        assert "Test fail_case ran 2 times and never reached Pass status." in out
        suite = ET.parse(REPORT).getroot()
        failure = suite.find("testcase/failure")
        system_out = suite.findtext("system-out") or ""
        assert failure is not None
        assert (failure.text or "").count("intentional failure") >= 3
        assert "Retrying failed test. Attempt 1 of 2." not in (failure.text or "")
        assert system_out.count("Retrying failed test.") == 2

        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
        out, _ = must_fail(
            f"testo run repeat_failed/basic.testo --prefix {PREFIX} --test-spec fail_case "
            f"--repeat-failed 2 --stop-on-fail --assume-yes --disable-timestamps",
            err=2,
        )
        assert "Retrying failed test." not in out
        assert "never reached Pass status" not in out

        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
        out, _ = must_fail(
            f"testo run repeat_failed/basic.testo --prefix {PREFIX} --test-spec fail_without_snapshots "
            f"--repeat-failed 1 --assume-yes --disable-timestamps"
        )
        assert "Retrying failed test. Attempt 1 of 1." in out
        assert "Restoring snapshot initial" in out
    finally:
        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
        if REPORT.exists():
            REPORT.unlink()
