# You can put your flac file here in .h format

Make sure the flac is **single channel**,  **8 bit** with a file size of less than **3.68MB**.

Example

```c
#ifndef FLACFILE_H
#define FLACFILE_H

static const unsigned char flacFile[] = {
 0x66, 0x4c, 0x61, 0x43, 0x00, 0x00, 0x00, 0x22, 0x12, 0x00, 0x12, 0x00,
    ...
 0x6f, 0xb7, 0xed, 0xfb, 0xfe, 0xff, 0xfd, 0xff, 0xd0, 0xf0, 0x0b
};

#endif /* FLACFILE_H */

```

You can then in the `main.c` include the flac file with `#include flac/<your_flac_file.h>`
