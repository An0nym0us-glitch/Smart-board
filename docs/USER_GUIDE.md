# ClassBoard user guide

## The board

ClassBoard opens on a clean blackboard with the pen ready. The ribbon at the bottom holds:

`MORE · PEN · ERASER · SELECT · (active tool) · UNDO · REDO · ‹ page › · + PAGE · zoom · full screen`

* Tap **PEN**, **ERASER** or **SELECT** to switch tools. Tap the active tool again to open its
  options. A small caret on the button shows that options are available.
* **MORE** opens every other tool: shapes, geometry, equations, graphs, instruments, images,
  text, tables, templates, import/export and settings. When you pick one of these tools, it
  appears as an extra ribbon button. Tap that button to reopen the tool's options.
* Tap the **page indicator** (`3 / 12`) to open the page navigator.
* Tap the **zoom percentage** to fit the page to the screen again.
* Tap anywhere outside a popover to close it.

## Touch, pen and mouse

| Action | Touch | Pen | Mouse |
|---|---|---|---|
| Draw / use tool | one finger | pen tip | left button |
| Pan | two fingers | – | right or middle drag, Shift + wheel |
| Zoom | pinch | – | wheel |
| Erase | four fingers or flat hand, wipe | pen eraser end | – |

* **Palm erase** works in every tool and counts as a single undo step. You can turn it off in
  Settings.
* **Several people drawing** (Settings): every finger draws its own line and two-finger zoom
  is disabled. Use this when two students write at the board together.
* A second finger that touches while a line is still being started cancels that line and
  starts zooming. Once a line is well under way, other touches don't interrupt it.

## Tools

* **Pen.** Styles: pen, highlighter, dashed and dotted. You can set thickness and colour,
  turn stylus pressure on or off, and enable *shape recognition*. With recognition on,
  roughly drawn lines, circles, rectangles and triangles become clean shapes.
* **Eraser.** *Stroke* removes whole lines, *Object* removes anything you touch, and *Area*
  rubs out exactly the ink under the eraser. *Clear page* asks for confirmation first, and you
  can undo it.
* **Select.** Tap an object, drag a rectangle, or draw a lasso. Drag to move. Corner handles
  resize and the round handle rotates (it snaps to 15°). Line and polygon points can be
  dragged individually. The floating bar offers edit, colour, front/back, duplicate, copy and
  delete. Double-tap text, formulas, graphs or tables to edit them.
* **Shapes** (MORE → Shapes). Tap a shape, then drag on the board, or just tap for a default
  size. Hold Shift for squares, circles and 45° lines. To draw a free polygon, tap its corners
  and then tap the first corner again (or double-tap, or press Enter).
* **Text** (MORE → Text). Tap the board and type. Size, bold/italic/underline, alignment and
  colour apply to the text you're editing or to the selected text boxes.

## Geometry

* **Instruments.** The ruler, protractor, set square and compass lie on the board:
  * drag the body to move an instrument;
  * drag the round arrow to rotate it (it snaps to 15°), or use two fingers to move and rotate
    at once;
  * tap × to put it away.
  * **Ruler and set square.** Start drawing with the pen near an edge and the line follows
    that edge exactly. The length is shown while you draw. Drag the tab at the end of the
    ruler to change its length.
  * **Protractor.** Drag the blue and orange knobs to measure an angle. The ∠ button stamps
    the measured angle onto the page.
  * **Compass.** Drag the pencil to draw an arc or a full circle. Drag the hinge to change the
    radius, and drag the needle leg to move it.
* **Measurements.** Distance, angle, slope and area. The values update when you move the
  points with Select.
* **Constructions.** Points (named A, B, C … and labelled with coordinates), segments, lines,
  rays and vectors. They snap to existing points and, on grid backgrounds or inside graphs,
  to whole coordinates.
* **Coordinates.** *Coordinate plane background* puts numbered axes on the page. The unit is
  1 cm = one grid square.

## Equations

MORE → Equation. Type LaTeX-style input or tap the palette, and the preview updates live.
Examples: `\frac{a}{b}`, `x^{2}`, `x_{n}`, `\sqrt{x}`, `\sqrt[3]{x}`, `\int_{0}^{1} f(x)\,dx`,
`\sum_{i=1}^{n} i`, `\lim_{x \to 0}`, `\vec{v}`, `\begin{pmatrix} a & b \\ c & d \end{pmatrix}`,
and Greek letters such as `\alpha` or `\pi`. Double-tap a formula to edit it.

## Graphs

MORE → Function / Graph → *Insert graph*.

* Type functions such as `2x + 1`, `a*sin(b x) + c`, `x² − 4`, `|x|`, `sqrt(x)` or `f(x) = e^x`.
* Letters other than x become **parameters**, each with its own slider.
* Drag inside the graph to pan and pinch to zoom. You can also use the zoom buttons.
* *Tap to place points* adds points that snap onto curves. Tap a point again to remove it.
* Add vectors by typing their components, and create value tables for the visible functions.
* Measurements and constructions placed inside a graph use the graph's units.

## Pages

* **+ PAGE** adds a page after the current one.
* In the page navigator you can tap a thumbnail to open it, drag thumbnails to reorder them
  (or use the ◀ ▶ buttons), and duplicate, delete, rename or change the background of a page.
* **Templates** (MORE → Templates) apply to this page, all pages, or new pages. *Background
  from image* and *Save this background* create custom templates that every lesson can use.

## Files

* **MORE → Lesson** has new, open, save, save as and recent lessons. Lessons are
  `.classboard` files.
* **Autosave** keeps a recovery copy while there are unsaved changes. The interval is set in
  Settings. If ClassBoard or the computer stops unexpectedly, the next start offers to restore
  the lesson.
* **Import.** Images (or drag them onto the board, or paste), pages from another lesson, and
  PDF documents. Each PDF page becomes a board page you can write on.
* **Export.** PDF (current page, whole lesson or a page range such as `1-3, 5`), PowerPoint
  (one slide per page) and PNG of the current page.

## Keyboard shortcuts

| Keys | Action |
|---|---|
| Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) | undo / redo |
| Ctrl+S / Ctrl+Shift+S / Ctrl+O / Ctrl+N | save / save as / open / new lesson |
| Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+D | copy / cut / paste / duplicate |
| Ctrl+A, Delete | select all, delete selection |
| Arrow keys (Shift for bigger steps) | nudge selection |
| Page Up / Page Down, Ctrl+M | previous / next page, new page |
| Ctrl+0, Ctrl++ / Ctrl+− | fit page, zoom in / out |
| P, E, V, T | pen, eraser, select, text |
| F11 | full screen |
| Esc | close popover / clear selection / finish editing |
