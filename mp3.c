#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

// MPEG audio header, 32 bits:
// - 11-bit syncword (all bits must be set)
// -  2-bit MPEG version (00 MPEG2.5, 10 MPEG2, 11 MPEG1)
// -  2-bit MPEG layer (01 Layer3, 10 Layer2, 11 Layer1)
// -  1-bit error checking flag (if 1, 16-bit CRC follows header)
// -  4-bit bitrate index
// -  2-bit sampling rate index
// -  1-bit padding flag (is MP3 frame padded to fit the bitrate?)
// -  1-bit unused
// -  2-bit channel mode flag
// -  2-bit channel mode extension, used for joint stereo (channel mode 0b01)
// -  1-bit copyright flag
// -  1-bit original flag
// -  2-bit obscure emphasis thing

// Struct to contain all header data in an accessible format, for any given header:
typedef struct mpa_header_s {
    bool     valid;                     // whether the header is valid or not
    uint8_t* location;                  // the header's location in memory
    size_t   frameSize;                 // the frame's total size in bytes, including header

    uint8_t  mpegVersion;               // either MPEG_V1, MPEG_V2 or MPEG_V25
    uint8_t  mpegLayer;                 // either 1, 2 or 3
    bool     crcEnabled;                // whether error checking is enabled for this header
    uint16_t bitrate;                   // bitrate in kbps
    uint16_t samplerate;                // sample rate in Hz
    bool     framePadded;               // whether the frame is padded
    uint8_t  channelMode;               // one of the CHANNEL_MODE_* constants
    uint8_t  cmLayer2BandLower;         // layer2 intensity stereo lower band
    uint8_t  cmLayer2BandUpper;         // layer2 intensity stereo upper band
    bool     cmLayer3IntensityStereo;   // layer3 intensity stereo enabled/disabled
    bool     cmLayer3MSStereo;          // layer3 MS stereo enabled/disabled
    bool     copyrightFlag;             // copyright flag
    bool     originalFlag;              // original flag
    uint8_t  emphasisMode;              // one of the EMPHASIS_* constants
} mpa_header;

// Relevant constants:
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

// Try to read an MPEG audio header from the given memory location. Returns an mpa_header object.
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
    //     bits     V1,L1   V1,L2   V1,L3   V2,L1   V2, L2 & L3
    //     0000     free    free    free    free    free
    //     0001     32      32      32      32      8
    //     0010     64      48      40      48      16
    //     0011     96      56      48      56      24
    //     0100     128     64      56      64      32
    //     0101     160     80      64      80      40
    //     0110     192     96      80      96      48
    //     0111     224     112     96      112     56
    //     1000     256     128     112     128     64
    //     1001     288     160     128     144     80
    //     1010     320     192     160     160     96
    //     1011     352     224     192     176     112
    //     1100     384     256     224     192     128
    //     1101     416     320     256     224     144
    //     1110     448     384     320     256     160
    //     1111     bad     bad     bad     bad     bad
    // (V2 means both MPEG2 and MPEG2.5)
    
    // X-Macros are used to generate the code that decides which bitrate to store.
    
    #define X_BITRATES \
        X(0b0001, 32,  32,  32,  32,  8)   \
        X(0b0010, 64,  48,  40,  48,  16)  \
        X(0b0011, 96,  56,  48,  56,  24)  \
        X(0b0100, 128, 64,  56,  64,  32)  \
        X(0b0101, 160, 80,  64,  80,  40)  \
        X(0b0110, 192, 96,  80,  96,  48)  \
        X(0b0111, 224, 112, 96,  112, 56)  \
        X(0b1000, 256, 128, 112, 128, 64)  \
        X(0b1001, 288, 160, 128, 144, 80)  \
        X(0b1010, 320, 192, 160, 160, 96)  \
        X(0b1011, 352, 224, 192, 176, 112) \
        X(0b1100, 384, 256, 224, 192, 128) \
        X(0b1101, 416, 320, 256, 224, 144) \
        X(0b1110, 448, 384, 320, 256, 160)
    
    #define X(MATCH, BITRATE_V1L1, BITRATE_V1L2, BITRATE_V1L3, BITRATE_V2L1, BITRATE_V2LX) \
        case MATCH: \
            if (hdr.mpegVersion == MPEG_V1) { \
                if      (hdr.mpegLayer == 3) hdr.bitrate = BITRATE_V1L3; \
                else if (hdr.mpegLayer == 2) hdr.bitrate = BITRATE_V1L2; \
                else if (hdr.mpegLayer == 1) hdr.bitrate = BITRATE_V1L1; \
            } else { \
                if (hdr.mpegLayer == 1) hdr.bitrate = BITRATE_V2L1; \
                else                    hdr.bitrate = BITRATE_V2LX; \
            } \
            break;
    
    switch (bitrateBits) {
        case 0b0000:
            hdr.bitrate = 0;
            break;
        case 0b1111:
            return INVALID_HEADER;
        
        X_BITRATES
    }
    
    #undef X

    // Store the sample rate.
    //     bits     MPEG1       MPEG2       MPEG2.5
    //     00       44100 Hz    22050 Hz    11025 Hz
    //     01       48000 Hz    24000 Hz    12000 Hz
    //     10       32000 Hz    16000 Hz    8000 Hz
    //     11       reserved    reserved    reserved
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
    // NOTE: hdr.bitrate is in kilobits per second and has to be converted.
    // NOTE: the obtained frame size includes the header.
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
// Skips over the given header's frame.
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
        fprintf(stderr, "ID3v2 tag found with length %ld\n", length);
        return length;
    } else {
        return 0;
    }
}

// Struct for an in-memory file.
typedef struct mem_file_s {
    size_t   size;
    uint8_t* mem;
} mem_file;

// Load an entire file into memory as a null-terminated string. Returns a mem_file object.
mem_file ReadFileIntoMemory (char* filename) {
    // Open the file:
    FILE* stream = fopen(filename, "r");
    if (stream == NULL) {
        fprintf(stderr, "ReadFile: failed to open %s\n", filename);
        exit(1);
    }

    // Get the file's size and allocate a suitably-sized buffer:
    fseek(stream, 0, SEEK_END);
    size_t   size = ftell(stream);
    uint8_t* mem  = (uint8_t*) malloc(size + 1);
    if (mem == NULL) {
        fprintf(stderr, "ReadFile: %lld byte allocation failed\n", (uint64_t) size);
        exit(1);
    }
    
    // Rewind, read the file and close it.
    rewind(stream);
    fread(mem, 1, size, stream);
    fclose(stream);

    // Add the null terminator and build up the mem_file object to return.
    mem[size] = '\0';
    mem_file mf = {size, mem};
    return mf;
}


int main() {
    // Read the test file into memory and get a pointer to its contents:
    mem_file testFileObj = ReadFileIntoMemory("test.mp3");
    size_t fileStart  = (size_t) testFileObj.mem;
    
    // Get pointers to the first and last memory locations of the actual MPEG data:
    uint8_t* firstLoc = testFileObj.mem + GetID3v2TagSize(testFileObj.mem);
    uint8_t* lastLoc  = testFileObj.mem + testFileObj.size;

    
    // Locate and print the first MPEG header's details:
    printf("Starting MPEG header search at %08llx...\n", (uint64_t) (firstLoc - fileStart));
    
    mpa_header firstHeader = GetFirstHeader(firstLoc, lastLoc);
    if (firstHeader.valid) {
        printf("First valid header at %08llx:\n", (uint64_t) (firstHeader.location - fileStart));
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

    
    // Print a table containing details for the first n MPEG headers:
    int nHeaders = 50;           // how many headers to process
    bool printAllHeaders = true; // if false, skip headers with different MPEG versions or layers

    if (printAllHeaders) {
        printf("Printing first %d MPEG headers found.\n\n", nHeaders);
    } else {
        printf("Printing first %d MPEG%s Layer %d headers found.\n\n", nHeaders,
            (firstHeader.mpegVersion == MPEG_V1)? "1" :
                (firstHeader.mpegVersion == MPEG_V2)? "2" : "2.5",
            firstHeader.mpegLayer);
    }

    printf(" Location | MPEG | L | Kbps | Hz    | E | C | O | Frame \n");
    printf("----------|------|---|------|-------|---|---|---|-------\n");

    // Loop over each header starting with the previously obtained one.
    mpa_header header = firstHeader;
    while (header.valid && nHeaders > 0) {
        // If printAllHeaders is false, check to see if this header's version and layer are correct:
        if (printAllHeaders || (
                header.mpegVersion == firstHeader.mpegVersion || 
                header.mpegLayer   == firstHeader.mpegLayer))
        {
            // Print a row for the current header, containing:
            // - location in memory
            // - MPEG version
            // - MPEG layer
            // - bitrate (kbps)
            // - sample rate (Hz)
            // - whether error checking is enabled for this header
            // - whether the "copyright" flag is set
            // - whether the "original" flag is set
            // - frame size
            
            printf(" %08llx | V%s | %d | %4d | %5d | %s | %s | %s | %5lld \n",
                (uint64_t) (header.location - fileStart),
                (header.mpegVersion == MPEG_V1)? "1  " :
                    (header.mpegVersion == MPEG_V2)? "2  " : "2.5",
                header.mpegLayer,
                header.bitrate,
                header.samplerate,
                header.crcEnabled?    "Y" : " ",
                header.copyrightFlag? "Y" : " ",
                header.originalFlag?  "Y" : " ",
                (uint64_t) header.frameSize);
                
            // Get the next header and decrement the nHeaders counter:
            header = GetNextHeader(&header, lastLoc);
            nHeaders--;
        }
    }

    return 0;
}