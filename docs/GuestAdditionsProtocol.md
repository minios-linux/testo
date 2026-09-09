# Guest Additions protocol

This document records the Guest Additions wire protocol implemented by the open-source fork. It is a maintenance reference for the public host and local CLI channels.

## Framing and common envelope

Host requests and normal guest responses are JSON frames. Each JSON frame is prefixed by a 32-bit byte length, followed by exactly that many UTF-8 JSON bytes. Current x86 QEMU interoperability uses little-endian framing.

Every request sent by the host protocol library contains `version` in addition to `method`. Each normal response sent by Guest Additions also contains its own `version`.

Typical request:

```json
{"method":"get_tmp_dir","version":"3.6.8"}
```

Typical successful response:

```json
{"success":true,"result":{"path":"/tmp"},"version":"3.6.8"}
```

For protocol versions before 2.2.10, `error` is plain text. From 2.2.10 onward it is a base64-encoded, NUL-terminated error string. File streaming changed at 2.2.8: older peers embed base64 data in JSON; newer peers use raw 64-bit-length-prefixed payloads after the JSON metadata frame.

The guest reads the complete four-byte JSON length prefix before decoding it, so fragmented stream reads do not desynchronize the channel.

## Supported host-channel methods

| Method | Request payload | Successful response / following data |
| --- | --- | --- |
| `check_avaliable` | no args | `result: {}` |
| `get_tmp_dir` | no args | `result.path` |
| `get_file_info` | `args.path` | `result.size` as a 64-bit-capable JSON integer |
| `copy_file` | `args[]` entries with destination `path` and `content` | `result: {}`; from 2.2.8, `content: null` is followed by raw `uint64` length + bytes |
| `copy_files_out` | positional `args: [src, dst]` | metadata array, then one raw `uint64` length + byte stream per file from 2.2.8 |
| `execute` | positional command in `args[0]`, optional `vars` object | zero or more `status: pending` frames, then one `status: finished` frame |
| `mount` | `folder_name`, `guest_path`, `permanent` | top-level `was_indeed_mounted` |
| `get_shared_folder_status` | `folder_name` | `result.name`, `result.is_mounted`, and `result.guest_path` when mounted |
| `umount` | `folder_name`, `permanent` | top-level `was_indeed_umounted` |

`get_file_info` accepts only an absolute path to a regular file. Missing paths, relative paths and directories are errors. This is the metadata preflight used by `remotefile` before any attachment data is downloaded.

For `copy_files_out`, directory entries contain only `path`. File entries contain `src`, `path`, `content`, and on the streaming protocol `content_size`. The `dst` argument is host-side destination metadata; the guest does not write to that host path.

The streaming size field is 64 bit. Values above 4 GiB are preserved in `get_file_info.size`, `copy_files_out.content_size`, and the following raw transfer length.

## Execute streaming

On Linux, Guest Additions executes the requested command with `2>&1`, so stderr and stdout share the same stream. While the process runs, output is delivered as frames shaped like:

```json
{"success":true,"result":{"status":"pending","stdout":"<base64 NUL-terminated bytes>"}}
```

The final frame has `status: "finished"`, integer `exit_code`, and the updated variable collection in `vars`. Host-side `exec expect` is evaluated only after a zero exit code and searches the combined streamed output with an ECMAScript regular expression.

## Local CLI-channel methods

The local `/var/run/testo-guest-additions.sock` handler additionally exposes `set_var` and `get_var`. They share the same execution variable context as host `execute` calls.

| Method | Request | Successful response |
| --- | --- | --- |
| `set_var` | `var_name`, `var_value`, `global` | `success: true` |
| `get_var` | `var_name` | top-level `var_value` |

These methods are intended for `testo-guest-additions-cli`; they are not additional host actions. The CLI forwards the requested variable name unchanged for `get <var_name>`.

## Deferred desktop/input methods

`active_window` and `record` belong to the deferred desktop/input stack rather than the core Guest Additions host-action protocol. `active_window` reports the active window geometry, while `record` is a long-lived input-capture stream.

## Validation

The supported core is covered by fake-channel/unit protocol tests, local PTY framing tests, real QEMU/libvirt virtio-channel VM smoke tests, and large-transfer size checks.

An interrupted raw `copy_files_out` transfer does **not** automatically resynchronize the stream. The remaining payload must be drained or the guest channel restarted before starting a new framed request.
