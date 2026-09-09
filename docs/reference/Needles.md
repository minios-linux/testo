# Needles

Needles are reusable image regions selected by semantic tags. The interpreter loads them from a directory specified by `--needles`.

Each needle consists of two files with the same basename: a PNG screenshot and a JSON metadata file. The JSON root may be an object or an array. Each region contains the tags that refer to it, its rectangle inside the PNG, and the image matching threshold.

```json
{
  "login": {
    "tags": ["login-button", "primary-action"],
    "xpos": 420,
    "ypos": 310,
    "width": 180,
    "height": 48,
    "match": 0.90
  }
}
```

`tags` may be a single string or an array of strings. `xpos` and `ypos` are the top-left coordinates of the region. `width` and `height` must be positive. `match` is a numeric matching threshold; values between 0 and 1 are normally used, and a higher value requires a closer visual match.

Use a tag in screen checks:

```testo
wait imgtag "login-button" timeout 30s
check imgtag "login-button"
```

Needle tags are also valid mouse destinations and support the regular image/text positioning specifiers:

```testo
mouse click imgtag "login-button".from_top(0).center()
```

If several regions carry the same tag, all of them are searched. A plain mouse destination must resolve to exactly one screen match; use `from_top`, `from_bottom`, `from_left`, or `from_right` to choose one when several matches are possible.

Generated region images are kept only for the duration of the interpreter process and are created inside `--allowed-sharing-directory`, so the existing NN-server sharing boundary also applies to needles.
