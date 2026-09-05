# Running Testo

## Interpreter arguments

The base component of Testo Framework is the `testo` interpreter. The command-line interface follows the modern Testo spelling with hyphenated option names.

The interpreter can run tests (`testo run`), clean Testo-managed entities (`testo clean`), print help (`testo help`), or print its version (`testo --version`).

### Tests run mode

SYNOPSIS

```text
testo run <input file | input folder> [--param <param-name> <param-value>]... \
  [--test-spec <wildcard pattern>]... [--exclude <wildcard pattern>]... \
  [--prefix <prefix>] [--stop-on-fail] [--repl-on-fail] [--debug] [--user] [--assume-yes] \
  [--invalidate <wildcard pattern>] [--report-folder </path/to/folder>] \
  [--report-format <format>] [--junit-report <path to JUnit report xml file>] \
  [--content-cksum-maxsize <Size in Megabytes>] \
  [--html] [--nn-server <ip:port>] --allowed-sharing-directory <path> \
  [--hypervisor <hypervisor type>] [--log-level <log level>] [--dry] \
  [--ignore-repl] [--disable-timestamps] [--skip-tests-with-repl] \
  [--needles <needles directory>] [--record-tests] \
  [--bootstrap-file </path/to/testo_file>] \
  [--export <path to destination>] \
  [--export-on-fail <path to destination>] [--repeat-failed <repeat number>]
```

- `input file` or `input folder`: Path to a `.testo` file or a folder containing test scripts. Folder input is searched recursively.
- `--param <param-name> <param-value>`: Define a parameter visible to test scenarios.
- `--test-spec <wildcard pattern>`: Run only tests matching the pattern.
- `--exclude <wildcard pattern>`: Exclude tests matching the pattern.
- `--prefix <prefix>`: Prefix all virtual entities, providing independent namespaces for otherwise identical test benches.
- `--stop-on-fail`: Stop execution after the first failed test.
- `--repl-on-fail`: Enter the interactive action REPL on the controller that caused a failed test. `--ignore-repl` suppresses this REPL as well.
- `--debug`: Pause after each successfully completed atomic action and wait for Enter before continuing. Container actions such as macros, blocks, `if`, and `for` do not add an extra pause around their nested actions.
- `--user`: On Linux/QEMU, run through the user's `qemu:///session` libvirt instance instead of requiring root and `qemu:///system`.
- `--assume-yes`: Do not ask for confirmation before running tests whose cache was invalidated.
- `--invalidate <wildcard pattern>`: Force cache invalidation for matching tests.
- `--report-folder </path/to/folder>`: Destination for generated reports.
- `--report-format <format>`: Select `allure`, `native_remote`, or `native_local` reporting.
- `--junit-report <path to JUnit report xml file>`: Write a JUnit XML report. This can be used together with the regular report formats. Cached and skipped Testo tests are represented as JUnit skipped cases.
- `--content-cksum-maxsize <Size in Megabytes>`: Maximum file size for content-based cache checks instead of modification-time checks.
- `--html`: Format standard output as HTML.
- `--nn-server <ip:port>`: Address of `testo-nn-server`. Default: `127.0.0.1:8156`.
- `--allowed-sharing-directory <path>`: **Mandatory.** Directory containing only files that may be sent to an untrusted NN server. If the NN server requests a reference image outside this directory, Testo rejects the request. Canonical paths are checked, so `..` and symlink escapes are not allowed.
- `--hypervisor <hypervisor type>`: Select the hypervisor backend. QEMU is the supported Linux backend; Hyper-V support is experimental on Windows.
- `--log-level <log level>`: Select the interpreter log level (`info` or `trace`).
- `--dry`: Perform parsing and semantic validation without executing tests.
- `--ignore-repl`: Ignore `repl` actions instead of entering interactive mode.
- `--disable-timestamps`: Omit UTC timestamps from per-action console log prefixes.
- `--skip-tests-with-repl`: Skip tests containing a `repl` action.
- `--needles <needles directory>`: Load current Testo needle pairs (`.png` + `.json`) from the specified directory. Needle regions can then be selected with `imgtag`; see [Needles](Needles.md).
- `--record-tests`: Record the graphical execution of the selected test plan. With the default `native_local` report format, one VP9 WebM mosaic is written to the current launch directory. Multiple VMs are tiled into the same recording. Recording uses the system `ffmpeg` executable instead of loading FFmpeg libraries into Testo, avoiding FFmpeg SONAME/ABI coupling. Without a report folder the option is accepted but no recording file is persisted, matching current Testo behavior.
- `--bootstrap-file </path/to/testo_file>`: Load an additional Testo script before the main input. Its declarations, macros, parameters, and tests are available to the main script; bootstrap tests are not selected as root tests, but can run when referenced as parents/dependencies.
- `--export <path to destination>`: After a successful run, export the selected test environment using the current Testo state-container v1 format. A destination ending in `.zip` creates a ZIP64/deflate archive; any other path creates a directory container. VM definitions, snapshots, storage/external files, metadata, virtual networks, and flash drives are included.
- `--export-on-fail <path to destination>`: Export the environment of each failed test attempt at the failure point. Running VMs are paused and a temporary snapshot named after the failed test is included in the container, including VM memory when applicable; the temporary snapshot is removed from local Testo state after export. With retries, every failed attempt updates the same destination, matching current Testo behavior. If several tests fail, the destination represents the most recent failed test. Unlike current Testo 15, replacement is staged and atomic, so stale files from an earlier failure are not left in a directory container. `.zip` destinations use the same ZIP64 format as `--export`.
- `--repeat-failed <repeat number>`: Retry each failed test up to the specified number of additional attempts. `--stop-on-fail` takes precedence and disables retries.

In Linux user mode Testo stores its state under `$HOME/.local/share/libvirt/testo` and logs under `$HOME/.local/state/testo`.

**Return values**

- `0` — all queued tests completed successfully.
- `1` — at least one queued test failed.
- `2` — syntax, semantic, configuration, or runtime setup error.

### Entity clean mode

SYNOPSIS

```text
testo clean [--user] [--item <item name>...] [--prefix <prefix>] [--assume-yes] \
  [--hypervisor <hypervisor type>] [--log-level <log level>]
```

- `--item <item name>...`: Clean only the specified logical Testo entity names. Multiple names may be supplied.
- `--prefix <prefix>`: Clean only Testo-managed entities with the specified prefix.
- `--user`: Clean entities from the user's libvirt session and user-state directory.
- `--assume-yes`: Erase matching Testo entities without an interactive confirmation.

Running `testo clean` without a prefix cleans only Testo-managed entities without a prefix. Manually created entities are not selected by this mechanism.


### State import mode

SYNOPSIS

```text
testo import <path container> [--user] [--force]
```

- `path container`: A Testo state-container v1 directory or `.zip` archive, including containers created by current Testo 15.
- `--user`: Restore into the user's `qemu:///session` environment.
- `--force`: Allow replacement of conflicting VM/storage/external files. Existing identical external files do not require `--force`.

Import validates the archive paths, complete manifest, and destination conflicts before modifying Testo-managed state. ZIP extraction rejects absolute paths, `..` escapes, and non-regular entries. Storage paths embedded in libvirt machine/snapshot XML are rewritten to the target Testo storage pool, so user-mode containers can move between different home directories. In QEMU user mode, virtual networks are restored in system libvirt while VMs are restored in the user session and attach to the network bridges, matching current Testo behavior.

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
