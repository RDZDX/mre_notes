# mre_notes

A MediaTek MRE notes app for `E:\Notes`, using the MRE APIs and editor/input conventions from [RDZDX/mre_editor](https://github.com/RDZDX/mre_editor), with a selectable file-browser UI patterned after the supplied Стас Дагестанцев `abook.cpp` example.

## Features

- Opens the `E:\Notes` directory and creates it when missing.
- Lists `.txt` notes; selecting a row and pressing **OK** opens it in the MRE text-input editor.
- If there are no notes at startup, immediately starts the **Create new** flow.
- **Options** menu: Create new, Rename, Delete.
- New notes and renamed notes use `.txt` when the extension is omitted. Note bodies are read and written as UTF-8.
- Supports keypad navigation and tap selection.

![alt text](https://rdzdx.github.io/mre_notes/picture.jpg)
