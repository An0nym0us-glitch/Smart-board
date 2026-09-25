# ClassBoard architecture

ClassBoard is organised in small modules that each have one job. Only `app/` knows about all
of them.

```text
            ┌────────────────────────── app/ ───────────────────────────┐
            │ MainWindow · LessonController · ExportController ·        │
            │ PopoverController · AppSettings · AppServices             │
            └──────┬───────────────┬──────────────┬──────────────┬──────┘
                   │               │              │              │
               ui/ (ribbon,    canvas/ ──── tools/ ────── geometry/ (instruments)
               popovers,         │  ▲          │
               theme)            │  │ input/   │
                   │             ▼  │          ▼
                   └──────────► document/ ◄── math/ · graph/
                                   │
                          storage/ · export/
```

## Document model (`document/`)

```text
Document ── pages: [Page]                 ── ImageStore (shared, deduplicated assets)
   │           └─ objects: [DocumentObject] (z-order)
   │                 Stroke · Shape · Text · Equation · Image · Graph · Table
   │                 Geometry (point/segment/line/ray/vector) · Measurement
   └─ CommandStack (the only undo/redo history)
```

* Every object keeps its geometry in a local frame centred on the origin. A transform
  (translation + rotation) places it on the page. Resizing, rotating, control points,
  hit testing and serialisation are virtual methods on `DocumentObject`.
* All changes go through `Command`s: `AddObjects`, `RemoveObjects`, `ModifyObjects`
  (before/after clones), `ReplaceObjects` (eraser splitting), `ReorderObject`,
  `InsertPage`, `RemovePage`, `MovePage` and `ModifyPage`. Tools never keep their own
  undo stack. Interactive edits such as dragging change the object live, then record one
  command with `pushApplied`.
* Clones share large data. Stroke points use `QVector`'s implicit sharing and images live
  in the `ImageStore`, so an undo snapshot costs very little memory.
* `Document` emits fine-grained signals (`objectAdded/Removed/Changed` with bounds). The
  canvas uses them to repaint only the affected region.

## Rendering (`canvas/`)

* `PageRenderer` draws a page (template background plus objects). The canvas, thumbnails
  and every exporter use it, so what you see is what you export.
* `CanvasWidget` keeps a cached pixmap of the committed content with a dirty region.
  Changes repaint only their bounds. Panning scrolls the cache and renders only the newly
  exposed strips. Pinch and wheel zoom show a scaled preview of the cache and re-render
  once zooming settles.
* Live content is drawn as an overlay on top of the cache: ink in progress, tool previews,
  selection handles and instruments.
* Templates are procedural and cover an infinite canvas. Grids fade out when they would be
  too dense on screen. The nominal 16:9 page frame is only used for export and "fit".

## Input (`input/`)

```text
QMouseEvent / QTabletEvent / QTouchEvent
        │
   InputManager ── pointer events (id, device, phase, pressure) ──► instruments / ToolController
        │
        └──────── gesture events (pan/zoom/rotate, palm erase) ────► canvas / instrument / tool
```

* Mouse events that the OS or Qt synthesises from touch and pen are ignored, because those
  devices are handled natively.
* **Touch state machine.** One finger draws. A second finger that arrives while the stroke is
  still young (under 280 ms or 7 mm of movement) cancels the stroke and starts pan/zoom. If
  the stroke is older, the extra finger is ignored so it isn't ruined. Four or more fingers
  landing close together within 450 ms, or one very large contact, start *palm erase*: one
  grouped gesture that becomes one undo step. An optional multi-user mode lets every finger
  draw on its own.
* Every pointer belongs to the tool that received its *down* event, so switching tools in the
  middle of a stroke is safe. The stylus eraser end is routed to the eraser automatically.

## Tools (`tools/`) and instruments (`geometry/`)

Tools implement a small interface: pointer down/move/up/cancel, hover, overlay painting,
keys and gestures. They work through the `ToolHost` interface, never directly with widgets.
The tools are pen, eraser, select, shape, text (inline editor), measure, construct and graph.

Instruments (ruler, protractor, set square, compass) are overlays. They are not document
content, just like a physical ruler lying on the board. Each one exposes handles and *edge
constraints*: ink started near an edge is projected onto that straight line or arc. The
compass and the protractor's stamp button produce document objects.

## Mathematics (`math/`, `graph/`)

* `CoordinateSystem` maps page coordinates to mathematical units. One global system serves
  the coordinate-plane template and all measurements. A `GraphObject` provides its own
  system, and `resolveCoordinateSystem` picks the right one for a set of points. As a
  result, measurements, points and vectors placed inside a graph use graph units.
* `math::Expression` compiles expressions such as `a·sin(bx)+c`, `2x²`, `|x|` and `√x` into
  a stack program for fast plotting. It handles implicit multiplication and splits
  multi-letter parameters.
* `mathtype::MathTypesetter` parses a LaTeX subset into a box tree (rows, fractions,
  radicals, scripts, limits, stretchy delimiters, matrices, accents). It paints with
  QPainter text and paths, so formulas stay vector in PDF export.

## UI (`ui/`)

* `Theme` is data driven (`resources/themes/dark.json`). Colours and metrics are named roles,
  and every size goes through `dp()` so the interface scales for 1080p, 1440p and 4K. The
  scale is chosen automatically and can be adjusted in Settings.
* `Popover` / `PopoverHost` form the single popover system: child widgets (never separate
  windows) with a rounded bubble, a soft shadow, a notch pointing at the anchor, a fade/slide
  animation, a back stack for nested panels, outside-tap dismissal, edge-aware placement and
  touch scrolling. Every panel in `ui/popovers/` is plain content placed inside it.
* Confirmations (`ConfirmOverlay`) and notifications (`Toast`) are drawn inside the window.
  Native dialogs are used only to pick files.

## Storage and export

* `.classboard` files use a CRC-checked container: compressed JSON document plus raw image
  assets. JSON documents carry a `formatVersion`, and `ProjectSerializer::migrate` upgrades
  older documents step by step. See [FILE_FORMAT.md](FILE_FORMAT.md).
* Saves are atomic (`QSaveFile`), and serialising and writing run on a worker thread.
* Autosave writes a recovery copy on a worker thread while there are unsaved changes. Each
  running session holds a lock file. On start-up, recovery copies whose lock is stale are
  offered for restore.
* Exports run on worker threads from immutable `DocumentSnapshot`s, with progress shown in
  the toast. PDF output is vector with real text. PPTX output is one high-resolution picture
  per 16:9 slide; PowerPoint cannot represent ink or formulas natively, so this keeps them
  looking exactly right.

## Memory and performance

* Images are decoded at most once, limited to 4096 px per side, and never re-encoded when
  saving. Down-scaled levels of detail are created on demand for the canvas.
* The render cache is one screen-sized pixmap (about 33 MB at 4K).
* The command history is limited to 300 steps, and clones share data.
* Heavy work (saving, autosave, export, PDF conversion) never runs on the GUI thread.
