unpackmp2 - lossless transformation of MPEG audio Layer II data
Copyright (C) 2009 Michael Henke
This is free software under GNU GPL v3, http://www.gnu.org/copyleft/gpl.html
------------------------------------------------------------------------------



introduction
------------
unpackmp2 is a filter that performs a lossless transformation of MPEG audio
Layer II data (mp2) into an unpacked format and back into mp2 format.

It is intended to be used as a preprocessor for lossless compression programs
in order to achieve a better compression of mp2 files.




how to use
----------
unpackmp2 is a filter program which reads input data from "stdin" and writes
output data to "stdout". Use standard I/O redirection to read from or write to
files or to pipe data from/to other programs ("<" or ">" or "|").

The compressor program LPAQ8 and some Windows command scripts are included
for convenience:

compressMP2LPAQ8.cmd   - compress "mp2" file into "um2.lpaq8" file
decompressMP2LPAQ8.cmd - decompresss "um2.lpaq8" file into "um2.lpaq8.mp2" file

lpaq8 file compressor (C) 2007, Matt Mahoney, Alexander Ratushnyak
http://mattmahoney.net/dc/




test results
------------
test1.mp2 (125.8 MB, 54min 57sec, 320kbps, 48kHz, stereo, source: DVB-S radio)

program            options            compressed size     unpackmp2 gain
-----------------  -----------------  ------------------  ---------------------
unpackmp2 | lpaq8  5                  110859854 = 84.0%   12489717 (11.9 MiB)
unpackmp2 | 7-Zip  7z LZMA ultra      117928435 = 89.4%   8262710  (7.9 MiB)
unpackmp2 | 7-Zip  7z Bzip2 ultra     119080485 = 90.3%   7493074  (7.1 MiB)
unpackmp2 | 7-Zip  7z PPMd ultra      120598552 = 91.4%   4933122  (4.7 MiB)
unpackmp2 | 7-Zip  Zip Deflate ultra  120718098 = 91.5%   6127333  (5.8 MiB)
lpaq8              5                  123349571 = 93.5%   (n/a)
7-Zip              7z PPMd ultra      125531674 = 95.2%   (n/a)
7-Zip              7z LZMA ultra      126191145 = 95.7%   (n/a)
7-Zip              7z Bzip2 ultra     126573559 = 96.0%   (n/a)
7-Zip              Zip Deflate ultra  126845431 = 96.2%   (n/a)
(original mp2)     (n/a)              131912640 = 100.0%  (n/a)
unpackmp2          (n/a)              202374495 = 153.4%  -70461855 (-67.2 MiB)
-----------------  -----------------  ------------------  ---------------------




license
-------
unpackmp2 - lossless transformation of MPEG audio Layer II data
Copyright (C) 2009 Michael Henke

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
