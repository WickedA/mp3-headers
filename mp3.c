#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// MPEG audio header, 32 bits:
// - 11-bit syncword (all bits must be set)
// -  2-bit MPEG version (00 MPEG2.5, 10 MPEG2, 11 MPEG1)
// -  2-bit MPEG layer (01 Layer3, 10 Layer2, 11 Layer1)
// -  1-bit error protection flag (if 1, 16-bit CRC follows header)
// -  4-bit bitrate index
// -  2-bit sampling rate index
// -  1-bit padding flag (is MP3 frame padded to fit the bitrate?)
// -  1-bit unused
// -  2-bit channel mode flag
// -  2-bit channel mode extension, used for joint stereo (channel mode 0b01)
// -  1-bit copyright flag
// -  1-bit original flag
// -  2-bit obscure emphasis thing

typedef struct mpa_header_s {
    bool     valid;
    uint8_t* location;
    size_t   frameSize;

    uint8_t  mpegVersion;
    uint8_t  mpegLayer;
    bool     crcEnabled;
    uint16_t bitrate;
    uint16_t samplerate;
    bool     framePadded;
    uint8_t  channelMode;
    uint8_t  cmLayer2BandLower;
    uint8_t  cmLayer2BandUpper;
    bool     cmLayer3IntensityStereo;
    bool     cmLayer3MSStereo;
    bool     copyrightFlag;
    bool     originalFlag;
    uint8_t  emphasisMode;
} mpa_header;

#define INVALID_HEADER ((mpa_header) {0})

const uint8_t MPEG_V1  = 1;
const uint8_t MPEG_V2  = 2;
const uint8_t MPEG_V25 = 3;

const uint8_t CHANNEL_MODE_STEREO       = 1;
const uint8_t CHANNEL_MODE_JOINT_STEREO = 2;
const uint8_t CHANNEL_MODE_DUAL         = 3;
const uint8_t CHANNEL_MODE_MONO         = 4;

const uint8_t EMPHASIS_NONE      = 1;
const uint8_t EMPHASIS_50_15_MS  = 2;
const uint8_t EMPHASIS_CCITT_J17 = 3;

// Read an MPEG audio header from the given memory location and return an mpa_header object.
mpa_header ReadMPAHeader (uint8_t* headerLoc) {
    mpa_header hdr = { 0 };
    hdr.valid     = true;
    hdr.location  = headerLoc;
    uint32_t data =
        (headerLoc[0] << 24) | (headerLoc[1] << 16) | (headerLoc[2] << 8) | headerLoc[3];

    // Check the sync bits.
    if (!((data & 0b11111111111000000000000000000000) == 0b11111111111000000000000000000000)) {
        return INVALID_HEADER;
    }

    // Get the various raw values contained in the header.
    uint32_t mpegVersionBits  = (data & 0b00000000000110000000000000000000) >> 19;
    uint32_t mpegLayerBits    = (data & 0b00000000000001100000000000000000) >> 17;
    uint32_t crcEnabledBits   = (data & 0b00000000000000010000000000000000) >> 16;
    uint32_t bitrateBits      = (data & 0b00000000000000001111000000000000) >> 12;
    uint32_t samplerateBits   = (data & 0b00000000000000000000110000000000) >> 10;
    uint32_t paddingBits      = (data & 0b00000000000000000000001000000000) >> 9;
    uint32_t cmBits           = (data & 0b00000000000000000000000011000000) >> 6;
    uint32_t cmeBits          = (data & 0b00000000000000000000000000110000) >> 4;
    uint32_t copyrightBits    = (data & 0b00000000000000000000000000001000) >> 3;
    uint32_t originalBits     = (data & 0b00000000000000000000000000000100) >> 2;
    uint32_t emphasisBits     = (data & 0b00000000000000000000000000000011) >> 0;

    // Store the MPEG version.
    if      (mpegVersionBits == 0b00) hdr.mpegVersion = MPEG_V25;
    else if (mpegVersionBits == 0b10) hdr.mpegVersion = MPEG_V2;
    else if (mpegVersionBits == 0b11) hdr.mpegVersion = MPEG_V1;
    else    return INVALID_HEADER;

    // Store the MPEG layer.
    if      (mpegLayerBits == 0b01) hdr.mpegLayer = 3;
    else if (mpegLayerBits == 0b10) hdr.mpegLayer = 2;
    else if (mpegLayerBits == 0b11) hdr.mpegLayer = 1;
    else    return INVALID_HEADER;

    // Store the CRC flag.
    hdr.crcEnabled = (bool) crcEnabledBits;

    // Store the bitrate, which varies depending on bitrateBits, the MPEG version and the layer.
    //     bits	    V1,L1	V1,L2	V1,L3	V2,L1	V2, L2 & L3
    //     0000	    free	free	free	free	free
    //     0001	    32	    32	    32	    32	    8
    //     0010	    64	    48	    40	    48	    16
    //     0011	    96	    56	    48	    56	    24
    //     0100	    128	    64	    56	    64	    32
    //     0101	    160	    80	    64	    80	    40
    //     0110	    192	    96	    80	    96	    48
    //     0111	    224	    112	    96	    112	    56
    //     1000	    256	    128	    112	    128	    64
    //     1001	    288	    160	    128	    144	    80
    //     1010	    320	    192	    160	    160	    96
    //     1011	    352	    224	    192	    176	    112
    //     1100	    384	    256	    224	    192	    128
    //     1101	    416	    320	    256	    224	    144
    //     1110	    448	    384	    320	    256	    160
    //     1111	    bad	    bad	    bad	    bad	    bad
    // (V2 means both MPEG2 and MPEG2.5)
    switch (bitrateBits) {
    case 0b0000:
        hdr.bitrate = 0;
        break;
    case 0b0001:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 32;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 32;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 32;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 32;
            else                    hdr.bitrate = 8;
        }
        break;
    case 0b0010:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 64;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 48;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 40;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 48;
            else                    hdr.bitrate = 16;
        }
        break;
    case 0b0011:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 96;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 56;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 48;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 56;
            else                    hdr.bitrate = 24;
        }
        break;
    case 0b0100:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 128;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 64;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 56;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 64;
            else                    hdr.bitrate = 32;
        }
        break;
    case 0b0101:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 160;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 80;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 64;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 80;
            else                    hdr.bitrate = 40;
        }
        break;
    case 0b0110:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 192;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 96;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 80;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 96;
            else                    hdr.bitrate = 48;
        }
        break;
    case 0b0111:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 224;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 112;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 96;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 112;
            else                    hdr.bitrate = 56;
        }
        break;
    case 0b1000:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 256;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 128;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 112;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 128;
            else                    hdr.bitrate = 64;
        }
        break;
    case 0b1001:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 288;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 160;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 128;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 144;
            else                    hdr.bitrate = 80;
        }
        break;
    case 0b1010:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 320;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 192;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 160;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 190;
            else                    hdr.bitrate = 96;
        }
        break;
    case 0b1011:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 352;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 224;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 192;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 176;
            else                    hdr.bitrate = 112;
        }
        break;
    case 0b1100:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 384;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 256;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 224;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 192;
            else                    hdr.bitrate = 128;
        }
        break;
    case 0b1101:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 416;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 320;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 256;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 224;
            else                    hdr.bitrate = 144;
        }
        break;
    case 0b1110:
        if (hdr.mpegVersion == MPEG_V1) {
            if      (hdr.mpegLayer == 1) hdr.bitrate = 448;
            else if (hdr.mpegLayer == 2) hdr.bitrate = 384;
            else if (hdr.mpegLayer == 3) hdr.bitrate = 320;
        } else {
            if (hdr.mpegLayer == 1) hdr.bitrate = 256;
            else                    hdr.bitrate = 160;
        }
        break;
    case 0b1111:
        return INVALID_HEADER;
    }

    // Store the sample rate.
    //     bits	    MPEG1	    MPEG2	    MPEG2.5
    //     00	    44100 Hz	22050 Hz	11025 Hz
    //     01	    48000 Hz	24000 Hz	12000 Hz
    //     10	    32000 Hz	16000 Hz	8000 Hz
    //     11	    reserved    reserved    reserved
    switch (samplerateBits) {
    case 0b00:
        if      (hdr.mpegVersion == MPEG_V1)  hdr.samplerate = 44100;
        else if (hdr.mpegVersion == MPEG_V2)  hdr.samplerate = 22050;
        else if (hdr.mpegVersion == MPEG_V25) hdr.samplerate = 11025;
        break;
    case 0b01:
        if      (hdr.mpegVersion == MPEG_V1)  hdr.samplerate = 48000;
        else if (hdr.mpegVersion == MPEG_V2)  hdr.samplerate = 24000;
        else if (hdr.mpegVersion == MPEG_V25) hdr.samplerate = 12000;
        break;
    case 0b10:
        if      (hdr.mpegVersion == MPEG_V1)  hdr.samplerate = 32000;
        else if (hdr.mpegVersion == MPEG_V2)  hdr.samplerate = 16000;
        else if (hdr.mpegVersion == MPEG_V25) hdr.samplerate = 8000;
        break;
    case 0b11:
        return INVALID_HEADER;
    }

    // Store the frame padding flag.
    hdr.framePadded = (bool) paddingBits;

    // Store the channel mode.
    if      (cmBits == 0b00) hdr.channelMode = CHANNEL_MODE_STEREO;
    else if (cmBits == 0b01) hdr.channelMode = CHANNEL_MODE_JOINT_STEREO;
    else if (cmBits == 0b10) hdr.channelMode = CHANNEL_MODE_DUAL;
    else if (cmBits == 0b11) hdr.channelMode = CHANNEL_MODE_MONO;

    // Store whatever the channel mode extension bits say.
    // Layer 3 uses them to enable or disable the intensity stereo and MS stereo features.
    // Layers 1 and 2 use them to mark which bands intensity stereo is applied to.
    if (hdr.mpegLayer == 3) {
        if (cmeBits == 0b01 || cmeBits == 0b10) hdr.cmLayer3IntensityStereo = true;
        if (cmeBits == 0b10 || cmeBits == 0b11) hdr.cmLayer3MSStereo = true;
    } else {
        hdr.cmLayer2BandUpper = 31;
        if      (cmeBits == 0b00) hdr.cmLayer2BandLower = 4;
        else if (cmeBits == 0b01) hdr.cmLayer2BandLower = 8;
        else if (cmeBits == 0b10) hdr.cmLayer2BandLower = 12;
        else if (cmeBits == 0b11) hdr.cmLayer2BandLower = 16;
    }

    // Store the copyright and original flags.
    hdr.copyrightFlag = (bool) copyrightBits;
    hdr.originalFlag  = (bool) originalBits;

    // Store the emphasis value.
    if      (emphasisBits == 0b00) hdr.emphasisMode = EMPHASIS_NONE;
    else if (emphasisBits == 0b01) hdr.emphasisMode = EMPHASIS_50_15_MS;
    else if (emphasisBits == 0b10) hdr.emphasisMode = EMPHASIS_CCITT_J17;
    
    // Now that we've extracted everything out of the header, calculate the frame's size.
    // The formula is 144 * bitrate (bits/sec) / samplerate (Hz)
    // Add 1 if the padding bit is set, apparently.
    // Note that the bitrate we get above is in kilobits per second.
    // TODO: is this a Layer3-only thing? Do other MPEG audio frames behave the same?
    hdr.frameSize = 144 * (hdr.bitrate * 1000) / hdr.samplerate;
    if (hdr.framePadded) hdr.frameSize += 1;

    // Return the filled-out header.
    return hdr;
}

// Look for the first valid header, starting at firstLoc and ending the search when reaching
// lastLoc. Returns a valid header if the search is successful and INVALID_HEADER if not.
mpa_header GetFirstHeader (uint8_t* firstLoc, uint8_t* lastLoc) {
    mpa_header nextHdr = { 0 };
    while (nextHdr.valid == false && firstLoc <= lastLoc) {
        nextHdr = ReadMPAHeader(firstLoc);
        ++firstLoc;
    }
    return nextHdr;
}

// Get the next valid header, given a header and a last allowable search location.
mpa_header GetNextHeader (mpa_header* lastHdr, uint8_t* lastLoc) {
    return GetFirstHeader(lastHdr->location + lastHdr->frameSize, lastLoc);
}

// Attempt to read an ID3v2 header and return the total size in bytes of the entire ID3 tag.
// Returns 0 if the given location does not point to a valid ID3v2 tag.
size_t GetID3v2TagSize (uint8_t* loc) {
    // Every ID3v2 tag starts with a 10-byte header containing the following:
    // - the sequence "ID3"
    // - a 2-byte version field
    // - a 1-byte flags field
    // - a 4-byte length for the tag, which is what we want
    if (loc[0] == 'I' && loc[1] == 'D' && loc[2] == '3') {
        // The length is actually a 32-bit "synchsafe" integer, which means the highest bit of each
        // component byte is set to 0. This means we need to fiddle with them a bit in order to
        // produce the actual 28-bit integer we care about.
        uint32_t length;
        uint8_t l0 = loc[6];
        uint8_t l1 = loc[7];
        uint8_t l2 = loc[8];
        uint8_t l3 = loc[9];
        length = (l0 << 21) | (l1 << 14) | (l2 << 7) | l3;
        // The length of the ID3 header (10 bytes) and footer (optional, also 10 bytes) is *not*
        // included in the value we just extracted.
        bool hasFooter = (bool) ((loc[5] & 0b00010000) >> 4);
        if (hasFooter) length += 20;
        else           length += 10;
        fprintf(stderr, "ID3v2 tag found at %lld with size %ld\n", (uint64_t) loc, length);
        return length;
    } else {
        return 0;
    }
}

// Load an entire file into memory as a null-terminated string.
typedef struct mem_file_s {
    size_t   size;
    uint8_t* mem;
} mem_file;
mem_file ReadFile (char* filename) {
    FILE* stream = fopen(filename, "r");
    if (stream == NULL) {
        fprintf(stderr, "ReadFile: failed to open %s\n", filename);
        exit(1);
    }

    fseek(stream, 0, SEEK_END);
    size_t   size = ftell(stream);
    uint8_t* mem  = (uint8_t*) malloc(size + 1);
    if (mem == NULL) {
        fprintf(stderr, "ReadFile: %ld byte allocation failed\n", size);
        exit(1);
    }
        
    rewind(stream);
    fread(mem, 1, size, stream);
    fclose(stream);

    mem[size] = '\0';
    mem_file mf = {size, mem};
    return mf;
}

int main() {
    mem_file testFileObj = ReadFile("test.mp3");
    uint8_t* firstLoc = testFileObj.mem + GetID3v2TagSize(testFileObj.mem);
    uint8_t* lastLoc  = testFileObj.mem + testFileObj.size;

    printf("Starting MPEG header search at %08llx...\n", (uint64_t) firstLoc);
    //printf("main:  lastLoc = %lld\n", (uint64_t) lastLoc);

    mpa_header firstHeader = GetFirstHeader(firstLoc, lastLoc);
    if (firstHeader.valid) {
        printf("First valid header at %08llx:\n", (uint64_t) firstHeader.location);
        printf("  MPEG%s Layer %d\n",
            ((firstHeader.mpegVersion == MPEG_V1)? "1" :
                (firstHeader.mpegVersion == MPEG_V2)? "2": "2.5"),
            firstHeader.mpegLayer);
        printf("  Bit rate:    %d kbps\n", firstHeader.bitrate);
        printf("  Sample rate: %d Hz\n", firstHeader.samplerate);
        printf("  Copyright: %s\n", firstHeader.copyrightFlag? "yes" : "no");
        printf("  Original:  %s\n", firstHeader.originalFlag?  "yes" : "no");
    } else {
        printf("No valid MPEG audio headers found.\n");
    }
    printf("\n");

    int nHeaders = 50;
    bool printAllHeaders = true;

    if (printAllHeaders) {
        printf("Printing first %d MPEG headers found.\n\n", nHeaders);
    } else {
        printf("Printing first %d MPEG%s Layer %d headers found.\n\n", nHeaders,
            (firstHeader.mpegVersion == MPEG_V1)? "1" :
                (firstHeader.mpegVersion == MPEG_V2)? "2" : "2.5",
            firstHeader.mpegLayer);
    }

    printf(" Location | MPEG | L | Kbps | Hz    | H | C | O | Frame \n");
    printf("----------|------|---|------|-------|---|---|---|-------\n");

    mpa_header header = firstHeader;
    while (header.valid && nHeaders > 0) {
        if (printAllHeaders || (
                header.mpegVersion == firstHeader.mpegVersion || 
                header.mpegLayer   == firstHeader.mpegLayer)) {
            printf(" %08llx | V%s | %d | %4d | %5d | %s | %s | %s | %5ld \n",
                (uint64_t) (header.location - firstHeader.location),
                (header.mpegVersion == MPEG_V1)? "1  " :
                    (header.mpegVersion == MPEG_V2)? "2  " : "2.5",
                header.mpegLayer,
                header.bitrate,
                header.samplerate,
                header.crcEnabled?    "Y" : " ",
                header.copyrightFlag? "Y" : " ",
                header.originalFlag?  "Y" : " ",
                header.frameSize);
            --nHeaders;
        }
        header = GetNextHeader(&header, lastLoc);
    }

    return 0;
}