# Running Testo

## Interpreter arguments

The base component of Testo Framework is the `testo` interpreter. The command-line interface follows the modern Testo spelling with hyphenated option names.

The interpreter can run tests (`testo run`), clean Testo-managed entities (`testo clean`), print help (`testo help`), or print its version (`testo --version`).

### Tests run mode

SYNOPSIS

```text
testo run <input file | input folder> [--param <param-name> <param-value>]... \
  [--test-spec <wildcard pattern>]... [--exclude <wildcard pattern>]... \
  [--prefix <prefix>] [--stop-on-fail] [--user] [--assume-yes] \
  [--invalidate <wildcard pattern>] [--report-folder </path/to/folder>] \
  [--report-format <format>] [--content-cksum-maxsize <Size in Megabytes>] \
  [--html] [--nn-server <ip:port>] --allowed-sharing-directory <path> \
  [--hypervisor <hypervisor type>] [--log-level <log level>] [--dry] \
  [--ignore-repl] [--skip-tests-with-repl]
```

- `input file` or `input folder`: Path to a `.testo` file or a folder containing test scripts. Folder input is searched recursively.
- `--param <param-name> <param-value>`: Define a parameter visible to test scenarios.
- `--test-spec <wildcard pattern>`: Run only tests matching the pattern.
- `--exclude <wildcard pattern>`: Exclude tests matching the pattern.
- `--prefix <prefix>`: Prefix all virtual entities, providing independent namespaces for otherwise identical test benches.
- `--stop-on-fail`: Stop execution after the first failed test.
- `--user`: On Linux/QEMU, run through the user's `qemu:///session` libvirt instance instead of requiring root and `qemu:///system`.
- `--assume-yes`: Do not ask for confirmation before running tests whose cache was invalidated.
- `--invalidate <wildcard pattern>`: Force cache invalidation for matching tests.
- `--report-folder </path/to/folder>`: Destination for generated reports.
- `--report-format <format>`: Select `allure`, `native_remote`, or `native_local` reporting.
- `--content-cksum-maxsize <Size in Megabytes>`: Maximum file size for content-based cache checks instead of modification-time checks.
- `--html`: Format standard output as HTML.
- `--nn-server <ip:port>`: Address of `testo-nn-server`. Default: `127.0.0.1:8156`.
- `--allowed-sharing-directory <path>`: **Mandatory.** Directory containing only files that may be sent to an untrusted NN server. If the NN server requests a reference image outside this directory, Testo rejects the request. Canonical paths are checked, so `..` and symlink escapes are not allowed.
- `--hypervisor <hypervisor type>`: Select the hypervisor backend. QEMU is the supported Linux backend; Hyper-V support is experimental on Windows.
- `--log-level <log level>`: Select the interpreter log level (`info` or `trace`).
- `--dry`: Perform parsing and semantic validation without executing tests.
- `--ignore-repl`: Ignore `repl` actions instead of entering interactive mode.
- `--skip-tests-with-repl`: Skip tests containing a `repl` action.

In Linux user mode Testo stores its state under `$HOME/.local/share/libvirt/testo` and logs under `$HOME/.local/state/testo`.

**Return values**

- `0` — all queued tests completed successfully.
- `1` — at least one queued test failed.
- `2` — syntax, semantic, configuration, or runtime setup error.

### Entity clean mode

SYNOPSIS

```text
testo clean [--prefix <prefix>] [--user] [--assume-yes] \
  [--hypervisor <hypervisor type>] [--log-level <log level>]
```

- `--prefix <prefix>`: Clean only Testo-managed entities with the specified prefix.
- `--user`: Clean entities from the user's libvirt session and user-state directory.
- `--assume-yes`: Erase matching Testo entities without an interactive confirmation.

Running `testo clean` without a prefix cleans only Testo-managed entities without a prefix. Manually created entities are not selected by this mechanism.

### Version

```text
testo --version
```

The legacy `testo version` form is not part of the modern command-line interface.

## Tests queueing algorithm

By default, the interpreter schedules all tests in the input `.testo` file or folder. `--test-spec` and `--exclude` are applied in command-line order as a filter pipeline:

1. Collect all test names from the input.
2. `--test-spec` keeps only matching names; `--exclude` removes matching names.
3. Pass the resulting set through each subsequent filter.
4. Queue the remaining tests.

### Wildcard pattern format

| Syntax | Meaning |
| --- | --- |
| `*` | Any number of characters |
| `?` | Any single character |
| `\` | Escape symbol |
| `[abc]` | Any character listed in the brackets |
| `[!abc]` | Any character except those listed in the brackets |
| <code>(abc&#124;c)</code> | Any of the sequences listed in parentheses |
