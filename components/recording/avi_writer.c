#include "avi_writer.h"

#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"

#include "app_config.h"

static const char *TAG = "avi_writer";

static bool parse_indexed_file_name(const char *name, uint32_t *sequence)
{
    if (!name || !sequence || strncmp(name, "record_", 7) != 0) {
        return false;
    }
    const char *digits = name + 7;
    if (!isdigit((unsigned char)*digits)) {
        return false;
    }
    char *end = NULL;
    unsigned long value = strtoul(digits, &end, 10);
    if (end == digits || strcmp(end, ".avi") != 0 || value > UINT32_MAX) {
        return false;
    }
    *sequence = (uint32_t)value;
    return true;
}

static uint32_t next_video_sequence(void)
{
    uint32_t next = 1;
    DIR *directory = opendir(APP_SD_MOUNT_POINT);
    if (directory) {
        struct dirent *entry;
        while ((entry = readdir(directory)) != NULL) {
            uint32_t sequence = 0;
            if (parse_indexed_file_name(entry->d_name, &sequence) && sequence < UINT32_MAX &&
                sequence + 1 > next) {
                next = sequence + 1;
            }
        }
        closedir(directory);
    }
    return next;
}

typedef struct {
    uint32_t chunk_id;
    uint32_t flags;
    uint32_t offset;
    uint32_t size;
} avi_index_entry_t;

static bool write_u16(FILE *file, uint16_t value)
{
    return fwrite(&value, sizeof(value), 1, file) == 1;
}

static bool write_u32(FILE *file, uint32_t value)
{
    return fwrite(&value, sizeof(value), 1, file) == 1;
}

static bool write_fourcc(FILE *file, const char *value)
{
    return fwrite(value, 1, 4, file) == 4;
}

static bool read_u32(FILE *file, uint32_t *value)
{
    return value && fread(value, sizeof(*value), 1, file) == 1;
}

static bool read_fourcc(FILE *file, char value[4])
{
    return value && fread(value, 1, 4, file) == 4;
}

static bool write_header(avi_writer_t *writer, const camera_fb_t *frame)
{
    FILE *video = writer->video;
    writer->width = frame->width;
    writer->height = frame->height;

    if (!write_fourcc(video, "RIFF")) {
        return false;
    }
    writer->riff_size_offset = ftell(video);
    if (!write_u32(video, 0) || !write_fourcc(video, "AVI ") || !write_fourcc(video, "LIST") ||
        !write_u32(video, 192) || !write_fourcc(video, "hdrl") || !write_fourcc(video, "avih") ||
        !write_u32(video, 56)) {
        return false;
    }
    writer->avih_offset = ftell(video);
    for (int i = 0; i < 14; ++i) {
        if (!write_u32(video, 0)) {
            return false;
        }
    }
    if (!write_fourcc(video, "LIST") || !write_u32(video, 116) || !write_fourcc(video, "strl") ||
        !write_fourcc(video, "strh") || !write_u32(video, 56)) {
        return false;
    }
    writer->strh_offset = ftell(video);
    for (int i = 0; i < 14; ++i) {
        if (!write_u32(video, 0)) {
            return false;
        }
    }
    if (!write_fourcc(video, "strf") || !write_u32(video, 40)) {
        return false;
    }
    writer->strf_offset = ftell(video);
    for (int i = 0; i < 10; ++i) {
        if (!write_u32(video, 0)) {
            return false;
        }
    }
    if (!write_fourcc(video, "LIST")) {
        return false;
    }
    writer->movi_size_offset = ftell(video);
    return write_u32(video, 0) && write_fourcc(video, "movi") &&
           (writer->movi_data_offset = ftell(video)) >= 0;
}

static bool patch_header_and_append_index(avi_writer_t *writer)
{
    FILE *video = writer->video;
    FILE *index = writer->index;
    long movi_end = ftell(video);
    if (movi_end < writer->movi_data_offset || fflush(index) != 0 || fseek(index, 0, SEEK_END) != 0) {
        return false;
    }
    long index_size = ftell(index);
    if (index_size < 0 || !write_fourcc(video, "idx1") || !write_u32(video, (uint32_t)index_size) ||
        fseek(index, 0, SEEK_SET) != 0) {
        return false;
    }

    char buffer[512];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), index)) > 0) {
        if (fwrite(buffer, 1, count, video) != count) {
            return false;
        }
    }
    if (ferror(index)) {
        return false;
    }

    long file_end = ftell(video);
    if (file_end < movi_end || fseek(video, writer->movi_size_offset, SEEK_SET) != 0 ||
        !write_u32(video, (uint32_t)(movi_end - writer->movi_data_offset + 4)) ||
        fseek(video, writer->avih_offset, SEEK_SET) != 0 || !write_u32(video, 1000000 / APP_RECORD_FPS) ||
        !write_u32(video, writer->max_frame_size * APP_RECORD_FPS) || !write_u32(video, 0) ||
        !write_u32(video, 0x10) || !write_u32(video, writer->frames) || !write_u32(video, 0) ||
        !write_u32(video, 1) || !write_u32(video, writer->max_frame_size) ||
        !write_u32(video, writer->width) || !write_u32(video, writer->height)) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!write_u32(video, 0)) {
            return false;
        }
    }

    if (fseek(video, writer->strh_offset, SEEK_SET) != 0 || !write_fourcc(video, "vids") ||
        !write_fourcc(video, "MJPG") || !write_u32(video, 0) || !write_u16(video, 0) ||
        !write_u16(video, 0) || !write_u32(video, 0) || !write_u32(video, 1) ||
        !write_u32(video, APP_RECORD_FPS) || !write_u32(video, 0) || !write_u32(video, writer->frames) ||
        !write_u32(video, writer->max_frame_size) || !write_u32(video, UINT32_MAX) || !write_u32(video, 0) ||
        !write_u16(video, 0) || !write_u16(video, 0) || !write_u16(video, writer->width) ||
        !write_u16(video, writer->height) || fseek(video, writer->strf_offset, SEEK_SET) != 0 ||
        !write_u32(video, 40) || !write_u32(video, writer->width) || !write_u32(video, writer->height) ||
        !write_u16(video, 1) || !write_u16(video, 24) || !write_fourcc(video, "MJPG") ||
        !write_u32(video, writer->max_frame_size)) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        if (!write_u32(video, 0)) {
            return false;
        }
    }
    return fseek(video, writer->riff_size_offset, SEEK_SET) == 0 &&
           write_u32(video, (uint32_t)(file_end - 8)) && fflush(video) == 0 && fsync(fileno(video)) == 0;
}

bool avi_writer_repair_legacy_index(const char *path)
{
    if (!path) {
        return false;
    }
    FILE *file = fopen(path, "rb+");
    if (!file) {
        return false;
    }

    bool repaired = false;
    long file_end = 0;
    char riff[4];
    char form[4];
    uint32_t riff_size = 0;
    if (read_fourcc(file, riff) && read_u32(file, &riff_size) && read_fourcc(file, form) &&
        memcmp(riff, "RIFF", 4) == 0 && memcmp(form, "AVI ", 4) == 0 &&
        fseek(file, 0, SEEK_END) == 0 && (file_end = ftell(file)) >= 12 &&
        riff_size == (uint32_t)(file_end - 8)) {
        long cursor = 12;
        while (cursor + 12 <= file_end) {
            char chunk_id[4];
            uint32_t chunk_size = 0;
            if (fseek(file, cursor, SEEK_SET) != 0 || !read_fourcc(file, chunk_id) ||
                !read_u32(file, &chunk_size) || chunk_size > (uint32_t)(file_end - cursor - 8)) {
                break;
            }
            if (memcmp(chunk_id, "LIST", 4) == 0) {
                char list_type[4];
                if (!read_fourcc(file, list_type)) {
                    break;
                }
                if (memcmp(list_type, "movi", 4) == 0) {
                    long movi_data = cursor + 12;
                    long index_pos = cursor + 8 + chunk_size + (chunk_size & 1U);
                    char first_chunk[4];
                    if (movi_data + 8 <= file_end && index_pos + 8 <= file_end &&
                        fseek(file, movi_data, SEEK_SET) == 0 && read_fourcc(file, first_chunk) &&
                        memcmp(first_chunk, "00dc", 4) == 0 && fseek(file, index_pos, SEEK_SET) == 0) {
                        char index_id[4];
                        uint32_t index_size = 0;
                        uint32_t first_offset = 0;
                        if (read_fourcc(file, index_id) && read_u32(file, &index_size) &&
                            memcmp(index_id, "idx1", 4) == 0 && index_size >= 16 &&
                            index_size % 16 == 0 && index_pos + 8 + index_size <= file_end) {
                            uint32_t entries = index_size / 16;
                            repaired = true;
                            if (fseek(file, index_pos + 8 + 8, SEEK_SET) != 0 ||
                                !read_u32(file, &first_offset) || first_offset != 0) {
                                repaired = false;
                            } else {
                                for (uint32_t entry = 0; entry < entries; ++entry) {
                                    long offset_pos = index_pos + 8 + (long)entry * 16 + 8;
                                    uint32_t offset = 0;
                                    if (fseek(file, offset_pos, SEEK_SET) != 0 || !read_u32(file, &offset) ||
                                        offset > UINT32_MAX - 4) {
                                        repaired = false;
                                        break;
                                    }
                                }
                                for (uint32_t entry = 0; repaired && entry < entries; ++entry) {
                                    long offset_pos = index_pos + 8 + (long)entry * 16 + 8;
                                    uint32_t offset = 0;
                                    if (fseek(file, offset_pos, SEEK_SET) != 0 || !read_u32(file, &offset) ||
                                        fseek(file, offset_pos, SEEK_SET) != 0 ||
                                        !write_u32(file, offset + 4)) {
                                        repaired = false;
                                    }
                                }
                            }
                            if (repaired && fflush(file) == 0 && fsync(fileno(file)) == 0) {
                                ESP_LOGW(TAG, "Repaired legacy AVI index offsets: %s", path);
                            } else {
                                repaired = false;
                            }
                        }
                    }
                    break;
                }
            }
            long next = cursor + 8 + chunk_size + (chunk_size & 1U);
            if (next <= cursor) {
                break;
            }
            cursor = next;
        }
    }
    fclose(file);
    return repaired;
}

void avi_writer_reset(avi_writer_t *writer)
{
    if (writer) {
        memset(writer, 0, sizeof(*writer));
    }
}

bool avi_writer_open(avi_writer_t *writer, const camera_fb_t *frame)
{
    if (!writer || !frame || writer->video || writer->index) {
        return false;
    }

    uint32_t sequence = next_video_sequence();
    bool path_available = false;
    for (unsigned int attempt = 0; attempt < 100000 && sequence != 0; ++attempt) {
        snprintf(writer->final_path, sizeof(writer->final_path), APP_SD_MOUNT_POINT "/record_%05" PRIu32 ".avi",
                 sequence);
        FILE *existing = fopen(writer->final_path, "rb");
        char temporary_path[sizeof(writer->temporary_path)];
        snprintf(temporary_path, sizeof(temporary_path), "%s.part", writer->final_path);
        FILE *temporary = fopen(temporary_path, "rb");
        if (!existing && !temporary) {
            path_available = true;
            break;
        }
        if (existing) {
            fclose(existing);
        }
        if (temporary) {
            fclose(temporary);
        }
        ++sequence;
    }
    if (!path_available) {
        return false;
    }

    snprintf(writer->temporary_path, sizeof(writer->temporary_path), "%s.part", writer->final_path);
    snprintf(writer->index_path, sizeof(writer->index_path), "%s.idx", writer->temporary_path);
    writer->video = fopen(writer->temporary_path, "wb+");
    writer->index = fopen(writer->index_path, "wb+");
    if (!writer->video || !writer->index || !write_header(writer, frame)) {
        avi_writer_abort(writer);
        return false;
    }
    return true;
}

bool avi_writer_write_frame(avi_writer_t *writer, const camera_fb_t *frame)
{
    if (!writer || !writer->video || !writer->index || !frame || frame->format != PIXFORMAT_JPEG ||
        frame->width != writer->width || frame->height != writer->height) {
        return false;
    }

    // idx1 offsets are relative to the start of the movi LIST, including its
    // four-byte list type. The first chunk therefore starts at offset 4.
    long frame_offset = ftell(writer->video) - writer->movi_data_offset + 4;
    avi_index_entry_t entry = {
        .chunk_id = 0x63643030,
        .flags = 0x10,
        .offset = (uint32_t)frame_offset,
        .size = (uint32_t)frame->len,
    };
    if (frame_offset < 0 || !write_fourcc(writer->video, "00dc") ||
        !write_u32(writer->video, (uint32_t)frame->len) ||
        fwrite(frame->buf, 1, frame->len, writer->video) != frame->len ||
        ((frame->len & 1) && fputc(0, writer->video) == EOF) ||
        fwrite(&entry, sizeof(entry), 1, writer->index) != 1) {
        return false;
    }
    ++writer->frames;
    if (frame->len > writer->max_frame_size) {
        writer->max_frame_size = frame->len;
    }
    return true;
}

bool avi_writer_would_exceed_limit(const avi_writer_t *writer, size_t frame_len)
{
    if (!writer || !writer->video || !writer->index) {
        return false;
    }

    long video_position = ftell(writer->video);
    long index_position = ftell(writer->index);
    // AVI chunk lengths and idx1 offsets are 32-bit fields. Keep this guard
    // local so a future limit change cannot silently create a corrupt file.
    if (video_position < 0 || index_position < 0 || frame_len > UINT32_MAX ||
        (uint64_t)video_position > UINT32_MAX || (uint64_t)index_position > UINT32_MAX) {
        return true;
    }

    uint64_t projected_size = (uint64_t)video_position + 8ULL + (uint64_t)frame_len +
                              (frame_len & 1ULL) + 8ULL + (uint64_t)index_position +
                              sizeof(avi_index_entry_t);
    return projected_size > APP_RECORD_MAX_FILE_BYTES;
}

bool avi_writer_finalize(avi_writer_t *writer)
{
    if (!writer || !writer->video || !writer->index || writer->frames == 0) {
        avi_writer_abort(writer);
        return false;
    }

    bool result = patch_header_and_append_index(writer);
    if (fclose(writer->index) != 0) {
        result = false;
    }
    writer->index = NULL;
    if (fclose(writer->video) != 0) {
        result = false;
    }
    writer->video = NULL;
    remove(writer->index_path);

    if (result && rename(writer->temporary_path, writer->final_path) != 0) {
        result = false;
    }
    if (!result) {
        remove(writer->temporary_path);
        ESP_LOGE(TAG, "Unable to finalize %s", writer->temporary_path);
    }
    return result;
}

void avi_writer_abort(avi_writer_t *writer)
{
    if (!writer) {
        return;
    }
    if (writer->index) {
        fclose(writer->index);
        writer->index = NULL;
    }
    if (writer->video) {
        fclose(writer->video);
        writer->video = NULL;
    }
    if (writer->index_path[0]) {
        remove(writer->index_path);
    }
    if (writer->temporary_path[0]) {
        remove(writer->temporary_path);
    }
}

bool avi_writer_is_open(const avi_writer_t *writer)
{
    return writer && writer->video && writer->index;
}

const char *avi_writer_path(const avi_writer_t *writer)
{
    return writer ? writer->final_path : "";
}

uint32_t avi_writer_frame_count(const avi_writer_t *writer)
{
    return writer ? writer->frames : 0;
}
