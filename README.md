# dskgen
Command line tool to create different types of .dsk files. (c) 2008-2015 Retroworks.

##What is a .dsk file?
A .dsk file is an image of a diskette that can be used in multiple emulators of machines such as Amstrad CPC or Spectrum +3, which used disks based on CPM formats.

##How does it work?
dskgen is used as a command line tool, so it will receive parameters that determine the type of diskette the image represents, and its contents.

The .dsk file this tool generates follows the Extended DSK specification, which can be found here:
http://cpctech.cpc-live.com/docs/extdsk.html

##Parameters

`-o, --outputFileName`   
(Optional) Output file name. Default value is `disk.dsk`.

`-b`
(Optional) Specifies the file that contains boot code por `|CPM` booting. Only valid for `SYSTEM` or `CUSTOM` disk types. Default value is empty.

`-c, --catalog ARG`
(Optional) Catalog type. Valid values are: `NONE`, `RAW` or `CPM`.
* `NONE`: No catalog is added. This can be used if sectors will be read in raw mode, or if the first file to add contains CATART data (as the first file will be written on the first available track).
* `CPM `: A standard CPM 2.2 catalog is created. This catalog is the one `AMSDOS` understands.
* `RAW `: A catalog is created in `RAW` mode (see spec below). This catalog is not understood by the OS. The catalog is created in the first available track (depends on the disk type), and its first sector. 

`-f, --files ARG1[;ARGn]`
(Optional) Specifies the files to insert. Each file is specified as `path`,`header`,`fileType`,`loadAddr`,`exeAddr`, and is separated from the rest using a semicolon. 
* `path`: (Mandatory) Source file path.
* `header`: (Optional) The header to add. Valid values are `NONE` or `AMSDOS`. If the header value is `AMSDOS`, the following values might be specified.
* `fileType`: (Optional) The file type. Valid values are `bas`,`bin`,`binp`,`scr`,`asc`, or empty.
  * `bas`: Basic file.
  * `bin`: Binary file.
  * `binp`: Binary protected file.
  * `scr`: Screen dump file.
  * `asc`: ASCII file.
* `loadAddr`: (Optional) The file loading address. Default value is `0x0`.
* `exeAddr`: (Optional) The memory location where execution will start when the file is loaded. Default value is `0x0`.
If no file is specified, an empty dsk will be created.

Example:`-f file1.bin,AMSDOS,0x100,0x500;file2.scr,NONE`

`-s, --sides`
Specifies the number of sides of the disk. Default value is 2.

`-t, --type`
Disk type. Valid values are `SYSTEM`, `DATA`, `IBM` and `CUSTOM`. See disk formats here: http://www.seasip.info/Cpm/amsform.html

`--initialSector`
Specifies the initial sector per track. Default value is `0xC1`. Only valid for `CUSTOM` disks.

`--initialTrack`
Specifies the initial track for data (except boot file). Default value is `0`. Only valid for `CUSTOM` disks.

`--sectors`
Specifies the number of sectors per track in the disk. Default value is `9`. Only valid for `CUSTOM` disks.

`--tracks`
Specifies the number of tracks in the disk. Default value is `80`. Only valid for `CUSTOM` disks.

`--config <configFile>`
Configuration file with all options and files specified in `JSON` format.

`--help`
Help. Show usage.

###Configuration JSON format:

The JSON format is as follows:

```
{
    "sides": NumSides,
    "catalog": "none|cpm|raw",
    "diskType": "system|data|ibm|custom",
    "boot": "<boot file path>",
    "files": [
      {
            "path": "<file path>",
            "header": "none|amsdos",
            "amsdosType": "bas|bin|binp|scr|asc|",
            "loadAddress": <load address in integer>,
            "executionAddress": <execution address in integer>
      } (, { ... } )*
    ]
```

###RAW Catalog format

```
Header       Entries 
(16 bytes)   (8 bytes each)
```

**Header**:
* Catalog ID: 8 fixed bytes: 0xE5, 0x52, 0x41, 0x57, 0x20, 0x43, 0x41, 0x54 (or 0xE5 + "RAW CAT")
* Disk ID: 6 bytes.
* Number of catalog entries: 2 bytes.
**Entries**
First entry is at offset 16.
* Byte 0: Padding (0xE5)
* Byte 1: Disc Side
* Byte 2: Initial Track
* Byte 3: Initial Sector
* Bytes 4-5: File length in bytes.
* Bytes 6-7: Unused.

##Examples

```
dskgen -o disk.dsk -c RAW -t SYSTEM -h NONE --files fileSpec1[;fileSpec2;fileSpecN]
dskgen -o disk.dsk --config cfg.json
```

cfg.json contents:

```
{
    "sides": 2,
    "catalog": "cpm",
    "diskType": "data",
    "boot": null,
    "files": [
        {
            "path": "samplefile.bin",
            "header": "amsdos",
            "amsdosType": "bin",
            "loadAddress": 256,
            "executionAddress": 1451
        }
    ]
}
```

If you liked this program, send the author an email ;)
