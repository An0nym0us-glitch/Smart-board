# The `.classboard` lesson format

A lesson is a single binary file made of a small, versioned container that holds a JSON
document and the image assets.

## Container (version 1)

All integers are big endian and strings use Qt `QDataStream` (`Qt_5_15`) encoding.

```text
"CLASSBRD"            8 bytes magic
quint32               container version (1)
quint32               entry count
entry × count:
    QString           name
    quint8            flags (bit 0: zlib compressed with qCompress)
    quint32           CRC-32 of the *uncompressed* data
    QByteArray        data
```

The reader rejects files with a wrong magic number, a truncated stream, a newer container
version or a CRC mismatch, so a damaged file is reported instead of loading as garbage.

| Entry | Content |
|---|---|
| `document.json` | the lesson (compressed) |
| `images/<sha1>.<ext>` | original encoded image bytes, stored once per unique image |

## Document JSON

```json
{
  "format": "classboard",
  "formatVersion": 1,
  "application": "ClassBoard 1.0.0",
  "metadata": { "title": "", "author": "", "created": "ISO-8601", "modified": "ISO-8601" },
  "coordinates": { "m": [m11, m12, m21, m22, dx, dy], "unit": "cm" },
  "defaultTemplate": { ...template... },
  "currentPage": 0,
  "pages": [ { "id": "uuid", "name": "", "size": [1920, 1080], "template": { ... }, "objects": [ ... ] } ]
}
```

`coordinates` is the page → math transform: by default the origin is at the page centre, one
unit is 40 px, and the y axis points up.

### Templates

`{ "id", "name", "kind": "blank|grid|dots|ruled|graph|music|coordinate|image", "background",
"line", "accent" (#AARRGGBB), "spacing", "major", "image": "<sha1>" }`

### Objects

Every object has `type`, `id`, `pos` `[x, y]` (page coordinates of the local origin) and an
optional `rot` in degrees. Geometry is stored in the object's local frame. Point lists are
flat arrays `[x0, y0, x1, y1, …]`.

| type | properties |
|---|---|
| `stroke` | `pts`, optional `pr` (pressures 0–1), `color`, `width`, `style` (`pen/highlighter/dashed/dotted`), `pressure` |
| `shape` | `kind`, `stroke`, `width`, `fill`, `dashed`; box shapes: `size`, `sides`; lines: `p1`, `p2`; free polygon: `pts` |
| `text` | `text`, `width`, `family`, `size`, `bold`, `italic`, `underline`, `color`, `align` |
| `equation` | `latex`, `size`, `color` |
| `image` | `image` (asset key), `size` |
| `graph` | `size`, `window` `[xMin, yMin, width, height]`, `grid`, `functions` `[{expr, color, visible}]`, `parameters` `[{name, value, min, max}]`, `points` `[{x, y, label}]`, `vectors` `[[x0, y0, x1, y1]]` |
| `table` | `rows`, `cols`, `cells` (row-major strings), `cellSize`, `fontSize`, `header`, `color` |
| `geometry` | `kind` (`point/segment/line/ray/vector`), `pts`, `color`, `label`, `name` |
| `measurement` | `kind` (`distance/angle/slope/area`), `pts`, `color` |

Unknown object types are skipped (and reported) rather than failing the whole load. This lets
an older ClassBoard open files that contain objects added in a later version.

## Versioning and migration

`formatVersion` identifies the JSON schema. On load, `ProjectSerializer::migrate` upgrades
older documents one version at a time. For example, version 0 stored page content under
`items`, and the migration renames it to `objects`. When a schema changes:

1. increase `ProjectSerializer::kFormatVersion`,
2. add a `case` in `migrate()` that converts the previous version,
3. add a test in `tests/tst_serialization.cpp`.

## Safety

* Saves write to a temporary file and rename it over the original only after a complete
  write (`QSaveFile`). An interrupted save never damages the previous file.
* Recovery copies (autosave) use the same format and live in the application data folder
  under `recovery/`.
