#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

// todo: fix memory leaks

#define DEBUG_VERBOSE 0
#define DEBUG_MEMORY 0
#define PROFILING 0
#define FLOAT float

#define BLOCK_RES 8
FLOAT lut_dct[BLOCK_RES * BLOCK_RES];
FLOAT lut_factor[BLOCK_RES * BLOCK_RES];

#if DEBUG_MEMORY
typedef struct {
    char file[64];
    void* ptr;
    size_t size;
    int line;
} alloc_debug_entry_t;
alloc_debug_entry_t alloc_debug_entries[1024] = {0};

size_t alloc_cursor = 0;

void* mem_alloc(size_t size, const char* file, int line) {
    alloc_debug_entry_t* entry = &alloc_debug_entries[alloc_cursor++];
    entry->ptr = malloc(size);
    entry->size = size;
    entry->line = line;
    strncpy(entry->file, file, sizeof(entry->file));

    #if DEBUG_VERBOSE
    printf("malloc %p (%i bytes, %s:%i)\n", 
        entry->ptr, 
        (int)entry->size,
        entry->file, 
        entry->line 
    );
    #endif

    return entry->ptr;
}

void mem_free(void* ptr) {
    if (!ptr) return;

    for (size_t i = 0; i <= alloc_cursor; ++i) {
        if (alloc_debug_entries[i].ptr == ptr) {
            #if DEBUG_VERBOSE
            printf("free %p (%i bytes, %s:%i)\n", 
                alloc_debug_entries[i].ptr, 
                (int)alloc_debug_entries[i].size,
                alloc_debug_entries[i].file, 
                alloc_debug_entries[i].line 
            );
            #endif
            free(ptr);
            alloc_debug_entries[i].ptr = NULL;
            return;
        }
    }
}

void mem_leak_check(void) {
    for (size_t i = 0; i <= 1024; ++i) {
        if (alloc_debug_entries[i].ptr != NULL) {
            printf("LEAKED %p (%i bytes, %s:%i)\n", 
                alloc_debug_entries[i].ptr, 
                (int)alloc_debug_entries[i].size,
                alloc_debug_entries[i].file, 
                alloc_debug_entries[i].line 
            );
        }
    }
}

#else

void* mem_alloc(size_t size, const char* file, int line) {
    (void)file;
    (void)line;
    return malloc(size);

}

void mem_free(void* ptr) {
    free(ptr);
}

#endif
#define MALLOC(size) mem_alloc(size, __FILE__, __LINE__)
#define FREE(ptr) { mem_free(ptr); ptr = NULL; }

#pragma pack(1)
typedef enum {
    JPEG_MARKER_START_OF_FRAME = 0xC0, // metadata
    JPEG_MARKER_HUFFMAN_TABLE = 0xC4,
    JPEG_MARKER_START_OF_IMAGE = 0xD8, // file magic
    JPEG_MARKER_END_OF_IMAGE = 0xD9, // end of file
    JPEG_MARKER_START_OF_SCAN = 0xDA, // start of image data
    JPEG_MARKER_QUANT_TABLE = 0xDB,
    JPEG_MARKER_JFIF_APP0 = 0xE0, // JFIF header
    JPEG_MARKER_JFIF_APP1 = 0xE1, // EXIF header
    JPEG_MARKER_MAGIC = 0xFF, // each marker is preceded by this value
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

typedef struct {
    uint8_t magic; // 0xFF
    uint8_t type; // see jpeg_marker_type_t
} jpeg_marker_t;

// todo: maybe just do this in the function and get rid of the struct? goes for the others too
typedef struct {
    uint16_t length; // excluding the markers
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
    char identifier[5]; // "Exif" including null terminator
    uint8_t _pad; // ignore, likely 0
    uint16_t byte_order; // see `jpeg_byte_order_t`
    uint16_t tiff_identifier; // should be 42
    int32_t ifd_offset; // relative to start of TIFF header
} jpeg_app1_t;

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
    uint8_t id;
    union {
        uint8_t u8;
        struct {
            uint8_t height : 4;
            uint8_t width : 4;
        };
    } blocks_per_mcu;
    uint8_t quant_table_id;
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
    // todo: reassess if any of these are unused
    size_t table_buf_size;
    uint8_t* symbols;
    uint32_t* codes;
    uint8_t* code_lengths;
    jpeg_huffman_lut_entry_t* left_shifted_code_lut;
} jpeg_huffman_table_decoded_t;

typedef enum {
    COMP_ID_UNDEFINED = 0,
    COMP_ID_Y = 1,
    COMP_ID_CB = 2,
    COMP_ID_CR = 3,
    COMP_ID_I = 4,
    COMP_ID_Q = 5,
} jpeg_component_id_t;

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
    uint16_t has_jfif : 1;
    uint16_t has_jfxx : 1;
    uint16_t has_exif : 1;
    uint16_t has_sof : 1;
    uint16_t has_sos : 1;
    uint16_t has_chn : 1;
    uint16_t has_thumb : 1;
    uint16_t has_quant : 1;
    uint16_t has_huff_tbl : 1;
    uint16_t has_luts : 1;

    jpeg_app0_jfif_t jfif;
    jpeg_app0_jfxx_t jfxx;
    jpeg_app1_t exif;
    jpeg_start_of_frame_t start_of_frame;
    jpeg_start_of_scan_t start_of_scan;
    
    size_t image_data_start;
    size_t image_data_end;

    size_t mcu_width;
    size_t mcu_height;

    int n_jfif_thumbnail_bytes;
    int n_quant_tables;
    int n_huffman_tables_ac;
    int n_huffman_tables_dc;

    size_t scratch_buffer_size;
    void* scratch_buffer;

    rgb8_t* out_image;
    size_t out_width;
    size_t out_height;

    jpeg_channel_t components[256];
    uint8_t* jfif_thumbnail_pixels; 
    uint8_t** quant_tables;
    jpeg_huffman_table_decoded_t* huffman_tables_dc;
    jpeg_huffman_table_decoded_t* huffman_tables_ac;
} jpeg_state_t;

typedef struct {
    uint8_t scan_buffer[65536];
    size_t scan_buffer_cursor;
    size_t end_at_index;
    size_t n_bits_ready;
    size_t curr_byte_bits_left;
    uint64_t bit_peek_buffer;
    uint8_t curr_byte;
} bit_stream_t;

#define ERROR(err) { printf("error: %s:%i: %s\n", __FILE__, __LINE__, err); exit(-2); }
#define TODO() { printf("todo: %s:%i\n", __FILE__, __LINE__); exit(-1); }

typedef enum {
    ENTROPY_DEC_STEP_NONE = 0,
    ENTROPY_DEC_STEP_RIGHT_ELSE_DOWN,
    ENTROPY_DEC_STEP_DOWN_LEFT,
    ENTROPY_DEC_STEP_DOWN_ELSE_RIGHT,
    ENTROPY_DEC_STEP_UP_RIGHT,
} jpeg_entropy_decoding_step_t;

void entropy_decode(FLOAT* block, size_t resolution, jpeg_state_t* state) {
    jpeg_entropy_decoding_step_t curr_step = ENTROPY_DEC_STEP_RIGHT_ELSE_DOWN;

    const size_t desired_scratch_buffer_size = resolution * resolution * sizeof(FLOAT);
    if (state->scratch_buffer_size < desired_scratch_buffer_size) {
        state->scratch_buffer = realloc(state->scratch_buffer, desired_scratch_buffer_size);
        state->scratch_buffer_size = desired_scratch_buffer_size;
    }
    const int n_values = resolution * resolution;
    const size_t edge = resolution - 1;
    size_t x = 0;
    size_t y = 0;

    for (int i = 0; i < n_values; ++i) {
        // write
        const size_t dst_i = (y * resolution) + x;
        ((FLOAT*)state->scratch_buffer)[dst_i] = block[i];

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
        default: ERROR("");
        }
    }

    memcpy(block, state->scratch_buffer, resolution * resolution * sizeof(FLOAT));
}

void read_s32(FILE* file, int32_t* dest, jpeg_byte_order_t byte_order) {
    if (byte_order == JPEG_BYTE_ORDER_LE) {
        if (!fread(dest, sizeof(*dest), 1, file)) ERROR("failed to read u32");
    }
    else if (byte_order == JPEG_BYTE_ORDER_BE) {
        uint8_t be_bytes[4];
        union {
            uint8_t bytes[4];
            int32_t u32;
        } le;
        if (!fread(be_bytes, 1, 4, file)) ERROR("failed to read u32");
        le.bytes[0] = be_bytes[3];
        le.bytes[1] = be_bytes[2];
        le.bytes[2] = be_bytes[1];
        le.bytes[3] = be_bytes[0];
        *dest = le.u32;
        
    }
    else ERROR("invalid byte order");
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
    if (fread(dest, 1, size, file) != size) ERROR("failed to read bytes");
}

void parse_start_of_image(void) {
    #if DEBUG_VERBOSE
        printf("START OF IMAGE\n");
    #endif
}

void parse_end_of_image(void) {
    #if DEBUG_VERBOSE
        printf("END OF IMAGE\n");
    #endif
}

void parse_jfif_app0(FILE* file, jpeg_state_t* state) {
    jpeg_app0_t header;
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

        const size_t thumbnail_size = header.length - sizeof(jpeg_app0_t) - sizeof(jpeg_app0_jfif_t);
        if (thumbnail_size != 0) {
            state->jfif_thumbnail_pixels = MALLOC(thumbnail_size);
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
        fseek(file, header.length - sizeof(jpeg_app0_t) - sizeof(jpeg_app0_jfxx_t), SEEK_CUR);

        state->has_jfxx = 1;

        #if DEBUG_VERBOSE
            printf("\t\t.thumbnail_format   = %i\n", jfxx->thumbnail_format);
        #endif
    }
    else {
        ERROR("Invalid APP0 identifier");
    }
}

void parse_jfif_app1(FILE* file, jpeg_state_t* state) {
    read_u16(file, &state->exif.length, JPEG_BYTE_ORDER_BE);
    read_bytes(file, &state->exif.identifier, sizeof(state->exif.identifier));
    read_u8(file, &state->exif._pad);
    read_u16(file, &state->exif.byte_order, JPEG_BYTE_ORDER_BE);
    read_u16(file, &state->exif.tiff_identifier, JPEG_BYTE_ORDER_BE);
    read_s32(file, &state->exif.ifd_offset, JPEG_BYTE_ORDER_BE);
    // todo: exif parsing through vivver's code
    fseek(file, state->exif.length - sizeof(state->exif), SEEK_CUR);

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

void parse_start_of_frame(FILE* file, jpeg_state_t* state) {
    jpeg_start_of_frame_t* header = &state->start_of_frame;
    read_u16(file, &header->length, JPEG_BYTE_ORDER_BE);
    read_u8(file, &header->bits_per_pixel);
    read_u16(file, &header->height, JPEG_BYTE_ORDER_BE);
    read_u16(file, &header->width, JPEG_BYTE_ORDER_BE);
    read_u8(file, &header->n_components);

    #if DEBUG_VERBOSE
        printf("START OF FRAME:\n");
        printf("\t.length         = %i\n", header->length);
        printf("\t.bits_per_pixel = %i\n", header->bits_per_pixel);
        printf("\t.height         = %i\n", header->height);
        printf("\t.width          = %i\n", header->width);
        printf("\t.n_components   = %i\n", header->n_components);
    #endif

    uint8_t smp_factor_max_w = 0;
    uint8_t smp_factor_max_h = 0;

    for (size_t i = 0; i < (size_t)header->n_components; ++i) {
        jpeg_channel_t component;
        read_u8(file, &component.id);
        read_u8(file, &component.blocks_per_mcu.u8);
        read_u8(file, &component.quant_table_id);

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

        state->components[(size_t)component.id] = component;
    }

    state->mcu_width = smp_factor_max_w * BLOCK_RES;
    state->mcu_height = smp_factor_max_h * BLOCK_RES;
    state->has_chn = 1;
    state->has_sof = 1;

    #if DEBUG_VERBOSE
        printf("\t\tMCU size: %ix%i\n", (int)state->mcu_width, (int)state->mcu_height);
    #endif
}

void parse_quant_table(FILE* file, jpeg_state_t* state) {
    jpeg_quant_table_t header;
    read_u16(file, &header.length, JPEG_BYTE_ORDER_BE);
    read_u8(file, &header.table_id);

    const size_t table_buf_size = header.length - sizeof(header);

    const size_t table_id = state->n_quant_tables++;
    state->quant_tables = realloc(state->quant_tables, sizeof(*state->quant_tables) * (state->n_quant_tables));
    state->quant_tables[table_id] = MALLOC(table_buf_size);
    read_bytes(file, state->quant_tables[table_id], table_buf_size);
    state->has_quant = 1;

    #if DEBUG_VERBOSE
        printf("QUANT TABLE:\n");
        printf("\t.length          = %i\n", header.length);
        printf("\t.table_id        = %i\n", header.table_id);
        printf("\ttable values: (%i bytes)\n\t\t", (int)table_buf_size);
        for (size_t i = 0; i < table_buf_size; ++i) {
            if (i && i % BLOCK_RES == 0) printf("\n\t\t");
            printf("%3i, ", state->quant_tables[table_id][i]);
        }
        printf("\n");
    #endif
}

void parse_huffman_table(FILE* file, jpeg_state_t* state) {
    jpeg_huffman_table_header_t header;
    read_u16(file, &header.length, JPEG_BYTE_ORDER_BE);
    read_u8(file, &header.class_id);
    read_bytes(file, &header.bit_lengths[0], sizeof(header.bit_lengths));

    jpeg_huffman_table_decoded_t huff_tbl;
    huff_tbl.table_buf_size = header.length - sizeof(header);
    huff_tbl.symbols = MALLOC(huff_tbl.table_buf_size);
    huff_tbl.codes = MALLOC(huff_tbl.table_buf_size * sizeof(uint32_t));
    huff_tbl.code_lengths = MALLOC(huff_tbl.table_buf_size);
    
    memset(huff_tbl.symbols, 0, huff_tbl.table_buf_size);
    memset(huff_tbl.codes, 0, huff_tbl.table_buf_size * sizeof(uint32_t));
    memset(huff_tbl.code_lengths, 0, huff_tbl.table_buf_size);
    read_bytes(file, huff_tbl.symbols, huff_tbl.table_buf_size);

    const size_t lut_size = (1 << 16) * sizeof(jpeg_huffman_lut_entry_t);
    huff_tbl.left_shifted_code_lut = MALLOC(lut_size);
    memset(huff_tbl.left_shifted_code_lut, 0, lut_size);

    // decode table
    size_t length = 1;
    size_t code = 0;
    size_t symbol_id = 0;

    for (size_t i_bit_lengths = 0; i_bit_lengths < sizeof(header.bit_lengths); ++i_bit_lengths) {
        for (size_t i = 0; i < header.bit_lengths[i_bit_lengths]; ++i) {
            huff_tbl.code_lengths[symbol_id] = length;
            const size_t code_start = (code++) << (15 - i_bit_lengths);
            const size_t code_end = ((code) << (15 - i_bit_lengths));
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
        ++state->n_huffman_tables_dc;
        state->huffman_tables_dc = realloc(state->huffman_tables_dc, state->n_huffman_tables_dc * sizeof(jpeg_huffman_table_decoded_t));
        state->huffman_tables_dc[state->n_huffman_tables_dc - 1] = huff_tbl;
    }
    else if (header.class == 1) { // AC
        ++state->n_huffman_tables_ac;
        state->huffman_tables_ac = realloc(state->huffman_tables_ac, state->n_huffman_tables_ac * sizeof(jpeg_huffman_table_decoded_t));
        state->huffman_tables_ac[state->n_huffman_tables_ac - 1] = huff_tbl;
    }
    else ERROR("Invalid huffman table class");
    state->has_huff_tbl = 1;

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

void parse_start_of_scan(FILE* file, jpeg_state_t* state) {
    jpeg_start_of_scan_t header;
    read_u16(file, &header.length, JPEG_BYTE_ORDER_BE);
    read_u8(file, &header.n_components);

    const size_t n_bytes_components = sizeof(jpeg_component_t) * (size_t)header.n_components;
    jpeg_component_t* components = MALLOC(n_bytes_components);
    read_bytes(file, components, n_bytes_components);

    read_u8(file, &header.spectral_selection_min);
    read_u8(file, &header.spectral_selection_max);
    read_u8(file, &header.successive_approximation);

    header.components = components;

    state->start_of_scan = header;
    state->image_data_start = ftell(file);
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
}

void bit_stream_init(bit_stream_t* stream) {
    memset(stream->scan_buffer, 0, sizeof(stream->scan_buffer));
    stream->scan_buffer_cursor = 0;
    stream->end_at_index = sizeof(stream->scan_buffer); // we never get to this index
    stream->n_bits_ready = 0;
    stream->curr_byte_bits_left = 0;
    stream->curr_byte = 0;
    stream->bit_peek_buffer = 0;
}

void bit_stream_get_byte(FILE* file, bit_stream_t* stream) {
    // get byte, if we have any
    int prev_was_marker = 0;
    while (1) {
        if (stream->scan_buffer_cursor == 0) {
            size_t read_bytes = fread(&stream->scan_buffer[stream->scan_buffer_cursor], 1, sizeof(stream->scan_buffer), file);

            if (read_bytes != sizeof(stream->scan_buffer)) {
                stream->end_at_index = read_bytes;
            }
        }

        if (stream->scan_buffer_cursor >= stream->end_at_index) return;
        stream->curr_byte = stream->scan_buffer[stream->scan_buffer_cursor];
        stream->scan_buffer_cursor = (stream->scan_buffer_cursor + 1) % sizeof(stream->scan_buffer);
        if (prev_was_marker) {
            if (stream->curr_byte == JPEG_MARKER_END_OF_IMAGE) {
                return;
            }
            if (stream->curr_byte != 0x00) {
                ERROR("Unexpected marker");
            }
            stream->curr_byte = 0xFF;
        }
        else if (stream->curr_byte == 0xFF) {
            prev_was_marker = 1;
            continue;
        }
        stream->curr_byte_bits_left = 8;
        break;
    }
}

#if 0
void bit_stream_refill(FILE* file, bit_stream_t* stream) {
    while (stream->n_bits_ready < 64) {
        if (stream->curr_byte_bits_left == 0) {
            bit_stream_get_byte(file, stream);
        }

        const uint64_t bit = stream->curr_byte >> (8 - stream->curr_byte_bits_left);
        stream->curr_byte <<= stream->curr_byte_bits_left;
        
        stream->bit_peek_buffer <<= stream->curr_byte_bits_left;
        stream->bit_peek_buffer |= bit;
        
        stream->n_bits_ready += stream->curr_byte_bits_left;
        stream->curr_byte_bits_left = 0;
    }
}
#elif 1 // chatgpt
void bit_stream_refill(FILE* file, bit_stream_t* stream) {
    while (stream->n_bits_ready < 64) {
        if (stream->curr_byte_bits_left == 0) {
            bit_stream_get_byte(file, stream);
            if (stream->curr_byte_bits_left == 0) break; // EOF guard if needed
        }

        // take as many bits as possible from curr_byte in one go
        uint32_t need = 64 - stream->n_bits_ready;
        uint32_t take = stream->curr_byte_bits_left < need ? stream->curr_byte_bits_left : need;
        if (stream->curr_byte_bits_left < need) {
            take = stream->curr_byte_bits_left;
        } else {
            take = need;
        }

        // extract top 'take' bits from curr_byte
        uint8_t shift = 8 - take;
        uint64_t bits = (uint64_t)((stream->curr_byte >> shift) & ((1u << take) - 1u));

        stream->bit_peek_buffer = (stream->bit_peek_buffer << take) | bits;
        stream->n_bits_ready += take;

        // consume those bits
        stream->curr_byte <<= take;
        stream->curr_byte_bits_left -= take;
    }
}
#else
void bit_stream_refill(FILE* file, bit_stream_t* stream) {
    while (stream->n_bits_ready < 64) {
        if (stream->curr_byte_bits_left == 0) {
            bit_stream_get_byte(file, stream);
        }

        const uint64_t bit = stream->curr_byte >> 7;
        stream->curr_byte <<= 1;
        --stream->curr_byte_bits_left;

        stream->bit_peek_buffer <<= 1;
        stream->bit_peek_buffer |= bit;
        ++stream->n_bits_ready;
    }
}
#endif

void bit_stream_refill_old(FILE* file, bit_stream_t* stream) {
    const size_t scan_buffer_half = sizeof(stream->scan_buffer) / 2;

    if (stream->scan_buffer_cursor == stream->end_at_index) {
        printf("no more image data\n");
        return;
    }

    // peek 16 bits
    while (stream->n_bits_ready < 16) {
        // make sure curr_byte has a bit in it
        if (stream->curr_byte_bits_left == 0) {
            // refill scan buffer
            if (stream->scan_buffer_cursor == 0 || stream->scan_buffer_cursor == scan_buffer_half) {
                size_t read_bytes = fread(&stream->scan_buffer[stream->scan_buffer_cursor], 1, scan_buffer_half, file);

                if (read_bytes != scan_buffer_half) {
                    stream->end_at_index = read_bytes + stream->scan_buffer_cursor;
                }
            }

            stream->curr_byte = stream->scan_buffer[stream->scan_buffer_cursor];
            stream->scan_buffer_cursor = (stream->scan_buffer_cursor + 1) % sizeof(stream->scan_buffer);
            if (stream->curr_byte == JPEG_MARKER_MAGIC) {
                stream->curr_byte = stream->scan_buffer[stream->scan_buffer_cursor];
                stream->scan_buffer_cursor = (stream->scan_buffer_cursor + 1) % sizeof(stream->scan_buffer);
                switch (stream->curr_byte) {
                    case 0x00: stream->curr_byte = 0xFF; break; // byte 0xFF is encoded as 0xFF, 0x00
                    case JPEG_MARKER_END_OF_IMAGE: stream->end_at_index = stream->scan_buffer_cursor - 1; break;
                    default: printf("%02X%02X ftell %x\n", JPEG_MARKER_MAGIC, stream->curr_byte, ftell(file)); ERROR("Unexpected marker in image scan"); break;
                }
            }
            stream->curr_byte_bits_left = 8;
            #if DEBUG_VERBOSE
            printf("bit_stream new byte: %02X\n", stream->curr_byte);
            #endif
        }

        const uint16_t bit = stream->curr_byte >> 7;
        stream->curr_byte <<= 1;
        --stream->curr_byte_bits_left;

        stream->bit_peek_buffer <<= 1;
        stream->bit_peek_buffer |= bit;
        ++stream->n_bits_ready;
    }
}

void bit_stream_advance(FILE* file, bit_stream_t* stream, size_t n_bits) {
    stream->n_bits_ready -= n_bits;
    bit_stream_refill(file, stream);
}

int32_t bit_stream_read_value(FILE* file, bit_stream_t* stream, size_t n_bits) {
    if (n_bits > 16) ERROR("reading too many (>16) bits from bitstream");
    if (n_bits == 0) return 0;

    bit_stream_refill(file, stream);
    const size_t bit_index = 63;
    int32_t value = 0;
    int high_bit = 1;
    for (size_t i = 0; i < n_bits; ++i) {
        int bit = (stream->bit_peek_buffer >> (bit_index - i)) & 1;
        value <<= 1;
        value |= bit;
        if (i == 0) high_bit = bit;
    }
    if (high_bit == 0) {
        value -= (1 << n_bits) - 1;
    }
    bit_stream_advance(file, stream, n_bits);
    return value;
}

void dequantize(FLOAT* block, int resolution, uint8_t* quant) {
    for (size_t i = 0; i < (resolution * resolution); ++i) {
        block[i] *= (FLOAT)quant[i];
    }
}

void generate_luts(size_t resolution, jpeg_state_t* state) {
    const FLOAT pi = 3.141592653589793;
    const FLOAT one_over_sqrt_one = 1.0 / sqrt(2.0);

    for (size_t u = 0; u < resolution; ++u) {
        for (size_t x = 0; x < resolution; ++x) {
            FLOAT dct_value = cos((((2.0 * x) + 1.0) * u * pi) / (2.0 * (FLOAT)resolution));
            lut_dct[(resolution * u) + x] = dct_value;

            FLOAT factor = 1.0;
            if (u == 0 && x == 0) factor = 0.5;
            else if (u == 0 || x == 0) factor = one_over_sqrt_one;
            lut_factor[(resolution * u) + x] = factor;
        }
    }
    state->has_luts = 1;
}

void idct(FLOAT* block, size_t resolution, jpeg_state_t* state) {
    if (!state->has_luts) generate_luts(resolution, state);

    size_t block_size = resolution * resolution * sizeof(FLOAT);
    FLOAT* decoded_block = MALLOC(block_size);

    for (size_t y = 0; y < resolution; ++y) {
        for (size_t x = 0; x < resolution; ++x) {
            FLOAT sum = 0.0;
            for (size_t v = 0; v < resolution; ++v) {
                const FLOAT dct_value2 = lut_dct[(resolution * v) + y];
                for (size_t u = 0; u < resolution; ++u) {
                    const FLOAT dct_value1 = lut_dct[(resolution * u) + x];
                    const FLOAT factor = lut_factor[(resolution * v) + u];
                    const FLOAT table_value = block[(resolution * v) + u];
                    sum += table_value * factor * dct_value1 * dct_value2;
                }
            }

            decoded_block[(resolution * y) + x] = sum / 4.0;
        }
    }

    memcpy(block, decoded_block, block_size);
    FREE(decoded_block);
}

void debug_block(const FLOAT* block) {
#if DEBUG_VERBOSE
    printf("BLOCK VALUES:\n");
    for (size_t i = 0; i < 64; ++i) {
        if(i % BLOCK_RES == 0) printf("\n");
        printf("%4.0f, ", block[i]);
    }
    printf("\n");
#else
    (void)block;
#endif
}

void add(FLOAT* arr, FLOAT val, size_t count) { 
    for (size_t i = 0; i < count; ++i) {
        arr[i] += val;
    } 
}

void decode_block(FILE* file, bit_stream_t* stream, jpeg_state_t* state, jpeg_component_t component_huff, uint8_t* quant, FLOAT* block, size_t resolution, FLOAT* dc_prev) {
    // decode huffman
    jpeg_huffman_table_decoded_t huff_tbl_dc = state->huffman_tables_dc[(size_t)component_huff.tables.dc];
    jpeg_huffman_table_decoded_t huff_tbl_ac = state->huffman_tables_ac[(size_t)component_huff.tables.ac];

    // DC
    jpeg_huffman_lut_entry_t lut_entry = {0};
    bit_stream_refill(file, stream);
    lut_entry = huff_tbl_dc.left_shifted_code_lut[(size_t)(stream->bit_peek_buffer >> 48)];
    bit_stream_advance(file, stream, (size_t)lut_entry.length);

    const size_t n_bits_to_read = (size_t)lut_entry.symbol;
    const int32_t value = bit_stream_read_value(file, stream, n_bits_to_read);

    const size_t res2 = resolution * resolution;

    block[0] = (FLOAT)value + *dc_prev;
    *dc_prev = block[0];

    // AC
    size_t block_cursor = 1;
    while(block_cursor < res2) {
        lut_entry = huff_tbl_ac.left_shifted_code_lut[(size_t)(stream->bit_peek_buffer >> 48)];
        bit_stream_advance(file, stream, (size_t)lut_entry.length);

        if (lut_entry.symbol == 0x00) { // end of block marker
            #if DEBUG_VERBOSE
            printf("end of block\n");
            #endif
            while (block_cursor < res2) block[block_cursor++] = 0;
            break; // done with this block
        }

        if (lut_entry.symbol == 0xF0) {
            #if DEBUG_VERBOSE
            printf("16x 0\n");
            #endif
            for (size_t i = 0; (i < 16) && (block_cursor < res2); ++i) {
                block[block_cursor++] = 0;
            }
            continue;
        }

        const size_t run_length = (size_t)(lut_entry.symbol >> 4);
        const size_t size = (size_t)(lut_entry.symbol & 0x0F);

        for (size_t i = 0; (i < run_length) && (block_cursor < res2); ++i) {
            block[block_cursor++] = 0;
        }

        const int32_t value = bit_stream_read_value(file, stream, size);

        block[block_cursor++] = (FLOAT)value;
        
        #if DEBUG_VERBOSE
        printf("%ix 0\n", run_length);
        printf("%i\n", value);
        #endif
    }

    dequantize(block, resolution, quant);
    entropy_decode(block, resolution, state);
    debug_block(block);
    idct(block, resolution, state);
    add(block, 128.0, res2);
}

rgb8_t ycbcr_to_rgb(FLOAT y, FLOAT cb, FLOAT cr) {
    // todo: verify
    FLOAT r = y + 1.402    * (cr - 128.0);
    FLOAT g = y - 0.344136 * (cb - 128.0) - 0.714136 * (cr - 128.0);
    FLOAT b = y + 1.772    * (cb - 128.0);                     
    
    r = fmin(255.0, fmax(0.0, r));
    g = fmin(255.0, fmax(0.0, g));
    b = fmin(255.0, fmax(0.0, b));
    
    return (rgb8_t){
        .r = (uint8_t)r,
        .g = (uint8_t)g,
        .b = (uint8_t)b,
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

    FILE* f = fopen("img.bmp","wb");
    fwrite(bmpfileheader,1,14,f);
    fwrite(bmpinfoheader,1,40,f);
    for(int i=0; i<h; i++) {
        fwrite(((uint8_t*)img)+(w*(h-i-1)*3),3,w,f);
        fwrite(bmppad,1,(4-(w*3)%4)%4,f);
    }
}

void parse_image_data(FILE* file, jpeg_state_t* state) {
    if (!state->has_chn) ERROR("no channel info found!");
    if (!state->has_jfif) ERROR("no JFIF header found!");
    if (!state->has_quant) ERROR("no quantization tables found!");
    if (!state->has_huff_tbl) ERROR("no huffman tables found!");
    if (!state->has_sof) ERROR("no start of frame segment found!");
    if (!state->has_sos) ERROR("no start of scan segment found!");

    fseek(file, state->image_data_start, SEEK_SET);
    bit_stream_t stream;
    bit_stream_init(&stream);

    const size_t n_mcu_y = (state->start_of_frame.height + state->mcu_height - 1) / state->mcu_height;
    const size_t n_mcu_x = (state->start_of_frame.width + state->mcu_width - 1) / state->mcu_width;
    const size_t n_mcu = n_mcu_x * n_mcu_y;

    const size_t res = BLOCK_RES;
    const size_t mcu_size = state->mcu_width * state->mcu_height;

    const size_t out_w = state->start_of_frame.width;
    const size_t out_h = state->start_of_frame.height;
    
    const size_t raw_w = n_mcu_x * state->mcu_width;
    const size_t raw_h = n_mcu_y * state->mcu_height;
    const size_t raw_size = raw_w * raw_h;

    // Allocate raw planes
    FLOAT* image_raw[256] = {NULL};
    for (size_t comp_id = 0; comp_id < state->start_of_scan.n_components; ++comp_id) {
        jpeg_component_t component_huff = state->start_of_scan.components[comp_id];
        jpeg_channel_t component_info = state->components[(size_t)component_huff.id];
        image_raw[comp_id] = MALLOC(raw_size * sizeof(FLOAT));
        memset(image_raw[comp_id], 0, raw_size * sizeof(FLOAT));
    }
    FLOAT* block_scratch = MALLOC(res * res * sizeof(FLOAT));
    
    // todo: maybe the blocks themselves can be contiguous for cache reasons (one 8x8 block fits in a single 64 byte cache line)
    FLOAT dc_prev[256] = {0.0};
    for (size_t mcu_y = 0; mcu_y < n_mcu_y; ++mcu_y) {
        for (size_t mcu_x = 0; mcu_x < n_mcu_x; ++mcu_x) {
            #if DEBUG_VERBOSE
            printf("mcu (%i, %i)\n", mcu_x, mcu_y);
            #endif

            // Decode block -> mcu_scratch
            for (size_t comp_id = 0; comp_id < state->start_of_scan.n_components; ++comp_id) {
                jpeg_component_t component_huff = state->start_of_scan.components[comp_id];
                jpeg_channel_t component_info = state->components[(size_t)component_huff.id];
                uint8_t* quant = state->quant_tables[(size_t)component_info.quant_table_id];

                const size_t width = (size_t)component_info.blocks_per_mcu.width;
                const size_t height = (size_t)component_info.blocks_per_mcu.height;

                #if DEBUG_VERBOSE
                printf("\tBLOCK MCU: %02X\n", component_info.blocks_per_mcu.u8);
                #endif
                
                for (size_t block_y = 0; block_y < height; ++block_y) {
                    for (size_t block_x = 0; block_x < width; ++block_x) {
                        #if DEBUG_VERBOSE
                        #endif
                        decode_block(file, &stream, state, component_huff, quant, block_scratch, res, &dc_prev[comp_id]);

                        // Place in raw buffer
                        const size_t block_offset_x = (mcu_x * width * res) + (block_x * res);
                        const size_t block_offset_y = (mcu_y * height * res) + (block_y * res);
                        #if DEBUG_VERBOSE
                        printf("\t\tblock (%i, %i) at offset (%i, %i)\n", block_x, block_y, block_offset_x, block_offset_y);
                        #endif

                        for (size_t pixel_y = 0; pixel_y < res; ++pixel_y) {
                            for (size_t pixel_x = 0; pixel_x < res; ++pixel_x) {
                                const size_t dst_x = block_offset_x + pixel_x;
                                const size_t dst_y = block_offset_y + pixel_y;
                                const size_t dst_i = (dst_y * raw_w) + dst_x;

                                const size_t block_i = (pixel_y * res) + pixel_x;

                                image_raw[comp_id][dst_i] = block_scratch[block_i];
                            }
                        }
                    }
                }
            }
        }
    }

    // upscale downsampled planes
    for (size_t comp_id = 0; comp_id < state->start_of_scan.n_components; ++comp_id) {
        jpeg_component_t component_huff = state->start_of_scan.components[comp_id];
        jpeg_channel_t component_info = state->components[(size_t)component_huff.id];

        const FLOAT src_pitch_x = (FLOAT)(res * component_info.blocks_per_mcu.width) / (FLOAT)state->mcu_width;
        const FLOAT src_pitch_y = (FLOAT)(res * component_info.blocks_per_mcu.height) / (FLOAT)state->mcu_height;

        if (src_pitch_x == 1.0 && src_pitch_y == 1.0) continue;

        // reverse order because the non-upscaled data is in the top left, and then then i can write the  
        // upscaled image to the same buffer without overwriting the source data
        // todo: interpolation - maybe just for the odd pixels mix the closest even neighbors
        for (int dst_y = out_h - 1; dst_y >= 0; --dst_y) {
            const FLOAT src_y = (FLOAT)dst_y * src_pitch_y;
            for (int dst_x = out_w - 1; dst_x >= 0; --dst_x) {
                const FLOAT src_x = (FLOAT)dst_x * src_pitch_x;
                const size_t src_index = (((size_t)floor(src_y)) * raw_w) + (size_t)floor(src_x);
                const size_t dst_index = (size_t)((dst_y * raw_w) + dst_x);
                image_raw[comp_id][dst_index] = image_raw[comp_id][src_index];
            }
        }
    }

    // components -> RGB
    jpeg_component_t* comps = state->start_of_scan.components;
    int index_y = -1;
    int index_cb = -1;
    int index_cr = -1;
    int index_i = -1;
    int index_q = -1;

    for (size_t comp_id = 0; comp_id < state->start_of_scan.n_components; ++comp_id) {
        switch (state->start_of_scan.components[comp_id].id) {
            case COMP_ID_Y: index_y = comp_id; continue;
            case COMP_ID_CB: index_cb = comp_id; continue;
            case COMP_ID_CR: index_cr = comp_id; continue;
            case COMP_ID_I: index_i = comp_id; continue;
            case COMP_ID_Q: index_q = comp_id; continue;
        }
    }

    rgb8_t* out_image = MALLOC(out_w * out_h * sizeof(rgb8_t));

    // Grayscale with a single Y component
    if (state->start_of_scan.n_components == 1 && index_y >= 0) {
        jpeg_component_t component_huff = state->start_of_scan.components[0];
        jpeg_channel_t component_info = state->components[(size_t)component_huff.id];

        const size_t raw_width = res * n_mcu_x * component_info.blocks_per_mcu.width;
        const size_t raw_height = res * n_mcu_y * component_info.blocks_per_mcu.height;

        for (size_t y = 0; y < out_h; ++y) {
            for (size_t x = 0; x < out_w; ++x) {
                // convert to u8
                const size_t raw_index = (y * raw_width) + x;
                const FLOAT raw_value = image_raw[0][raw_index];
                uint8_t value = 0;
                if (raw_value > 255.0) value = 255;
                else if (raw_value > 0.0) value = (uint8_t)round(raw_value);

                // store to output
                const size_t out_index = (y * out_w) + x;
                out_image[out_index].r = value;
                out_image[out_index].g = value;
                out_image[out_index].b = value;
            }
        }
    }

    // YCbCr
    else if (state->start_of_scan.n_components == 3 && index_y >= 0 && index_cb >= 0 && index_cr >= 0) {
        for (size_t y = 0; y < out_h; ++y) {
            for (size_t x = 0; x < out_w; ++x) {
                const size_t pixel_index = (y * raw_w) + x;
                const FLOAT y_ = image_raw[index_y][pixel_index];
                const FLOAT cb = image_raw[index_cb][pixel_index];
                const FLOAT cr = image_raw[index_cr][pixel_index];
                const rgb8_t pixel = ycbcr_to_rgb(y_, cb, cr);
                // rgb8_t pixel = (rgb8_t) {
                //     .r = (uint8_t)fmin(255.0, fmax(0.0, y_)),
                //     .g = (uint8_t)fmin(255.0, fmax(0.0, cb)),
                //     .b = (uint8_t)fmin(255.0, fmax(0.0, cr)),
                // };
                out_image[(y * out_w) + x] = pixel;
            }
        }
    }

    else {
        TODO()
    }

    state->out_image = out_image;
    state->out_width = out_w;
    state->out_height = out_h;

    for (size_t comp_id = 0; comp_id < state->start_of_scan.n_components; ++comp_id) {
        FREE(image_raw[comp_id]);
    }
    // FREE(block_scratch);
}

void cleanup(jpeg_state_t* state) {
    for (size_t i = 0; i < (size_t)state->n_quant_tables; ++i) {
        FREE(state->quant_tables[i]);
    }
    FREE(state->quant_tables);
    for (size_t i = 0; i < (size_t)state->n_huffman_tables_ac; ++i) {
        FREE(state->huffman_tables_ac[i].symbols);
        FREE(state->huffman_tables_ac[i].codes);
        FREE(state->huffman_tables_ac[i].code_lengths);
        FREE(state->huffman_tables_ac[i].left_shifted_code_lut);
    }
    FREE(state->huffman_tables_ac);
    for (size_t i = 0; i < (size_t)state->n_huffman_tables_dc; ++i) {
        FREE(state->huffman_tables_dc[i].symbols);
        FREE(state->huffman_tables_dc[i].codes);
        FREE(state->huffman_tables_dc[i].code_lengths);
        FREE(state->huffman_tables_dc[i].left_shifted_code_lut);
    }
    FREE(state->huffman_tables_dc);
    FREE(state->start_of_scan.components);
    FREE(state->jfif_thumbnail_pixels);
    FREE(state->scratch_buffer);
    memset(state, 0, sizeof(jpeg_state_t));
}

int main(int argc, char** argv) {
    // parse arguments
    if (argc != 3) {
        printf("Usage: jpeg_dec <input> <output>\n");
        // return 1;
    }
    
    const char* in_path = "D:/Projects/Offline/C/jpeg_decoder/test assets/test12.jpg";
    // const char* in_path = argv[1];
    // const char* out_path = argv[2];

    // open input file
    FILE* in_file = fopen(in_path, "rb");
    if (!in_file) {
        printf("Error: failed to open file \"%.*s\"", 512, in_path);
        return 2;
    }

    jpeg_state_t state = {0};

    #if PROFILING
    #define LOOP_COUNT (256)
    for (size_t _loops = 1; _loops < (LOOP_COUNT + 1); ++_loops) {
    fseek(in_file, 0, SEEK_SET);
    printf("%6i / %i\r", _loops, LOOP_COUNT);
    #endif
    // handle each section
    jpeg_marker_t marker;
    while (fread(&marker, sizeof(jpeg_marker_t), 1, in_file)) {
        if (marker.magic != JPEG_MARKER_MAGIC) break;

        switch (marker.type) {
        case JPEG_MARKER_START_OF_FRAME: parse_start_of_frame(in_file, &state); break;
        case JPEG_MARKER_HUFFMAN_TABLE:  parse_huffman_table(in_file, &state); break;
        case JPEG_MARKER_START_OF_IMAGE: parse_start_of_image(); break;
        case JPEG_MARKER_END_OF_IMAGE:   parse_end_of_image(); break;
        case JPEG_MARKER_START_OF_SCAN:  parse_start_of_scan(in_file, &state); break;
        case JPEG_MARKER_QUANT_TABLE:    parse_quant_table(in_file, &state); break;
        case JPEG_MARKER_JFIF_APP0:      parse_jfif_app0(in_file, &state); break;
        case JPEG_MARKER_JFIF_APP1:      parse_jfif_app1(in_file, &state); break;
        default:                         ERROR("Unknown marker"); break;
        }
        
        continue;
    }

    parse_image_data(in_file, &state);

    #if PROFILING
    cleanup(&state);
    }
    #else
    write_bmp("test.bmp", state.out_image, state.out_width, state.out_height);
    printf("wrote bmp\n");
    cleanup(&state);
    #endif


    #if DEBUG_MEMORY
    mem_leak_check();
    #endif

    return 0;
}
