# Third-Party Notices

p-radials includes and links against third-party software. Each dependency
remains subject to its own license terms.

## CommonLibSSE-NG

Copyright (c) the CommonLibSSE-NG contributors.

Source: <https://github.com/alandtse/CommonLibSSE-NG>

Licensed under GNU General Public License version 3 or later, with the Modding
Exception and GPL-3.0 Linking Exception (with Corresponding Source). The full
license and exceptions are included in the CommonLibSSE-NG submodule under
`COPYING` and `EXCEPTIONS.md`.

## Dear ImGui

Copyright (c) 2014-2026 Omar Cornut.

Source: <https://github.com/ocornut/imgui>

Licensed under the MIT License:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## NanoSVG

Copyright (c) 2013-2014 Mikko Mononen.

Source: <https://github.com/memononen/nanosvg>

p-radials uses NanoSVG and NanoSVGRast to parse and rasterize SVG icon assets
in memory. NanoSVG is distributed under the following permissive license:

> This software is provided 'as-is', without any express or implied warranty.
> In no event will the authors be held liable for any damages arising from the
> use of this software. Permission is granted to anyone to use this software
> for any purpose, including commercial applications, and to alter it and
> redistribute it freely, subject to the condition that this notice is not
> removed or altered from any source distribution.

The complete notice is also available in [licenses/NanoSVG.txt](licenses/NanoSVG.txt).

## JsonCpp

Copyright (c) 2007-2010 Baptiste Lepilleur and The JsonCpp Authors.

Source: <https://github.com/open-source-parsers/jsoncpp>

JsonCpp is public domain where that status is recognized. In jurisdictions
where it is not, it is made available under the MIT License. p-radials uses it
to parse optional Inventory Interface Information Injector (I4) JSON rules.

The complete notice is available in [licenses/JsonCpp.txt](licenses/JsonCpp.txt).

## zlib

Copyright (C) 1995-2022 Jean-loup Gailly and Mark Adler.

Source: <https://zlib.net/>

p-radials uses zlib to decompress SWF data in memory for its optional I4 icon
bridge. zlib is distributed under the zlib License.

The complete notice is available in [licenses/zlib.txt](licenses/zlib.txt).

## SkyUI Icons

Some icons used by p-radials were originally created for SkyUI.

Copyright belongs to the original SkyUI icon author Psychosteve.

These assets are not licensed under p-radials' GPL-3.0-or-later license and
remain subject to the original SkyUI permissions. Modifying or redistributing
the original SkyUI icon files may require permission from their authors.

SkyUI: https://www.nexusmods.com/skyrimspecialedition/mods/12604

## Inventory Interface Information Injector compatibility

p-radials can read I4-compatible JSON rules and user-installed SWF icon
sources. Those rules and assets are not included with p-radials and remain
subject to the license and permissions of the mod that provides them.
