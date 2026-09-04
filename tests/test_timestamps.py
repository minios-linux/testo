from common import *
import re

PREFIX = "timestamps_qa_"


def test_timestamps():
    must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
    try:
        out, _ = must_succeed(
            f"testo run junit/basic.testo --prefix {PREFIX} --test-spec pass_case --invalidate pass_case --assume-yes"
        )
        assert re.search(r"\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} UTC\] \[\s*0%\]", out)

        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
        out, _ = must_succeed(
            f"testo run junit/basic.testo --prefix {PREFIX} --test-spec pass_case --invalidate pass_case --assume-yes --disable-timestamps"
        )
        assert not re.search(r"\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2} UTC\]", out)
        assert re.search(r"\[\s*0%\] Preparing the environment", out)
    finally:
        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
