#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <threads.h>
#include <time.h>

// todo next: progressive scan
// - if only one component in a scan, the subsampling swizzling is ignored
// - 

#if defined(_MSC_VER)
#define ALIGN(a) __declspec(align(a))
#else
#define ALIGN(a) __attribute__((aligned(a)))
#endif

#define DEBUG 1
#define DEBUG_ERROR 1
#define DEBUG_VERBOSE 0
#define DEBUG_MEMORY 1
#define PROFILING 0
#define FLOAT float
#define pi 3.141592653589793

#define BLOCK_RES 8
#define BLOCK_RES2 ((BLOCK_RES) * (BLOCK_RES))
FLOAT ALIGN(128) lut_dct[BLOCK_RES2] = {0};
int ALIGN(128) lut_zigzag[BLOCK_RES2]  = {0};

thread_local char error_buffer[256];
thread_local size_t error_length = 0;

#if DEBUG
#define ERROR(err) { printf("error: %s:%i: %s\n", __FILE__, __LINE__, err); exit(-2); }
#define TODO() { printf("todo: %s:%i\n", __FILE__, __LINE__); exit(-1); }

#elif DEBUG_ERROR
#define ERROR(err) { \
    strncpy(error_buffer, err, sizeof(error_buffer)); \
    error_length = strlen(error_buffer); \
    printf("error: %s\n", error_buffer); \
}
#define TODO() {}

#else
#define ERROR(err) { \
    strncpy(error_buffer, err, sizeof(error_buffer)); \
    error_length = strlen(error_buffer); \
}
#define TODO() {}

#endif

/// Enums
typedef enum {
    // implemented
    JPEG_MARKER_START_OF_FRAME0 = 0xC0, // non-differential huffman-coded baseline DCT
    JPEG_MARKER_START_OF_FRAME1 = 0xC1,  // non-differential huffman-coded extended sequential DCT
    JPEG_MARKER_HUFFMAN_TABLE = 0xC4,
    JPEG_MARKER_START_OF_IMAGE = 0xD8, // (file magic)
    JPEG_MARKER_END_OF_IMAGE = 0xD9, 
    JPEG_MARKER_START_OF_SCAN = 0xDA, // start of compressed image data
    JPEG_MARKER_QUANT_TABLE = 0xDB,
    JPEG_MARKER_RESTART_INTERVAL = 0xDD, // how many macroblocks before restart
    JPEG_MARKER_JFIF_APP0 = 0xE0, // JFIF header
    JPEG_MARKER_JFIF_APP1 = 0xE1, // EXIF header
    JPEG_MARKER_JFIF_APP2 = 0xE2, // icc color profile
    JPEG_MARKER_COMMENT = 0xFE,
    JPEG_MARKER_PREFIX = 0xFF, // prefix byte preceding every marker

    // todo:
    JPEG_MARKER_START_OF_FRAME2 = 0xC2,  // non-differential huffman-coded progressive DCT
    JPEG_MARKER_START_OF_FRAME3 = 0xC3,  // non-differential huffman-coded lossless (sequential)
    JPEG_MARKER_START_OF_FRAME5 = 0xC5,  // differential huffman-coded sequential DCT
    JPEG_MARKER_START_OF_FRAME6 = 0xC6,  // differential huffman-coded progressive DCT
    JPEG_MARKER_START_OF_FRAME7 = 0xC7,  // differential huffman-coded lossless (sequential)
    JPEG_MARKER_START_OF_FRAME9 = 0xC9,  // non-differential arithmetic-coded extended sequential DCT
    JPEG_MARKER_START_OF_FRAME10 = 0xCA, // non-differential arithmetic-coded progressive DCT
    JPEG_MARKER_START_OF_FRAME11 = 0xCB, // non-differential arithmetic-coded lossless (sequential)
    JPEG_MARKER_START_OF_FRAME13 = 0xCD, // differential arithmetic-coded extended sequential DCT
    JPEG_MARKER_START_OF_FRAME14 = 0xCE, // differential arithmetic-coded progressive DCT
    JPEG_MARKER_START_OF_FRAME15 = 0xCF, // differential arithmetic-coded lossless (sequential)
    JPEG_MARKER_DEF_ARITH_CODING = 0xCC, // arithetic coding conditions
    JPEG_MARKER_RST0 = 0xD0, // restart marker 0
    JPEG_MARKER_RST1 = 0xD1, // restart marker 1
    JPEG_MARKER_RST2 = 0xD2, // restart marker 2
    JPEG_MARKER_RST3 = 0xD3, // restart marker 3
    JPEG_MARKER_RST4 = 0xD4, // restart marker 4
    JPEG_MARKER_RST5 = 0xD5, // restart marker 5
    JPEG_MARKER_RST6 = 0xD6, // restart marker 6
    JPEG_MARKER_RST7 = 0xD7, // restart marker 7
    JPEG_MARKER_JFIF_APP3 = 0xE3, // todo: figure these out
    JPEG_MARKER_JFIF_APP4 = 0xE4,
    JPEG_MARKER_JFIF_APP5 = 0xE5,
    JPEG_MARKER_JFIF_APP6 = 0xE6,
    JPEG_MARKER_JFIF_APP7 = 0xE7,
    JPEG_MARKER_JFIF_APP8 = 0xE8,
    JPEG_MARKER_JFIF_APP9 = 0xE9,
    JPEG_MARKER_JFIF_APP10 = 0xEA,
    JPEG_MARKER_JFIF_APP11 = 0xEB,
    JPEG_MARKER_JFIF_APP12 = 0xEC,
    JPEG_MARKER_JFIF_APP13 = 0xED,
    JPEG_MARKER_JFIF_APP14 = 0xEE,
    JPEG_MARKER_JFIF_APP15 = 0xEF,
    JPEG_MARKER_LINE_COUNT = 0xDC,
} jpeg_marker_type_t;

typedef enum {
    JPEG_DENSITY_NONE = 0,
    JPEG_DENSITY_PPI = 1,
    JPEG_DENSITY_PPCM = 2,
} jpeg_density_units_t;

typedef enum {
    JPEG_THUMB_JPEG = 0x10,
    JPEG_THUMB_PALETTE_8 = 0x11,
    JPEG_THUMB_PIXEL_24 = 0x13,
} jpeg_thumbnail_format_t;

typedef enum {
    JPEG_BYTE_ORDER_LE = 0x4949,
    JPEG_BYTE_ORDER_BE = 0x4D4D,
} jpeg_byte_order_t;

typedef enum {
    JPEG_SCAN_MODE_BASELINE = 0,
    JPEG_SCAN_MODE_EXTENDED = 1,
    JPEG_SCAN_MODE_PROGRESSIVE = 2,
    JPEG_SCAN_MODE_LOSSLESS = 3,
} jpeg_scan_mode_t;

// Structs
typedef struct {
    uint8_t magic; // 0xFF
    uint8_t type; // see jpeg_marker_type_t
} jpeg_marker_t;

typedef struct {
    uint16_t length; // excluding markers
    char identifier[5]; // either "JFIF" or "JFXX" including null terminator
    // following this header: either `jpeg_app0_jfif_t`or `jpeg_app0_jfxx_t`, depending on `identifier`
} jpeg_app0_t;

typedef struct {
    uint8_t jfif_version_major;
    uint8_t jfif_version_minor;
    uint8_t density_units; // see `jpeg_density_units_t`
    uint16_t density_w;
    uint16_t density_h;
    uint8_t thumbnail_w; // in pixels, can be 0, in which case there is no thumbnail data after this header
    uint8_t thumbnail_h; // in pixels, can be 0, in which case there is no thumbnail data after this header    
    // following this header: uint8_t thumbnail_pixels[3 * thumbnail_w * thumbnail_h];
} jpeg_app0_jfif_t;

typedef struct {
    uint8_t thumbnail_format; // see `jpeg_thumbnail_format_t`
    // following this header: thumbnail data
    // todo: do we want this to be parsed at all?
} jpeg_app0_jfxx_t;

typedef struct {
    uint16_t length; // excluding markers
    char identifier[32]; // "Exif" including null terminator
    uint8_t _pad; // ignore, likely 0
    uint16_t byte_order; // see `jpeg_byte_order_t`
    uint16_t tiff_identifier; // should be 42
    int32_t ifd_offset; // relative to start of TIFF header
} jpeg_app1_t;

typedef enum {
    COLOR_TRANSFORM_UNKNOWN = 0x00,
    COLOR_TRANSFORM_YCBCR = 0x01,
    COLOR_TRANSFORM_YCCK = 0x02,
} jpeg_app14_color_transform_t;

typedef struct {
    uint16_t length; // excluding markers
    char identifier[6]; // "Adobe" - not null terminated in file, will add null terminator myself
    uint16_t version;
    uint16_t flags0; // ?
    uint16_t flags1; // ?
    jpeg_app14_color_transform_t color_transform;
} jpeg_app14_t;

typedef struct {
    int16_t n_tags;
    // following this header: a list of `jpeg_ifd_tag_t`
} jpeg_ifd_header_t;

typedef struct {
    int16_t field_tag;
    int16_t field_type;
    int32_t length;
    union {
        uint32_t u32;
        uint16_t u16;
        uint8_t u8;
        int32_t s32;
        int16_t s16;
        int8_t s8;
    };
} jpeg_ifd_tag_t;

typedef struct {
    uint16_t length;
    uint8_t table_id; // todo: table id and 16-bit precision bit?
    // following this header: length - sizeof(jpeg_quant_table_header_t) bytes of quantization table values, usually 64 bytes
} jpeg_quant_table_t;

typedef struct {
    uint16_t length;
    uint8_t bits_per_pixel;
    uint16_t height;
    uint16_t width;
    uint8_t n_components;
    // following this header: `n_components` entries of `jpeg_channel_t`
} jpeg_start_of_frame_t;

typedef struct {
    int32_t coefficients[BLOCK_RES2];
} jpeg_block_t;

typedef struct {
    uint8_t id;
    union {
        uint8_t u8;
        struct {
            uint8_t height : 4;
            uint8_t width : 4;
        };
    } blocks_per_mcu;
    uint8_t quant_table_id;
    
    jpeg_block_t* blocks;
    int32_t dc_prev;
    int32_t block_index;
} jpeg_channel_t;

typedef struct {
    uint16_t length;
    union {
        uint8_t class_id;
        struct {
            uint8_t id : 4;
            uint8_t class : 4;
        };
    };
    uint8_t bit_lengths[16];
    // following this header: huffman table symbols
} jpeg_huffman_table_header_t;

typedef struct {
    uint8_t length;
    uint8_t symbol;
} jpeg_huffman_lut_entry_t;

typedef struct {
    size_t table_buf_size;
    uint8_t* symbols;
    uint32_t* codes;
    uint8_t* code_lengths;
    jpeg_huffman_lut_entry_t* left_shifted_code_lut;
} jpeg_huffman_table_decoded_t;

typedef struct {
    uint8_t id; // see jpeg_component_id_t;
    union {
        struct {
            uint8_t ac : 4;
            uint8_t dc : 4;
        };
        uint8_t u8;
    } tables;
} jpeg_component_t;

typedef struct {
    uint16_t length;
    uint8_t n_components;
    // following this: jpeg_component_t[n_components]
    jpeg_component_t* components;
    uint8_t spectral_selection_min;
    uint8_t spectral_selection_max;
    uint8_t successive_approximation;
    // following this header: compressed image data
} jpeg_start_of_scan_t;

typedef struct {
    // uint8_t r, g, b;
    uint8_t b, g, r; // temp for bmp
} rgb8_t;

typedef struct {
    uint8_t has_soi;
    uint8_t has_jfif;
    uint8_t has_jfxx;
    uint8_t has_exif;
    uint8_t has_app14;
    uint8_t has_sof;
    uint8_t has_sos;
    uint8_t has_chn;
    uint8_t has_thumb;
    uint8_t has_quant;
    uint8_t has_huff_tbl;
    uint8_t has_luts;
    
    uint16_t restart_interval;

    jpeg_app0_jfif_t jfif;
    jpeg_app0_jfxx_t jfxx;
    jpeg_app1_t exif;
    jpeg_app14_t app14;
    jpeg_start_of_frame_t start_of_frame;
    jpeg_start_of_scan_t start_of_scan;
    
    size_t image_data_start;
    size_t image_data_end;

    size_t mcu_width;
    size_t mcu_height;
    size_t n_mcu_x;
    size_t n_mcu_y;

    int n_jfif_thumbnail_bytes;
    int n_quant_tables;

    size_t scratch_buffer_size;
    void* scratch_buffer;

    rgb8_t* out_image;
    size_t out_width;
    size_t out_height;
    
#define MAX_ALLOC_COUNT 256
    void* allocated_chunks[MAX_ALLOC_COUNT];
    size_t alloc_cursor;

    int eob_run;

    jpeg_channel_t components[256];
    size_t component_mapping[256];
    size_t n_components;
    uint8_t* jfif_thumbnail_pixels; 
    uint8_t** quant_tables;
    jpeg_huffman_table_decoded_t huffman_tables_dc[16]; // shouldn't be more than 4, but i'll cover the whole 4-bit range just to be sure
    jpeg_huffman_table_decoded_t huffman_tables_ac[16];
} jpeg_state_t;

/// Memory
void* mem_alloc(size_t size, jpeg_state_t* state) {
    if (state->alloc_cursor >= MAX_ALLOC_COUNT) ERROR("Ran out of memory allocation slots")
    return state->allocated_chunks[state->alloc_cursor++] = malloc(size);
}

void* mem_realloc(void* ptr, size_t size, jpeg_state_t* state) {
    if (!ptr) return mem_alloc(size, state);

    size_t slot = MAX_ALLOC_COUNT;
    for (size_t i = 0; i < state->alloc_cursor; ++i) {
        if (state->allocated_chunks[i] == ptr) {
            slot = i;
        }
    }
    if (slot >= MAX_ALLOC_COUNT) ERROR("memory error: pointer tracking desync!")
    return state->allocated_chunks[slot] = realloc(ptr, size);
}

void mem_free_all(jpeg_state_t* state) {
    for (size_t i = 0; i < state->alloc_cursor; ++i) {
        free(state->allocated_chunks[i]);
        state->allocated_chunks[i] = 0;
    }
    state->alloc_cursor = 0;
}

void read_s32(FILE* file, int32_t* dest, jpeg_byte_order_t byte_order) {
    if (byte_order == JPEG_BYTE_ORDER_LE) {
        if (!fread(dest, sizeof(*dest), 1, file)) {
            ERROR("failed to read u32")
        };
    }
    else if (byte_order == JPEG_BYTE_ORDER_BE) {
        uint8_t be_bytes[4];
        union {
            uint8_t bytes[4];
            int32_t u32;
        } le;
        if (!fread(be_bytes, 1, 4, file)) {
            ERROR("failed to read u32");
        }
        le.bytes[0] = be_bytes[3];
        le.bytes[1] = be_bytes[2];
        le.bytes[2] = be_bytes[1];
        le.bytes[3] = be_bytes[0];
        *dest = le.u32;
        
    }
    else {
        ERROR("invalid byte order");
    }
}

void read_u16(FILE* file, uint16_t* dest, jpeg_byte_order_t byte_order) {
    if (byte_order == JPEG_BYTE_ORDER_LE) {
        if (!fread(dest, sizeof(*dest), 1, file)) ERROR("failed to read u16");
    }
    else if (byte_order == JPEG_BYTE_ORDER_BE) {
        uint8_t high = 0;
        uint8_t low = 0;
        if (!fread(&high, 1, 1, file)) ERROR("failed to read high byte of u16");
        if (!fread(&low, 1, 1, file)) ERROR("failed to read low byte of u16");
        *dest = ((uint16_t)high << 8) + ((uint16_t)low);
    }
    else ERROR("invalid byte order");
}

void read_u8(FILE* file, uint8_t* dest) {
    if (!fread(dest, sizeof(*dest), 1, file)) ERROR("failed to read u8");
}

void read_bytes(FILE* file, void* dest, size_t size) {
    if (fread(dest, 1, size, file) != size) {
        ERROR("failed to read bytes");
    }
}
void print_binary(uint64_t number, int32_t start_msb, int32_t n_bits) {
    #if DEBUG_VERBOSE
    for (int i = start_msb; i > (start_msb - n_bits); --i) {
        if ((number >> i) & 1) {
            printf("1");
        } else {
            printf("0");
        }
    }
    printf("\n");
    #else
    (void)number;
    (void)start_msb;
    (void)n_bits;
    #endif
}

typedef enum {
    HOLD_CONTINUE = 0,
    HOLD_RESTART = 1,
    HOLD_END_OF_IMAGE = 2,
    HOLD_END_OF_SCAN = 3,
} bit_stream_hold_reason_t;

typedef struct {
    uint64_t peek_buffer; // upper 16 bits will contain a key into the huffman table luts
    size_t peek_buffer_cursor; // so we can start writing to the peek buffer's most significant bits first, and then keep track as we shift them out
    bit_stream_hold_reason_t hold; // makes the file stream stop shifting in new bytes, until restarted externally. used when encountering restart markers or end of image. 0 = no hold, 1 = restart marker, 2 = end of image
} bit_stream_t;

void bit_stream_init(bit_stream_t* stream) {
    memset(stream, 0, sizeof(*stream));
}

int32_t bit_stream_get_next_bits(FILE* file, bit_stream_t* stream, size_t n_bits, int do_advance) {
    // should never happen, but let's be careful just in case
    if (n_bits == 0) return 0;
    if (n_bits >= 32) return 0; 

    // refill buffer
    while ((stream->peek_buffer_cursor < 56) && (stream->hold == 0)) {
        // todo: cache file data
        uint8_t next_byte = 0;
        fread(&next_byte, 1, 1, file);

        // handle markers
        if (next_byte == 0xFF) {
            fread(&next_byte, 1, 1, file);

            // 0xFF is the marker prefix, so for actual byte 0xFF in the 
            // bitstream, there has to be a 0x00 after, which we should ignore
            if (next_byte == 0x00) {
                next_byte = 0xFF;
            }
            else if (next_byte == JPEG_MARKER_END_OF_IMAGE) {
                stream->hold = HOLD_END_OF_IMAGE;
                #if DEBUG_VERBOSE
                printf("marker 0xFF%02X spotted at offset %08X\n", next_byte, (int)ftell(file));
                #endif
                break;
            }
            // detect restart markers
            else if (next_byte >= JPEG_MARKER_RST0 && next_byte <= JPEG_MARKER_RST7) {
                stream->hold = HOLD_RESTART;
                #if DEBUG_VERBOSE
                printf("marker 0xFF%02X spotted at offset %08X\n", next_byte, (int)ftell(file));
                #endif
                break;
            }
            else {
                stream->hold = HOLD_END_OF_SCAN;
                #if DEBUG_VERBOSE
                printf("marker 0xFF%02X spotted at offset %08X\n", next_byte, (int)ftell(file));
                #endif
                break;
            }
        }

        // shift into peek buffer
        stream->peek_buffer |= ((uint64_t)next_byte) << (56 - stream->peek_buffer_cursor);
        stream->peek_buffer_cursor += 8;
    }

    // get the upper n_bits, and shift them out afterwards
    const int32_t value = stream->peek_buffer >> (64 - n_bits);

    if (do_advance) {
        stream->peek_buffer <<= n_bits;
        stream->peek_buffer_cursor -= n_bits;
    }
    #if DEBUG_VERBOSE
    printf("ftell(): 0x%08X\n", (int)ftell(file));
    #endif
    return value;
}

size_t parse_start_of_image(jpeg_state_t* state) {
#if DEBUG_VERBOSE
        printf("START OF IMAGE\n");
#endif
    state->has_soi = 1;
    return 0;
}

size_t parse_end_of_image(void) {
#if DEBUG_VERBOSE
        printf("END OF IMAGE\n");
#endif
    return 0;
}

size_t parse_jfif_app0(FILE* file, jpeg_state_t* state) {
    jpeg_app0_t header = {};
    read_u16(file, &header.length, JPEG_BYTE_ORDER_BE);
    read_bytes(file, &header.identifier, sizeof(header.identifier));

#if DEBUG_VERBOSE
        printf("APP0:\n");
        printf("\t.length     = %i\n", header.length);
        printf("\t.identifier = %.*s", (int)sizeof(header.identifier), header.identifier);
        printf(":\n");
#endif

    if (strncmp(header.identifier, "JFIF", 4) == 0) {
        jpeg_app0_jfif_t* jfif = &state->jfif;
        read_u8(file, &jfif->jfif_version_major);
        read_u8(file, &jfif->jfif_version_minor);
        read_u8(file, &jfif->density_units);
        read_u16(file, &jfif->density_w, JPEG_BYTE_ORDER_BE);
        read_u16(file, &jfif->density_h, JPEG_BYTE_ORDER_BE);
        read_u8(file, &jfif->thumbnail_w);
        read_u8(file, &jfif->thumbnail_h);

        const size_t app0_size = 7;
        const size_t jfif_size = 9;
        const size_t thumbnail_size = header.length - app0_size - jfif_size;

        if (thumbnail_size != 0) {
            state->jfif_thumbnail_pixels = mem_alloc(thumbnail_size, state);
            read_bytes(file, state->jfif_thumbnail_pixels, thumbnail_size);
            state->n_jfif_thumbnail_bytes = thumbnail_size;
        }

        state->has_jfif = 1;
        
#if DEBUG_VERBOSE
            printf("\t\t.jfif_version_major = %i\n", jfif->jfif_version_major);
            printf("\t\t.jfif_version_minor = %i\n", jfif->jfif_version_minor);
            printf("\t\t.density_units      = %i\n", jfif->density_units);
            printf("\t\t.density_w          = %i\n", jfif->density_w);
            printf("\t\t.density_h          = %i\n", jfif->density_h);
            printf("\t\t.thumbnail_w        = %i\n", jfif->thumbnail_w);
            printf("\t\t.thumbnail_h        = %i\n", jfif->thumbnail_h);
#endif
    }
    else if (strncmp(header.identifier, "JFXX", 4) == 0) {
        jpeg_app0_jfxx_t* jfxx = &state->jfxx;

        read_u8(file, &jfxx->thumbnail_format);        

        const size_t app0_size = 7;
        const size_t jfxx_size = 1;
        fseek(file, header.length - app0_size - jfxx_size, SEEK_CUR);

        state->has_jfxx = 1;

#if DEBUG_VERBOSE
            printf("\t\t.thumbnail_format   = %i\n", jfxx->thumbnail_format);
#endif
    }
    else {
        ERROR("Invalid APP0 identifier");
    }
    return (size_t)header.length;
}

size_t parse_jfif_app1(FILE* file, jpeg_state_t* state) {
    read_u16(file, &state->exif.length, JPEG_BYTE_ORDER_BE);

    // read identifier
    uint8_t curr_byte = 0;

    size_t id_len_max = sizeof(state->exif.identifier);

    for (size_t i = 0; i < id_len_max - 1; ++i) {
        read_u8(file, &curr_byte);
        state->exif.identifier[i] = (char)curr_byte;

        if (curr_byte == 0) break;
    }

    if (strncmp(state->exif.identifier, "Exif", id_len_max) == 0) {
        if (state->has_exif) {
            #if DEBUG
                printf("double Exif marker found! ignoring second and beyond.\n");
            #endif
            return (size_t)state->exif.length;
        }
        read_u8(file, &state->exif._pad);
        read_u16(file, &state->exif.byte_order, JPEG_BYTE_ORDER_BE);
        read_u16(file, &state->exif.tiff_identifier, JPEG_BYTE_ORDER_BE);
        read_s32(file, &state->exif.ifd_offset, JPEG_BYTE_ORDER_BE);

        // todo: exif parsing

        state->has_exif = 1;

        #if DEBUG_VERBOSE
            printf("APP1:\n");
            printf("\t.length          = %i\n", state->exif.length);
            printf("\t.identifier      = %.*s\n", 5, (char*)state->exif.identifier);
            printf("\t.byte_order      = %.*s\n", 2, (char*)&state->exif.byte_order);
            printf("\t.tiff_identifier = %i\n", state->exif.tiff_identifier);
            printf("\t.ifd_offset      = %i\n", state->exif.ifd_offset);
        #endif
    }
    // todo: APP1 with other identifiers

    return (size_t)state->exif.length;
}

size_t parse_jfif_app14(FILE* file, jpeg_state_t* state) {
    jpeg_app14_t header = {0};
    read_u16(file, &header.length, JPEG_BYTE_ORDER_BE);
    read_bytes(file, &header.identifier[0], 5);
    read_u16(file, &header.version, JPEG_BYTE_ORDER_BE);
    read_u16(file, &header.flags0, JPEG_BYTE_ORDER_BE);
    read_u16(file, &header.flags1, JPEG_BYTE_ORDER_BE);
    uint8_t color_transform = 0;
    read_u8(file, &color_transform);
    header.color_transform = (jpeg_app14_color_transform_t)color_transform;
    state->app14 = header;
    state->has_app14 = 1;
    return header.length;
}

size_t parse_comment(FILE* file) {
    uint16_t length = 0;
    read_u16(file, &length, JPEG_BYTE_ORDER_BE);
    // todo: read comment
    return length;
}

size_t parse_jfif_app_todo(FILE* file) {
    uint16_t length = 0;
    read_u16(file, &length, JPEG_BYTE_ORDER_BE);
    return length;
}

size_t parse_start_of_frame(FILE* file, jpeg_state_t* state, uint8_t marker) {
    jpeg_start_of_frame_t* header = &state->start_of_frame;
    read_u16(file, &header->length, JPEG_BYTE_ORDER_BE);
    read_u8(file, &header->bits_per_pixel);
    read_u16(file, &header->height, JPEG_BYTE_ORDER_BE);
    read_u16(file, &header->width, JPEG_BYTE_ORDER_BE);
    read_u8(file, &header->n_components);

    const int mode = (marker >> 0) & 0x03;
    const int coding = (marker >> 3) & 0x01;
    const int differential = (marker >> 2) & 0x01;

    if (coding != 0) ERROR("arithmetic-coded jpeg files not supported");
    if (differential != 0) ERROR("differential jpeg files not supported");

    if (header->n_components == 0) ERROR("scan has no components");

    if (mode == JPEG_SCAN_MODE_BASELINE) {
        if (header->bits_per_pixel != 8) ERROR("scan has invalid bits per pixel");
    }
    else if (mode == JPEG_SCAN_MODE_EXTENDED) {
        if (header->bits_per_pixel != 8 && header->bits_per_pixel != 12) ERROR("scan has invalid bits per pixel");
    }
    else if (mode == JPEG_SCAN_MODE_PROGRESSIVE) {
        if (header->bits_per_pixel != 8 && header->bits_per_pixel != 12) ERROR("scan has invalid bits per pixel");
        if (header->n_components > 4) ERROR("progressive scan can not have more than 4 components")
    }
    else /* if (mode == JPEG_SCAN_MODE_LOSSLESS) */ {
        if (header->bits_per_pixel < 2 || header->bits_per_pixel > 16) ERROR("lossless scan bit depth must be >= 2 and <= 16");
        ERROR("lossless jpeg not yet implemented");
    }

#if DEBUG_VERBOSE
        const char* names_mode[] = {"base", "extended", "progressive", "lossless"};
        const char* names_differential[] = {"non-differential", "differential"};
        const char* names_coding[] = {"huffman", "arithmetic"};
        printf("START OF FRAME:\n");
        printf("\ttype: %02X (%s mode, %s, %s-coding", marker, 
            names_mode[mode],
            names_differential[differential],
            names_coding[coding]
        );
        printf(")\n");

        printf("\t.length         = %i\n", header->length);
        printf("\t.bits_per_pixel = %i\n", header->bits_per_pixel);
        printf("\t.height         = %i\n", header->height);
        printf("\t.width          = %i\n", header->width);
        printf("\t.n_components   = %i\n", header->n_components);
#endif

    uint8_t smp_factor_max_w = 0;
    uint8_t smp_factor_max_h = 0;

    for (size_t i = 0; i < (size_t)header->n_components; ++i) {
        jpeg_channel_t component = {0};
        read_u8(file, &component.id);
        read_u8(file, &component.blocks_per_mcu.u8);
        read_u8(file, &component.quant_table_id);

        if (component.blocks_per_mcu.width == 0 || component.blocks_per_mcu.width > 4) {
            ERROR("invalid horizontal blocks per MCU");
        }
        if (component.blocks_per_mcu.height == 0 || component.blocks_per_mcu.height > 4) {
            ERROR("invalid vertical blocks per MCU");
        }
        if (mode != JPEG_SCAN_MODE_LOSSLESS && component.quant_table_id > 3) {
            ERROR("invalid quant table id");
        }
        if (mode == JPEG_SCAN_MODE_LOSSLESS && component.quant_table_id != 0) {
            ERROR("invalid quant table id");
        }

        if (component.blocks_per_mcu.width > smp_factor_max_w) {
            smp_factor_max_w = component.blocks_per_mcu.width;
        }
        if (component.blocks_per_mcu.height > smp_factor_max_h) {
            smp_factor_max_h = component.blocks_per_mcu.height;
        }
        
#if DEBUG_VERBOSE
        printf("\t\tCHANNEL %i:\n", (int)i);
        printf("\t\t\t.id              =  %i\n", component.id);
        printf("\t\t\t.blocks_per_mcu  =  %ix%i\n", component.blocks_per_mcu.width, component.blocks_per_mcu.height);
        printf("\t\t\t.quant_table_id  =  %i\n", component.quant_table_id);
#endif

        component.dc_prev = 0;
        component.block_index = 0;

        // block array will be allocated later, since we need to know more about the MCUs

        state->components[(size_t)component.id] = component;
        state->component_mapping[state->n_components++] = component.id;
    }

    state->mcu_width = smp_factor_max_w * BLOCK_RES;
    state->mcu_height = smp_factor_max_h * BLOCK_RES;
    state->n_mcu_x = (header->width + state->mcu_width - 1) / state->mcu_width;
    state->n_mcu_y = (header->height + state->mcu_height - 1) / state->mcu_height;
    state->has_chn = 1;
    state->has_sof = 1;

#if DEBUG_VERBOSE
        printf("\t\tMCU size: %ix%i\n", (int)state->mcu_width, (int)state->mcu_height);
#endif
    
    return (size_t)header->length;
}

size_t parse_quant_table(FILE* file, jpeg_state_t* state, int do_decode) {
    jpeg_quant_table_t header = {0};
    read_u16(file, &header.length, JPEG_BYTE_ORDER_BE);
    
    if (!do_decode) { // skip parsing for metadata only pass
        return (size_t)header.length;
    }

    int bytes_left = (int)header.length - 2;

    while (bytes_left) {
        read_u8(file, &header.table_id);
        const uint8_t table_id = header.table_id & 0x0F;
        const uint8_t precision = header.table_id >> 4;
        if (precision > 1) {
            #if DEBUG
                printf("precision %i\n", header.table_id);
            #endif
            ERROR("invalid table precision");
        }

        const size_t table_buf_size = BLOCK_RES2 + (BLOCK_RES2 * precision);

        // allocate new quantization table
        if (state->n_quant_tables <= table_id) {
            state->n_quant_tables = table_id + 1;
            state->quant_tables = mem_realloc(state->quant_tables, sizeof(*state->quant_tables) * (state->n_quant_tables), state);
        }
        state->quant_tables[table_id] = mem_alloc(table_buf_size, state);

        read_bytes(file, state->quant_tables[table_id], table_buf_size);
        bytes_left -= (table_buf_size + 1);

        #if DEBUG_VERBOSE
                printf("QUANT TABLE:\n");
                printf("\t.length          = %i\n", header.length);
                printf("\t.table_id        = %i\n", table_id);
                printf("\t.precision       = %i\n", ((precision + 1) * 8));
                printf("\ttable values: (%i bytes)\n\t\t", (int)table_buf_size);
                for (size_t i = 0; i < table_buf_size; ++i) {
                    if (i && i % BLOCK_RES == 0) printf("\n\t\t");
                    printf("%3i, ", state->quant_tables[table_id][i]);
                }
                printf("\n");
        #endif
    }

    state->has_quant = 1;

    return (size_t)header.length;
}

size_t parse_restart_interval(FILE* file, jpeg_state_t* state) {
    (void)state;
    uint16_t length = 0;
    read_u16(file, &length, JPEG_BYTE_ORDER_BE);
    read_u16(file, &state->restart_interval, JPEG_BYTE_ORDER_BE);
    return (size_t)length;
}


size_t parse_huffman_table(FILE* file, jpeg_state_t* state, int do_decode) {
    const size_t header_start = ftell(file);

    jpeg_huffman_table_header_t header = {0};
    read_u16(file, &header.length, JPEG_BYTE_ORDER_BE);

    if (!do_decode) { // skip parsing for metadata only pass
        return (size_t)header.length;
    }

    const size_t header_end = header_start + header.length;

    while (ftell(file) < (intptr_t)header_end) {
        read_u8(file, &header.class_id);
        read_bytes(file, &header.bit_lengths[0], sizeof(header.bit_lengths));

        jpeg_huffman_table_decoded_t huff_tbl;
        huff_tbl.table_buf_size = 0;
        for (size_t i = 0; i < sizeof(header.bit_lengths); ++i) {
            huff_tbl.table_buf_size += header.bit_lengths[i];
        }

        huff_tbl.symbols = mem_alloc(huff_tbl.table_buf_size, state);
        huff_tbl.codes = mem_alloc(huff_tbl.table_buf_size * sizeof(uint32_t), state);
        huff_tbl.code_lengths = mem_alloc(huff_tbl.table_buf_size, state);
        
        memset(huff_tbl.symbols, 0, huff_tbl.table_buf_size);
        memset(huff_tbl.codes, 0, huff_tbl.table_buf_size * sizeof(uint32_t));
        memset(huff_tbl.code_lengths, 0, huff_tbl.table_buf_size);
        read_bytes(file, huff_tbl.symbols, huff_tbl.table_buf_size);

        const size_t lut_size = (1 << 16) * sizeof(jpeg_huffman_lut_entry_t);
        huff_tbl.left_shifted_code_lut = mem_alloc(lut_size, state);
        memset(huff_tbl.left_shifted_code_lut, 0, lut_size);

        // decode table
        size_t length = 1;
        size_t code = 0;
        size_t symbol_id = 0;

        for (size_t i_bit_lengths = 0; i_bit_lengths < sizeof(header.bit_lengths); ++i_bit_lengths) {
            for (size_t i = 0; i < header.bit_lengths[i_bit_lengths]; ++i) {
                const size_t code_start = (code++) << (15 - i_bit_lengths);
                const size_t code_end = ((code) << (15 - i_bit_lengths));
                
                huff_tbl.code_lengths[symbol_id] = length;
                huff_tbl.codes[symbol_id] = code_start;
                
                for (size_t i_lut = code_start; i_lut < code_end; ++i_lut) {
                    huff_tbl.left_shifted_code_lut[i_lut].length = i_bit_lengths + 1;
                    huff_tbl.left_shifted_code_lut[i_lut].symbol = huff_tbl.symbols[symbol_id];
                }
                ++symbol_id;
            }
            code <<= 1;
            ++length;
        }
        
        if (header.class == 0) { // DC
            state->huffman_tables_dc[header.id] = huff_tbl;
        }
        else if (header.class == 1) { // AC
            state->huffman_tables_ac[header.id] = huff_tbl;
        }
        else {
            ERROR("Invalid huffman table class");
        }

        #if DEBUG_VERBOSE
            printf("HUFFMAN TABLE:\n");
            printf("\t.length = %i\n", header.length);
            printf("\t.class  = %i\n", header.class);
            printf("\t.id     = %i\n", header.id);
            printf("\tlengths:\n");
            for (size_t i = 0; i < sizeof(header.bit_lengths); ++i) {
                printf("\t\t%2i - %2X", (int)i, header.bit_lengths[i]);
                printf("\n");
            }
            printf("\tvalues:\n");
            for (size_t i = 0; i < huff_tbl.table_buf_size; ++i) {
                printf("\t\t%02X - ", huff_tbl.symbols[i]);
                for (int j = 15; j >= 0; --j) {
                    printf("%i", (huff_tbl.codes[i] >> j) & 1);
                }
                printf("\t(%i)\n", huff_tbl.codes[i]);
            }
        #endif
    }

    state->has_huff_tbl = 1;
    return (size_t)header.length;
}

int32_t parse_raw_value(int32_t value, int32_t n_bits) {
    int32_t msb = (1 << (n_bits-1));
    if ((value & msb) == 0) {
        value = value - (1 << n_bits) + 1;
    }
    return value;
}

void fetch_coefficients(FILE* file, bit_stream_t* stream, jpeg_state_t* state, size_t scan_comp_id, size_t block_index, size_t ss_start, size_t ss_end) {
    // find component info and tables
    jpeg_component_t* component_huff = &state->start_of_scan.components[scan_comp_id];
    jpeg_huffman_table_decoded_t* huff_tbl_dc = &state->huffman_tables_dc[(size_t)component_huff->tables.dc];
    jpeg_huffman_table_decoded_t* huff_tbl_ac = &state->huffman_tables_ac[(size_t)component_huff->tables.ac];
    jpeg_channel_t* component_info = &state->components[(size_t)component_huff->id];

    // lazy-allocate the buffer array for this component
    if (component_info->blocks == NULL) {
        const size_t n_mcu = state->n_mcu_x * state->n_mcu_y;
        const size_t blocks_per_mcu = (size_t)component_info->blocks_per_mcu.width * (size_t)component_info->blocks_per_mcu.height;
        const size_t n_bytes = n_mcu * blocks_per_mcu * sizeof(jpeg_block_t);
        component_info->blocks = mem_alloc(n_bytes, state);
        memset(component_info->blocks, 0, n_bytes);
    }
    
    jpeg_block_t* curr_block = &component_info->blocks[block_index];

    jpeg_huffman_lut_entry_t lut_entry;
    size_t spectral_index = ss_start;

    const int32_t ah = (int32_t)state->start_of_scan.successive_approximation >> 4;
    const int32_t al = (int32_t)state->start_of_scan.successive_approximation & 0x0F;

    const int first_visit = (ah == 0);

    if (first_visit && (state->eob_run > 0)) {
        --state->eob_run;
        return;
    }

    // first visit
    if (first_visit) {
        // DC component
        if (spectral_index == 0) {
            // read huffman symbol
            const size_t left_shifted_code = (size_t)bit_stream_get_next_bits(file, stream, 16, 0);
            jpeg_huffman_lut_entry_t lut_entry = huff_tbl_dc->left_shifted_code_lut[left_shifted_code];
            bit_stream_get_next_bits(file, stream, (size_t)lut_entry.length, 1);
            print_binary(left_shifted_code, 15, lut_entry.length);

            // read value
            const size_t size = (size_t)lut_entry.symbol;
            const int32_t raw_bits = bit_stream_get_next_bits(file, stream, size, 1);
            int32_t value = parse_raw_value(raw_bits, size);
            print_binary(raw_bits, size - 1, size);
            
            // apply dpcm and store
            value += component_info->dc_prev;
            component_info->dc_prev = value;
            curr_block->coefficients[0] = (value << al);
            ++spectral_index;
        }

        // AC components
        while (spectral_index <= ss_end) {
            // read huffman symbol
            const size_t left_shifted_code = (size_t)bit_stream_get_next_bits(file, stream, 16, 0);
            lut_entry = huff_tbl_ac->left_shifted_code_lut[left_shifted_code];
            bit_stream_get_next_bits(file, stream, (size_t)lut_entry.length, 1);
            print_binary(left_shifted_code, 15, lut_entry.length);

            // end of block run
            if (((lut_entry.symbol & 0x0F) == 0) && (lut_entry.symbol != 0xF0)) {
                const size_t size = (size_t)(lut_entry.symbol >> 4);
                if (size == 0) return;

                const uint32_t raw_bits = bit_stream_get_next_bits(file, stream, size, 1);
                const uint32_t run_length = raw_bits + (1 << size);
                state->eob_run = (size_t)run_length - 1; // this block is included
                return;
            }
            
            // otherwise, symbol is split into 0xRS, with R = run length, S = size, as in number of bits to read next
            size_t run_length = (size_t)(lut_entry.symbol >> 4);
            const size_t size = (size_t)(lut_entry.symbol & 0x0F);
            const uint32_t raw_bits = bit_stream_get_next_bits(file, stream, size, 1);
            print_binary(raw_bits, size - 1, size);

            // rle and store value
            spectral_index += run_length;
            if (spectral_index > ss_end) return;
            curr_block->coefficients[spectral_index] += parse_raw_value(raw_bits, size) << al;
            ++spectral_index;
        }
    }

    // refinement
    else {
        // DC component
        if (spectral_index == 0) {
            const int32_t refinement_bit = bit_stream_get_next_bits(file, stream, 1, 1);
            print_binary(refinement_bit, 0, 1);
            curr_block->coefficients[0] |= (refinement_bit << al);
            ++spectral_index;
        }

        // AC components
        const int pos = 1 << al;
        const int neg = -(1 << al);
        if (state->eob_run == 0) {

            while (spectral_index <= ss_end) {
                // read huffman symbol
                const size_t left_shifted_code = (size_t)bit_stream_get_next_bits(file, stream, 16, 0);
                lut_entry = huff_tbl_ac->left_shifted_code_lut[left_shifted_code];
                bit_stream_get_next_bits(file, stream, (size_t)lut_entry.length, 1);
                print_binary(left_shifted_code, 15, lut_entry.length);

                // symbol is split into 0xRS, with R = run length, S = size, as in number of bits to read next
                size_t run_length = (size_t)(lut_entry.symbol >> 4);
                const size_t size = (size_t)(lut_entry.symbol & 0x0F);

                int32_t value = 0;

                // refinement bit
                if (size == 1) {
                    const int32_t refinement_bit = bit_stream_get_next_bits(file, stream, 1, 1);
                    print_binary(refinement_bit, 0, 1);

                    value = (refinement_bit == 1) ? pos : neg;
                }

                // rle
                else if (size == 0) {
                    // full zero run
                    if (lut_entry.symbol == 0xF0) {
                    }
                    // end of band run
                    else {
                        const size_t size = (size_t)(lut_entry.symbol >> 4);
                        if (size == 0) return;

                        const uint32_t raw_bits = bit_stream_get_next_bits(file, stream, size, 1);
                        const uint32_t run_length = raw_bits + (1 << size);
                        state->eob_run += (size_t)run_length - 1;
                        return;
                    }
                }

                else {
                    ERROR("invalid symbol refinement scan");
                }
                
                // handle run length
                while (run_length > 0) {
                    if (curr_block->coefficients[spectral_index] != 0) {
                        const int32_t refinement_bit = bit_stream_get_next_bits(file, stream, 1, 1);
                        print_binary(refinement_bit, 0, 1);

                        if (curr_block->coefficients[spectral_index] >= 0) {
                            curr_block->coefficients[spectral_index] += pos;
                        }
                        else {
                            curr_block->coefficients[spectral_index] += neg;
                        }
                    }

                    ++spectral_index;
                    --run_length;
                    continue;
                }

                if ((value != 0) && (spectral_index <= ss_end)) {
                    curr_block->coefficients[spectral_index++] = value;
                }
            }
        }

        // refinement eob runs still have refinement bits
        else if (state->eob_run > 0) {
            while (spectral_index < ss_end) {
                if (curr_block->coefficients[spectral_index] != 0) {
                    const int32_t refinement_bit = bit_stream_get_next_bits(file, stream, 1, 1);
                    print_binary(refinement_bit, 0, 1);

                    if (curr_block->coefficients[spectral_index] >= 0) {
                        curr_block->coefficients[spectral_index] += pos;
                    }
                    else {
                        curr_block->coefficients[spectral_index] += neg;
                    }
                }
                ++spectral_index;
            }
            --state->eob_run;
        }
    }
}

int handle_restarts(FILE* file, jpeg_state_t* state, bit_stream_t* stream) {
    int has_restarts = (state->restart_interval > 0);
    int n_mcus_before_restart = state->restart_interval;
    int did_restart = 0;
    if (has_restarts) {
        if (n_mcus_before_restart == 0) {
            #if DEBUG
            if (!stream->hold) {
                printf("desync between restart markers and restart interval?\n");
                printf("ftell() %i\n", (int)ftell(file));
            }
            #endif
            
            // restart the bitstream on the byte after the restart marker
            if (stream->hold == HOLD_RESTART) {
                stream->hold = HOLD_CONTINUE;
                bit_stream_init(stream);
                for (size_t i = 0; i < state->n_components; ++i) {
                    size_t mapping = state->component_mapping[i];
                    state->components[mapping].dc_prev = 0;
                }
                did_restart = 1;
            }
            n_mcus_before_restart += state->restart_interval;
        }
        --n_mcus_before_restart;
    }
    return did_restart;
}

size_t parse_start_of_scan(FILE* file, jpeg_state_t* state, int do_decode) {
    jpeg_start_of_scan_t header = {0};
    read_u16(file, &header.length, JPEG_BYTE_ORDER_BE);
    
    const size_t marker_data_start = ftell(file);

    if (!do_decode) { // skip parsing for metadata only pass
        return (size_t)header.length;
    }

    // what are we getting in this scan
    read_u8(file, &header.n_components);

    const size_t n_bytes_components = sizeof(jpeg_component_t) * (size_t)header.n_components;
    jpeg_component_t* components = mem_alloc(n_bytes_components, state);
    read_bytes(file, components, n_bytes_components);

    read_u8(file, &header.spectral_selection_min);
    read_u8(file, &header.spectral_selection_max);
    read_u8(file, &header.successive_approximation);

    if (header.n_components == 0 || header.n_components > 4) {
        ERROR("invalid number of components in scan")
    }
    if (header.spectral_selection_min >= BLOCK_RES2 || header.spectral_selection_max >= BLOCK_RES2) {
        ERROR("invalid spectral selection for scan (selection exceeds block resolution)");
    }
    if (header.spectral_selection_min > header.spectral_selection_max) {
        ERROR("invalid spectral selection for scan (min is greater than max)");
    }

    header.components = components;
    state->start_of_scan = header;
    state->has_sos = 1;

#if DEBUG_VERBOSE
        printf("START OF SCAN:\n");
        printf("\t.length                   = %i\n", header.length);
        printf("\t.n_components             = %i\n", header.n_components);
        for (size_t i = 0; i < (size_t)header.n_components; ++i) {
            printf("\t\tid: %03i,\tac:%02i,\tdc:%02i\n", components[i].id, components[i].tables.ac, components[i].tables.dc);
        }
        printf("\t.spectral_selection_min   = %i\n", header.spectral_selection_min);  
        printf("\t.spectral_selection_max   = %i\n", header.spectral_selection_max)  ;
        printf("\t.successive_approximation = %i\n", header.successive_approximation);
#endif

    // decode scan
    if (!state->has_chn) ERROR("no channel info found!");
    if (!state->has_quant) ERROR("no quantization tables found!");
    if (!state->has_huff_tbl) ERROR("no huffman tables found!");
    if (!state->has_sof) ERROR("no start of frame segment found!");

    bit_stream_t stream;
    bit_stream_init(&stream);

    int n_block_to_skip = 0;

    // handle interleaved scan
    if (state->start_of_scan.n_components > 1) {
        const size_t n_mcu_x = state->n_mcu_x;
        const size_t n_mcu_y = state->n_mcu_y;

        for (size_t mcu_y = 0; mcu_y < n_mcu_y; ++mcu_y) {
            for (size_t mcu_x = 0; mcu_x < n_mcu_x; ++mcu_x) {
                if (handle_restarts(file, state, &stream)) {
                    state->eob_run = 0;
                }
                
                // populate spectral band
                for (size_t comp_id = 0; comp_id < state->start_of_scan.n_components; ++comp_id) {
                    jpeg_component_t component_huff = state->start_of_scan.components[comp_id];
                    jpeg_channel_t* component_info = &state->components[(size_t)component_huff.id];
                    
                    const size_t width = (size_t)component_info->blocks_per_mcu.width;
                    const size_t height = (size_t)component_info->blocks_per_mcu.height;
                    
                    #if DEBUG_VERBOSE
                    printf("\tBLOCK MCU: %02X\n", component_info.blocks_per_mcu.u8);
                    #endif
                    
                    for (size_t block_y = 0; block_y < height; ++block_y) {
                        for (size_t block_x = 0; block_x < width; ++block_x) {
                            const size_t unswizzled_block_x = ((mcu_x * width) + block_x);
                            const size_t unswizzled_block_y = ((mcu_y * height) + block_y);
                            const size_t unswizzled_block_index = unswizzled_block_x + (unswizzled_block_y * n_mcu_x * width);

                            fetch_coefficients(file, &stream, state, comp_id,
                                unswizzled_block_index,
                                state->start_of_scan.spectral_selection_min, 
                                state->start_of_scan.spectral_selection_max
                            );
                        }
                    }
                }
            }
        }
    }

    // handle single component scan
    else if (state->start_of_scan.n_components == 1) {
        jpeg_component_t component_huff = state->start_of_scan.components[0];
        jpeg_channel_t* component_info = &state->components[(size_t)component_huff.id];

        const size_t n_block_x = state->n_mcu_x * (size_t)component_info->blocks_per_mcu.width;
        const size_t n_block_y = state->n_mcu_y * (size_t)component_info->blocks_per_mcu.height;

        for (size_t block_y = 0; block_y < n_block_y; ++block_y) {
            for (size_t block_x = 0; block_x < n_block_x; ++block_x) {
                if (handle_restarts(file, state, &stream)) {
                    state->eob_run = 0;
                }

                const size_t unswizzled_block_index = (block_y * n_block_x) + block_x;
                
                printf("unswizzled_block_index: %i (block at %i, %i out of %i, %i)\n", (int)unswizzled_block_index, (int)block_x * 8, (int)block_y * 8, (int)n_block_x * 8, (int)n_block_y * 8);
                            
                fetch_coefficients(file, &stream, state, 0,
                    unswizzled_block_index,
                    state->start_of_scan.spectral_selection_min, 
                    state->start_of_scan.spectral_selection_max
                );
            }
        }
    }
    else ERROR("invalid number of components");

    const size_t marker_data_end = ftell(file);
    return marker_data_end - marker_data_start;
}

void unzigzag(FLOAT *restrict block, jpeg_state_t* state) {
    const size_t n_values = (BLOCK_RES2);

    for (size_t i = 0; i < n_values; ++i) {
        ((FLOAT*)state->scratch_buffer)[i] = block[lut_zigzag[i]];        
    }

    memcpy(block, state->scratch_buffer, (BLOCK_RES2) * sizeof(FLOAT));
}

void dequantize(FLOAT *restrict block, uint8_t* quant) {
    for (size_t i = 0; i < (BLOCK_RES2); ++i) {
        block[i] *= (FLOAT)quant[i];
    }
}

void generate_luts(jpeg_state_t* state) {
    for (size_t u = 0; u < BLOCK_RES; ++u) {
        for (size_t x = 0; x < BLOCK_RES; ++x) {
            const FLOAT dct_value = cosf((((2.0 * (FLOAT)x) + 1.0) * (FLOAT)u * pi) / (2.0 * (FLOAT)BLOCK_RES));
            lut_dct[(BLOCK_RES * u) + x] = dct_value;
        }
    }

    typedef enum {
        ENTROPY_DEC_STEP_NONE = 0,
        ENTROPY_DEC_STEP_RIGHT_ELSE_DOWN,
        ENTROPY_DEC_STEP_DOWN_LEFT,
        ENTROPY_DEC_STEP_DOWN_ELSE_RIGHT,
        ENTROPY_DEC_STEP_UP_RIGHT,
    } jpeg_entropy_decoding_step_t;
    jpeg_entropy_decoding_step_t curr_step = ENTROPY_DEC_STEP_RIGHT_ELSE_DOWN;
    const int n_values = BLOCK_RES2;
    const size_t edge = BLOCK_RES - 1;
    size_t x = 0;
    size_t y = 0;

    for (int i = 0; i < n_values; ++i) {
        const size_t dst_i = (y * BLOCK_RES) + x;
        lut_zigzag[dst_i] = i;

        // move to next value
        switch (curr_step) {
        case ENTROPY_DEC_STEP_RIGHT_ELSE_DOWN: 
            if (x < edge) ++x;
            else if (y < edge) ++y; 
            else ERROR("Error entropy decoding");
            curr_step = ENTROPY_DEC_STEP_DOWN_LEFT;
            break;
        case ENTROPY_DEC_STEP_DOWN_LEFT:
            --x; ++y;
            if (x == 0 || y == edge) curr_step = ENTROPY_DEC_STEP_DOWN_ELSE_RIGHT;
            break;
        case ENTROPY_DEC_STEP_DOWN_ELSE_RIGHT:
            if (y < edge) ++y;
            else if (x < edge) ++x;
            else ERROR("Error entropy decoding");
            curr_step = ENTROPY_DEC_STEP_UP_RIGHT;
            break;
        case ENTROPY_DEC_STEP_UP_RIGHT:
            ++x; --y;
            if (x == edge || y == 0) curr_step = ENTROPY_DEC_STEP_RIGHT_ELSE_DOWN;
            break;
        default: ERROR("Error entropy decoding");
        }
    }
    
    state->has_luts = 1;
}

void idct_2d(FLOAT *restrict block, jpeg_state_t *state) {
    FLOAT *restrict scratch_buffer = (FLOAT*)state->scratch_buffer;
    // colums
    for (int x = 0; x < BLOCK_RES; ++x) {
        for (int y = 0; y < BLOCK_RES; ++y) {
            FLOAT sum = 0.0f;
            for (int u = 0; u < BLOCK_RES; ++u) {
                const FLOAT c = (u == 0) ? sqrtf(1.0f / BLOCK_RES) : sqrtf(2.0f / BLOCK_RES);
                sum += c * block[u * BLOCK_RES + x] * lut_dct[(BLOCK_RES * u) + y];
            }
            scratch_buffer[y * BLOCK_RES + x] = sum;
        }
    }

    // rows
    for (int y = 0; y < BLOCK_RES; ++y) {
        for (int x = 0; x < BLOCK_RES; ++x) {
            FLOAT sum = 0.0f;
            for (int u = 0; u < BLOCK_RES; ++u) {
                const FLOAT c = (u == 0) ? sqrtf(1.0f / BLOCK_RES) : sqrtf(2.0f / BLOCK_RES);
                sum += c * scratch_buffer[y * BLOCK_RES + u] * lut_dct[(BLOCK_RES * u) + x];
            }
            block[y * BLOCK_RES + x] = sum;
        }
    }
}

void debug_block(const FLOAT* block) {
#if DEBUG_VERBOSE
    printf("BLOCK VALUES:\n");
    for (size_t i = 0; i < (BLOCK_RES2); ++i) {
        if(i % BLOCK_RES == 0) printf("\n");
        printf("%6.2f, ", block[i]);
    }
    printf("\n");
#else
    (void)block;
#endif
}

void add_div(FLOAT* arr, FLOAT to_add, FLOAT to_div, size_t count) { 
    for (size_t i = 0; i < count; ++i) {
        arr[i] += to_add;
        arr[i] /= to_div;
    } 
}

rgb8_t ycbcr_to_rgb(FLOAT y, FLOAT cb, FLOAT cr) {
    return (rgb8_t){
        .r = (uint8_t)fmin(255.0, fmax(0.0, y + 1.402    * (cr - 128.0))),
        .g = (uint8_t)fmin(255.0, fmax(0.0, y - 0.344136 * (cb - 128.0) - 0.714136 * (cr - 128.0))),
        .b = (uint8_t)fmin(255.0, fmax(0.0, y + 1.772    * (cb - 128.0))),
    };
}

// debug - stolen from https://stackoverflow.com/questions/2654480/writing-bmp-image-in-pure-c-c-without-other-libraries
void write_bmp(const char* path, const rgb8_t* img, int w, int h) {
    unsigned char bmpfileheader[14] = {'B','M', 0,0,0,0, 0,0, 0,0, 54,0,0,0};
    unsigned char bmpinfoheader[40] = {40,0,0,0, 0,0,0,0, 0,0,0,0, 1,0, 24,0};
    unsigned char bmppad[3] = {0,0,0};
    int filesize = 54 + 3*w*h;

    bmpfileheader[ 2] = (unsigned char)(filesize    );
    bmpfileheader[ 3] = (unsigned char)(filesize>> 8);
    bmpfileheader[ 4] = (unsigned char)(filesize>>16);
    bmpfileheader[ 5] = (unsigned char)(filesize>>24);

    bmpinfoheader[ 4] = (unsigned char)(       w    );
    bmpinfoheader[ 5] = (unsigned char)(       w>> 8);
    bmpinfoheader[ 6] = (unsigned char)(       w>>16);
    bmpinfoheader[ 7] = (unsigned char)(       w>>24);
    bmpinfoheader[ 8] = (unsigned char)(       h    );
    bmpinfoheader[ 9] = (unsigned char)(       h>> 8);
    bmpinfoheader[10] = (unsigned char)(       h>>16);
    bmpinfoheader[11] = (unsigned char)(       h>>24);

    FILE* f = fopen(path,"wb");
    fwrite(bmpfileheader,1,14,f);
    fwrite(bmpinfoheader,1,40,f);
    for(int i=0; i<h; i++) {
        fwrite(((uint8_t*)img)+(w*(h-i-1)*3),3,w,f);
        fwrite(bmppad,1,(4-(w*3)%4)%4,f);
    }
    fclose(f);
}

#define ALIGN_UP(value, align) ((((value) + (align) - 1) / (align)) * (align))

void parse_image_data(jpeg_state_t* state) {
    // allocate full res planes per component, even for downsampled planes (we will upscale them afterwards)
    FLOAT* planes[4] = {0};
    const size_t out_w = state->start_of_frame.width;
    const size_t out_h = state->start_of_frame.height;
    size_t padded_width = ALIGN_UP(out_w, state->mcu_width);
    size_t padded_height = ALIGN_UP(out_h, state->mcu_height);
    const size_t bytes_per_plane = padded_width * padded_height * sizeof(FLOAT);

    for (size_t comp_id = 0; comp_id < state->n_components; ++comp_id) {
        if (comp_id >= 4) break;

        jpeg_channel_t component_info = state->components[state->component_mapping[comp_id]];

        size_t n_block_x = state->n_mcu_x * component_info.blocks_per_mcu.width;
        size_t n_block_y = state->n_mcu_y * component_info.blocks_per_mcu.height;

        planes[comp_id] = mem_alloc(bytes_per_plane, state);
        memset(planes[comp_id], 0, bytes_per_plane);

        uint8_t* quant = state->quant_tables[(size_t)component_info.quant_table_id];

        size_t block_id = 0;
        for (size_t block_y = 0; block_y < n_block_y; ++block_y) {
            for (size_t block_x = 0; block_x < n_block_x; ++block_x) {
                jpeg_block_t* block = &component_info.blocks[block_id++];

                // convert to float
                FLOAT block_float[BLOCK_RES2];

                if (!component_info.blocks) {
                    memset(block_float, 0, sizeof(block_float));
                    ERROR("error decoding image data");
                }

                for (size_t i = 0; i < BLOCK_RES2; ++i) {
                    block_float[i] = (FLOAT)block->coefficients[i];
                }
            
                // 128.0 for 8 bit, 2048.0 for 12 bit
                const float to_add = (float)(1 << (state->start_of_frame.bits_per_pixel-1));

                // 1.0 for 8 bit, 16.0 for 12 bit
                const float to_div = (float)(1 << (state->start_of_frame.bits_per_pixel-8));

                dequantize(block_float, quant);
                unzigzag(block_float, state);
                idct_2d(block_float, state);
                add_div(block_float, to_add, to_div, BLOCK_RES2);
                debug_block(block_float);

                // blit to plane
                const size_t block_offset_x = block_x * BLOCK_RES;
                const size_t block_offset_y = block_y * BLOCK_RES;

                for (size_t pixel_y = 0; pixel_y < BLOCK_RES; ++pixel_y) {
                    for (size_t pixel_x = 0; pixel_x < BLOCK_RES; ++pixel_x) {
                        const size_t dst_x = block_offset_x + pixel_x;
                        const size_t dst_y = block_offset_y + pixel_y;
                        const size_t dst_i = (dst_y * padded_width) + dst_x;
                        
                        const size_t block_i = (pixel_y * BLOCK_RES) + pixel_x;
                        
                        planes[comp_id][dst_i] = block_float[block_i];
                    }
                }
            }
        }

        // upscale plane to full res
        const FLOAT src_pitch_x = (FLOAT)(BLOCK_RES * component_info.blocks_per_mcu.width) / (FLOAT)state->mcu_width;
        const FLOAT src_pitch_y = (FLOAT)(BLOCK_RES * component_info.blocks_per_mcu.height) / (FLOAT)state->mcu_height;

        if ((src_pitch_x != 1.0f) || (src_pitch_y != 1.0f)) {
            // reverse order because the non-upscaled data is in the top left, and then then i can write the  
            // upscaled image to the same buffer without overwriting the source data
            
            FLOAT *restrict img_comp = planes[comp_id];
            for (int dst_y = out_h - 1; dst_y >= 0; --dst_y) {
                const FLOAT src_y = (FLOAT)dst_y * src_pitch_y;
                for (int dst_x = out_w - 1; dst_x >= 0; --dst_x) {
                    const FLOAT src_x = (FLOAT)dst_x * src_pitch_x;
                    const size_t src_index = (((size_t)floor(src_y)) * padded_width) + (size_t)floor(src_x);
                    const size_t dst_index = (size_t)((dst_y * padded_width) + dst_x);
                    img_comp[dst_index] = img_comp[src_index];
                }
            }
        }
    }

    /* we need to figure out which component's which (spec does not clarify this afaict)
       - if app14 exists, check the transform:
           - 0:
               - 3 components: RGB
               - 4 components: CMYK
           - 1: 
               - 3 components: YCbCr
           - 2: 
               - 4 components: YCCK
       - else, if 3 components
           if one of the component's blocks per mcu is higher than the others, that's Y, otherwise don't assign Y
         then just sequentially fill up the components with Y, Cb, Cr, in that order, skipping ones you already have
    */

    int comp_y = -1;
    int comp_cb = -1;
    int comp_cr = -1;

    // explicit component definitions
    if (state->has_app14) {
        if (state->app14.color_transform == COLOR_TRANSFORM_UNKNOWN) {
            if (state->n_components == 3) { // RGB
                TODO()
            }
            if (state->n_components == 4) { // CMYK
                TODO()
            }
        }
        else if (state->app14.color_transform == COLOR_TRANSFORM_YCBCR) {
            if (state->n_components == 3) { // YCbCr
                comp_y = 0;
                comp_cb = 1;
                comp_cr = 2;
            }
        }
        else if (state->app14.color_transform == COLOR_TRANSFORM_YCCK) {
            if (state->n_components == 4) { // YCCK
                TODO()
            }
        }
        else {
            ERROR("invalid color transform in APP14 marker");
        }
    }
    // undefined, so we have to take our best guess
    else if (state->n_components == 3) {
        size_t lowest_n_mcu = SIZE_MAX;
        size_t highest_n_mcu = 0;
        size_t highest_comp_id = 0;

        for (size_t comp_id = 0; comp_id < state->n_components; ++comp_id) {
            const jpeg_channel_t* component_info = &state->components[state->component_mapping[comp_id]];
            const size_t curr_n_mcu = component_info->blocks_per_mcu.width * component_info->blocks_per_mcu.height;

            if (curr_n_mcu > highest_n_mcu) {
                highest_n_mcu = curr_n_mcu;
                highest_comp_id = comp_id;
            }
            if (curr_n_mcu < lowest_n_mcu) {
                lowest_n_mcu = curr_n_mcu;
            }
        }

        // not possible to guess based on sampling factor, assume YCbCr as fallback
        if (lowest_n_mcu == highest_n_mcu) { 
            comp_y = 0;
            comp_cb = 1;
            comp_cr = 2;
        }
        else {
            comp_y = highest_comp_id;
            for (int i = 0; i < 3; ++i) {
                if (comp_y == i) continue;
                else if (comp_cb == -1) comp_cb = i;
                else if (comp_cr == -1) comp_cr = i;
            }
        }
    }
    // grayscale
    else if (state->n_components == 1) {
        comp_y = 0;
    }
    else {
        ERROR("invalid image components");
    }

    rgb8_t *restrict out_image = mem_alloc(out_w * out_h * sizeof(rgb8_t), state);

    if ((comp_y >= 0) && (comp_cb >= 0) && (comp_cr >= 0)) {
        const FLOAT *restrict image_y_ = planes[comp_y];
        const FLOAT *restrict image_cb = planes[comp_cb];
        const FLOAT *restrict image_cr = planes[comp_cr];
        size_t pixel_index = 0;
        for (size_t y = 0; y < out_h; ++y) {
            for (size_t x = 0; x < out_w; ++x) {
                const FLOAT y_ = image_y_[pixel_index];
                const FLOAT cb = image_cb[pixel_index];
                const FLOAT cr = image_cr[pixel_index];
                out_image[(y * out_w) + x] = ycbcr_to_rgb(y_, cb, cr);
                ++pixel_index;
            }
            pixel_index += (padded_width - out_w);
        }
    }
    else {
        TODO();
    }

    state->out_image = out_image;
    state->out_width = out_w;
    state->out_height = out_h;

    return;
}

void cleanup(jpeg_state_t* state) {
    mem_free_all(state);
    memset(state, 0, sizeof(jpeg_state_t));
}

void handle_markers(FILE* in_file, jpeg_state_t* state, int do_decode) {
    state->scratch_buffer = mem_alloc(BLOCK_RES2 * sizeof(FLOAT), state);
    
    jpeg_marker_t marker;

    while (fread(&marker, sizeof(jpeg_marker_t), 1, in_file)) {
        if (marker.magic != JPEG_MARKER_PREFIX) break;

        size_t marker_start = ftell(in_file);
        size_t length = 0;

        #if DEBUG
        printf("marker = 0xFF%02X\n", marker.type);
        #endif

        switch (marker.type) {
            case JPEG_MARKER_START_OF_FRAME0:
            case JPEG_MARKER_START_OF_FRAME1:
            case JPEG_MARKER_START_OF_FRAME2:  length = parse_start_of_frame(in_file, state, marker.type); break;
            case JPEG_MARKER_HUFFMAN_TABLE:    length = parse_huffman_table(in_file, state, do_decode); break;
            case JPEG_MARKER_START_OF_IMAGE:   length = parse_start_of_image(state); break;
            case JPEG_MARKER_END_OF_IMAGE:     length = parse_end_of_image(); break;
            case JPEG_MARKER_START_OF_SCAN:    length = parse_start_of_scan(in_file, state, do_decode); break;
            case JPEG_MARKER_QUANT_TABLE:      length = parse_quant_table(in_file, state, do_decode); break;
            case JPEG_MARKER_RESTART_INTERVAL: length = parse_restart_interval(in_file, state); break;
            case JPEG_MARKER_JFIF_APP0:        length = parse_jfif_app0(in_file, state); break;
            case JPEG_MARKER_JFIF_APP1:        length = parse_jfif_app1(in_file, state); break;
            case JPEG_MARKER_JFIF_APP2:        length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP3:        length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP4:        length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP5:        length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP6:        length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP7:        length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP8:        length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP9:        length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP10:       length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP11:       length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP12:       length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP13:       length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_JFIF_APP14:       length = parse_jfif_app14(in_file, state); break;
            case JPEG_MARKER_JFIF_APP15:       length = parse_jfif_app_todo(in_file); break;
            case JPEG_MARKER_COMMENT:          length = parse_comment(in_file); break;
            default:                           ERROR("Unknown marker"); break;
        }

        if (!state->has_soi) ERROR("file is not a valid jpeg file")

        if (error_length > 0) {
            return;
        }

        fseek(in_file, marker_start + length, SEEK_SET);

        // if (marker.type == JPEG_MARKER_START_OF_SCAN) {
        //     parse_image_data(state);

        //     char out_path_int[256];
        //     snprintf(out_path_int, 255, "scan%08i.bmp", (int)ftell(in_file));
        //     write_bmp(out_path_int, state->out_image, state->out_width, state->out_height);
        // }
        
        continue;
    }
}

#if STANDALONE
int main(int argc, char** argv) {
    // parse arguments
    char* in_path = "../test/StandingThere.jpg";
    char* out_path = "../result/StandingThere.bmp";
    if (argc != 3) {
        printf("Usage: jpeg_dec <input> <output>\n");
        // return 1;
    } else {
        in_path = argv[1];
        out_path = argv[2];
    }
    
    printf("\n%s\n", in_path);

    // open input file
    FILE* in_file = fopen(in_path, "rb");
    if (!in_file) {
        printf("Error: failed to open file \"%.*s\"\n", 512, in_path);
        return 2;
    }

    jpeg_state_t state = {0};
    generate_luts(&state);
    handle_markers(in_file, &state, 1);
    parse_image_data(&state);
    
    if (error_length > 0) {
        return 3;
    }

    write_bmp(out_path, state.out_image, state.out_width, state.out_height);

#if DEBUG
    printf("wrote bmp to %s\n", out_path);
#endif

    cleanup(&state);

    return 0;
}
#endif

#if MIV_LIBRARY
#include "MIV.h"

int64_t registration_procedure(Plugin_Registration_Entry* registration) {
    registration->name_of_filetype = to_string("JPEG Image");
    registration->procedure_prefix = to_string("jpeg_");
    registration->extension = to_string("JPG");
    registration->magic_number = to_string("\xFF\xD8");
    registration->has_settings = 0;
    return 0;
}

Log jpeg_cleanup(Pre_Rendering_Info* pre_info) {
    if (pre_info->user_ptr != NULL) {
        cleanup((jpeg_state_t*)pre_info->user_ptr);
        free(pre_info->user_ptr);
        pre_info->user_ptr = NULL;
    }

    memset(error_buffer, 0, sizeof(error_buffer));
    error_length = 0;

    return (Log){0};
}

void jpeg_init(Pre_Rendering_Info* pre_info) {
    if (pre_info->user_ptr == NULL) {
        pre_info->user_ptr = malloc(sizeof(jpeg_state_t));
    }

    jpeg_state_t* state = (jpeg_state_t*)pre_info->user_ptr;
    memset(state, 0, sizeof(*state));

    fseek(pre_info->fileptr, 0, SEEK_SET);
    generate_luts(state);
}

Log jpeg_pre_render(Pre_Rendering_Info* pre_info) {
    #if DEBUG
    printf("pre_render\n");
    #endif

    jpeg_cleanup(pre_info);
    jpeg_init(pre_info);

    jpeg_state_t* state = (jpeg_state_t*)pre_info->user_ptr;

    handle_markers(pre_info->fileptr, state, 0);

    if (error_length > 0) {
        return (Log){LOG_TYPE_ERROR, {
            .count = error_length, 
            .data = (uint8_t*)&error_buffer
        }};
    }

    pre_info->width = state->start_of_frame.width;
    pre_info->height = state->start_of_frame.height;
    pre_info->bit_depth = state->start_of_frame.bits_per_pixel;
    pre_info->channels = state->start_of_frame.n_components;
    pre_info->metadata_count = 0; // todo
    return (Log){0};
}

Log jpeg_render(Pre_Rendering_Info* pre_info, Rendering_Info* render_info) {
    jpeg_cleanup(pre_info);
    jpeg_init(pre_info);
    
    if (error_length > 0) {
        return (Log){LOG_TYPE_ERROR, {
            .count = error_length, 
            .data = (uint8_t*)&error_buffer
        }};
    }
    
    jpeg_state_t* state = (jpeg_state_t*)pre_info->user_ptr;
    
    handle_markers(pre_info->fileptr, state, 1);
    parse_image_data(state);
    
    if (error_length > 0) {
        return (Log){LOG_TYPE_ERROR, {
            .count = error_length, 
            .data = (uint8_t*)&error_buffer
        }};
    }

    for (int i = 0; i < render_info->buffer_count; ++i) {
        render_info->buffer[i][0] = state->out_image[i].r;
        render_info->buffer[i][1] = state->out_image[i].g;
        render_info->buffer[i][2] = state->out_image[i].b;
        render_info->buffer[i][3] = 255;
    }

    return (Log){0};
}

#endif
