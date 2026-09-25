# Building and packaging ClassBoard

## Windows 10/11 (recommended target)

1. Install **Visual Studio 2022** (or 2019) with the *Desktop development with C++* workload.
2. Install **Qt 5.15.2** for `MSVC 2019 64-bit` with the Qt online installer. `aqtinstall`
   also works:
   ```powershell
   pip install aqtinstall
   aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 -O C:\Qt
   ```
3. Install **CMake** (3.16 or newer) and, optionally, **Ninja**.
4. Open an *x64 Native Tools Command Prompt for VS* and run:
   ```bat
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019_64
   cmake --build build
   set QT_QPA_PLATFORM=offscreen
   ctest --test-dir build --output-on-failure
   ```
   With the Visual Studio generator, use `cmake -S . -B build -A x64 ...` and then
   `cmake --build build --config Release`.
5. Run `build\bin\ClassBoard.exe`. To run it outside the build tree, copy the Qt runtime next to
   it with `windeployqt`, or configure with `-DCLASSBOARD_DEPLOY_QT=ON`. With that option,
   every build runs `windeployqt` for you.

### Installer and portable ZIP

```bat
cmake --build build --target package
```

CPack always builds a portable ZIP. When NSIS is installed it also builds an installer, which
registers the `.classboard` file type. The install step runs `windeployqt` on the installed
executable.

### Import requirements

Everything works offline; nothing is uploaded anywhere.

* **PDF import.** If Qt was built with the Qt PDF module (`Qt5Pdf`), CMake finds it and
  ClassBoard renders PDFs itself. Otherwise ClassBoard uses Poppler's `pdftoppm`, found
  on `PATH` or in a `poppler\bin` (or `poppler\Library\bin`) folder next to
  `ClassBoard.exe`. The Windows build produced by CI ships Poppler in `bin\poppler\bin`; for
  your own installer configure with `-DCLASSBOARD_POPPLER_DIR=<folder with pdftoppm.exe>`.
  Without a backend the PDF import buttons are disabled and explain why.
* **PowerPoint import (.ppt, .pptx).** Slides are converted to PDF by an installed office
  suite and then rendered like a PDF (so PDF import must be available too):
  * Microsoft PowerPoint (Windows) through its automation interface, or
  * LibreOffice (free, any platform) in headless mode — found on `PATH`, in
    `C:\Program Files\LibreOffice\program`, or in a `libreoffice\program` folder next to
    ClassBoard.
  Without either, the import explains what to install.

### Optional components
* **Recogniser plug-ins.** Offline handwriting-to-formula recognisers are Qt plug-ins placed in
  `plugins\recognizers` next to the executable. See `src/ai/Recognition.h`.

## Linux (development)

```bash
sudo apt install qtbase5-dev qtbase5-dev-tools libqt5svg5-dev cmake ninja-build
# optional, for PDF / PowerPoint import and their tests:
sudo apt install poppler-utils libreoffice-impress
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Continuous integration

`.github/workflows/build.yml` builds and tests on Windows (MSVC, Qt 5.15.2) and Linux on every
push, with Poppler and LibreOffice installed so the import tests run too. It uploads a
ready-to-run Windows folder (including Poppler) as a build artifact.

## Diagnostics

* `ClassBoard --screenshot out.png` renders the window after start-up and exits.
* `ClassBoard --ui-scale 1.5` overrides the interface size.
* Set `CLASSBOARD_SCREENSHOTS=<dir>` when running `tst_ui`, `tst_tools_ui` or `tst_board_ui`
  to get a screenshot of every popover and tool.
