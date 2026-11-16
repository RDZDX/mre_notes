# mre_notes

A MediaTek MRE notes app for `E:\Notes`, using the MRE APIs and editor/input conventions from [RDZDX/mre_editor](https://github.com/RDZDX/mre_editor), with a selectable file-browser UI patterned after the supplied `abook.cpp` example.

## Features

- Opens the `E:\Notes` directory and creates it when missing.
- Lists `.txt` notes; selecting a row and pressing **OK** opens it in the MRE text-input editor.
- If there are no notes at startup, immediately starts the **Create new** flow.
- **Options** menu: Create new, Rename, Delete.
- New notes and renamed notes use `.txt` when the extension is omitted. Note bodies are read and written as UTF-8.
- Supports keypad navigation and tap selection.

## Build

Open `mre_notes.c` and `mre_notes.def` in the same MediaTek MRE SDK setup used to build `mre_editor`. The SDK headers/libraries are supplied by the local MRE SDK; they are not vendored here. Configure the project with the SDK's normal MRE include paths and resource-packaging steps, using `config.xml` as package metadata.

The folder is intentionally fixed to `E:\Notes`. The implementation caps a note at 4,096 UCS-2 characters (and refuses files larger than 64 KiB before loading) to keep memory use appropriate for older devices.
