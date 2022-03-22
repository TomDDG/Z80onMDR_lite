# Z80onMDR_lite

Z80onMDR_Lite - Simple ZX Spectrum Z80 snapshot to Microdrive MDR image converter

    v1.0  initial release based on v1.9b of Z80onMDR
    v1.1  improved loading speed on real hardware by interleaving files
    v1.1a further improvements to loading speed by adding space between the files 
          up to 8x improvement in loading speed compared to v1.
          Example loading times for Chase HQ 128k version (converted z80 snapshot)
            v1.0  - 2mins 40secs
            v1.1  - 1min 30secs
            v1.1a - 20secs
    v1.2  new 3 stage launcher to remove screen corruption (added -o option to still use old launcher)
    v1.21 improved large delta speed and attr matching added for no gap 3 stage launcher
    v1.22 improved gap finder, v1.23 bug fixe for thing I broke in v1.22
    v1.3 handle stack in screen for better compatibility
    v1.31 better stack handling

A cut down version of the full Z80onMDR https://tomdalby.com/other/z80onmdr.html 
to use with or within other utilities under the GPL3.0 licence. Only converts
a single Z80 snapshot with minimal output and no options. Works with both 48k &
128k snapshots.

    Copyright (C) 2021 Tom Dalby
 
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

