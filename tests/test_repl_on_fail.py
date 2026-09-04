from common import *

PREFIX = "repl_on_fail_qa_"


def test_repl_on_fail():
    must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
    try:
        out, _ = must_fail(
            f"testo run junit/basic.testo --prefix {PREFIX} --test-spec fail_case "
            f"--repl-on-fail --assume-yes --disable-timestamps",
            input='print "after failure"\n',
        )
        assert "[100%] Entering REPL mode on probe" in out
        assert "probe: after failure" in out
        assert "You have entered the following commands:" in out
        assert 'print "after failure"' in out
        assert "[100%] Leaving REPL mode on probe" in out
        assert out.index("Leaving REPL mode on probe") < out.index("never reached Pass status")

        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
        out, _ = must_fail(
            f"testo run junit/basic.testo --prefix {PREFIX} --test-spec fail_case "
            f"--repl-on-fail --ignore-repl --assume-yes --disable-timestamps"
        )
        assert "Entering REPL mode" not in out

        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
        out, _ = must_fail(
            f"testo run junit/basic.testo --prefix {PREFIX} --test-spec fail_case "
            f"--repl-on-fail --stop-on-fail --assume-yes --disable-timestamps",
            err=2,
            input="",
        )
        assert "[100%] Entering REPL mode on probe" in out
        assert "never reached Pass status" not in out

        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
        out, _ = must_fail(
            f"testo run junit/basic.testo --prefix {PREFIX} --test-spec fail_case "
            f"--repl-on-fail --repeat-failed 1 --assume-yes --disable-timestamps",
            input="\n",
        )
        assert out.count("[100%] Entering REPL mode on probe") == 2
        assert out.count("Retrying failed test. Attempt 1 of 1.") == 1
    finally:
        must_succeed(f"testo clean --prefix {PREFIX} --assume-yes")
