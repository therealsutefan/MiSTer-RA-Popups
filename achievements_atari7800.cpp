// achievements_atari7800.cpp -- RetroAchievements Atari 7800 handler
//
// The Atari 7800 core shares the "ATARI7800" MiSTer core with the Atari 2600.
// When the loaded ROM is a .a78 file, achievements.cpp switches g_active_handler
// to this handler (console_id=51, RC_CONSOLE_ATARI_7800).
//
// DDRAM memory map (set by ra_7800_mirror.sv):
//   map + 0x0000 : RACH header (magic, region_count=2, flags, version)
//   map + 0x0008 : frame_counter + reserved
//   map + 0x0010 : region 0 descriptor (size=2048, ddram_offset=0x0100)
//   map + 0x0018 : region 1 descriptor (size=2048, ddram_offset=0x0900)
//   map + 0x0100 : ram0 (2048 bytes) -- CPU $2000-$27FF
//   map + 0x0900 : ram1 (2048 bytes) -- CPU $1800-$1FFF
//
// Address mapping for rcheevos (hardware address -> BRAM index -> map offset):
//   $2000-$27FF  ->  BRAM[addr & 0x7FF]  ->  map + 0x100 + (addr & 0x7FF)
//   $0040-$00FF  ->  BRAM[addr & 0x7FF]  ->  map + 0x100 + (addr & 0x7FF)  (zero-page mirror)
//   $0140-$01FF  ->  BRAM[addr & 0x7FF]  ->  map + 0x100 + (addr & 0x7FF)  (stack mirror)
//   $1800-$1FFF  ->  BRAM[addr & 0x7FF]  ->  map + 0x900 + (addr & 0x7FF)

#include "achievements_console.h"
#include "achievements.h"
#include "ra_ramread.h"
#include "user_io.h"
#include "lib/md5/md5.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef HAS_RCHEEVOS
#include "rc_consoles.h"
#endif

// ---------------------------------------------------------------------------
// Atari 7800 hash calculation
//
// The RetroAchievements database hashes Atari 7800 ROMs by stripping the
// 128-byte .a78 header (if present) and computing MD5 of the remaining data.
// The header is identified by the 9-byte magic "ATARI7800" at byte offset 1.
// This matches the algorithm in rcheevos rc_hash_7800().
// ---------------------------------------------------------------------------
static int atari7800_calculate_hash(const char *rom_path, char *md5_hex_out)
{
    // rom_path is usually relative to the MiSTer root (e.g. "games/ATARI7800/x.a78")
    char abs_path[1024];
    if (rom_path[0] == '/') {
        snprintf(abs_path, sizeof(abs_path), "%s", rom_path);
    } else {
        extern const char *getRootDir(void);
        snprintf(abs_path, sizeof(abs_path), "%s/%s", getRootDir(), rom_path);
    }

    FILE *f = fopen(abs_path, "rb");
    if (!f) {
        ra_log_write("ATARI7800: Failed to open ROM: %s\n", abs_path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(f);
        return 0;
    }

    uint8_t *rom_data = (uint8_t *)malloc((size_t)file_size);
    if (!rom_data) {
        fclose(f);
        return 0;
    }

    size_t nread = fread(rom_data, 1, (size_t)file_size, f);
    fclose(f);

    if ((long)nread != file_size) {
        free(rom_data);
        return 0;
    }

    // Strip 128-byte header if the .a78 magic is present at offset 1
    const uint8_t *hash_data = rom_data;
    size_t hash_size = (size_t)file_size;
    if (file_size > 128 && memcmp(rom_data + 1, "ATARI7800", 9) == 0) {
        ra_log_write("ATARI7800: .a78 header detected, skipping 128 bytes\n");
        hash_data = rom_data + 128;
        hash_size = (size_t)file_size - 128;
    }

    MD5_CTX ctx;
    MD5Init(&ctx);
    MD5Update(&ctx, (unsigned char *)hash_data, hash_size);
    unsigned char md5_bin[16];
    MD5Final(md5_bin, &ctx);

    for (int i = 0; i < 16; i++)
        sprintf(&md5_hex_out[i * 2], "%02x", md5_bin[i]);
    md5_hex_out[32] = '\0';

    ra_log_write("ATARI7800: hash=%s (rom_size=%ld, hashed=%zu)\n",
                 md5_hex_out, file_size, hash_size);

    free(rom_data);
    return 1;
}

// ---------------------------------------------------------------------------
// Memory read
// ---------------------------------------------------------------------------
static uint8_t s_last_20c4 = 0xFF;
static uint8_t s_last_205b = 0xFF;

static uint32_t atari7800_read_memory(void *map, uint32_t address,
                                       uint8_t *buffer, uint32_t num_bytes)
{
    uint32_t r = ra_ramread_atari7800_read(map, address, buffer, num_bytes);
    // Spy on 0x20C4 (hammer state) and 0x205B (ResetIf guard) for debugging
    if (address <= 0x20C4 && address + num_bytes > 0x20C4) {
        uint8_t v = buffer[0x20C4 - address];
        if (v != s_last_20c4) {
            ra_log_write("A7800 READ 0x20C4: %02X->%02X  0x205B=%02X\n",
                         s_last_20c4, v, s_last_205b);
            s_last_20c4 = v;
        }
    }
    if (address <= 0x205B && address + num_bytes > 0x205B) {
        uint8_t v = buffer[0x205B - address];
        if (v != s_last_205b) {
            ra_log_write("A7800 READ 0x205B: %02X->%02X  0x20C4=%02X\n",
                         s_last_205b, v, s_last_20c4);
            s_last_205b = v;
        }
    }
    return r;
}

// ---------------------------------------------------------------------------
// Protocol detection
// ---------------------------------------------------------------------------
static int atari7800_detect_protocol(void *map)
{
    if (!ra_ramread_active(map)) {
        ra_log_write("ATARI7800: FPGA mirror not detected\n");
        return 0;
    }
    uint8_t region_count = ((const uint8_t *)map)[4];
    if (region_count != 2) {
        ra_log_write("ATARI7800: Expected region_count=2, got %u -- "
                     "FPGA may not have 7800 mirror support\n", region_count);
        return 0;
    }
    ra_log_write("ATARI7800: Protocol OK (region_count=%u)\n", region_count);
    return 1;
}

// ---------------------------------------------------------------------------
// Poll
// Returns 1 so the default loop is suppressed: rc_client_do_frame is called
// here only on REAL frame transitions, never from the idle path.
// This prevents idle calls from consuming delta conditions prematurely.
// ---------------------------------------------------------------------------
static uint32_t s_last_frame = 0xFFFFFFFF;
static uint32_t s_poll_count = 0;
static uint8_t  s_last_ram0[2048];  // snapshot from previous frame

// Vblank interval tracking (file-level so atari7800_reset() can clear them)
static struct timespec s_last_vblank    = {0, 0};
static long   s_vblank_min_us           = 100000L;
static long   s_vblank_max_us           = 0L;
static long   s_vblank_sum_us           = 0L;
static uint32_t s_vblank_count          = 0;
static uint32_t s_do_frame_count        = 0;

static int atari7800_poll(void *map, void *client, int game_loaded)
{
    s_poll_count++;

    const uint8_t *b = (const uint8_t *)map;

    if (!b) return 1;

    uint32_t frame_ctr = b[8]  | ((uint32_t)b[9] << 8)
                       | ((uint32_t)b[10] << 16) | ((uint32_t)b[11] << 24);

    int frame_changed = (frame_ctr != s_last_frame);

    if (frame_changed) {
        s_last_frame = frame_ctr;

        // --- Vblank interval measurement ---
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (s_last_vblank.tv_sec != 0 || s_last_vblank.tv_nsec != 0) {
            long us = (long)(now.tv_sec  - s_last_vblank.tv_sec)  * 1000000L
                     + (now.tv_nsec - s_last_vblank.tv_nsec) / 1000L;
            if (us < s_vblank_min_us) s_vblank_min_us = us;
            if (us > s_vblank_max_us) s_vblank_max_us = us;
            s_vblank_sum_us += us;
            s_vblank_count++;
            // Log anomalies immediately (>25ms or <8ms for ~60Hz signal)
            if (us > 25000L || us < 8000L)
                ra_log_write("A7800 VBLANK_ANOMALY: frame=%u interval=%ldus(%.1fHz)\n",
                             frame_ctr, us, us > 0 ? 1000000.0 / us : 0.0);
            // Log summary every 60 frames (~1 second)
            if ((s_vblank_count % 60) == 0) {
                ra_log_write("A7800 TIMING[%u]: avg=%.0fus(%.1fHz) min=%ldus max=%ldus do_frame=%u\n",
                             s_vblank_count,
                             (double)s_vblank_sum_us / s_vblank_count,
                             60000000.0 / (double)s_vblank_sum_us,
                             s_vblank_min_us, s_vblank_max_us, s_do_frame_count);
                s_vblank_min_us = 100000L; s_vblank_max_us = 0L;
                s_vblank_sum_us = 0L;      s_do_frame_count = 0;
            }
        }
        s_last_vblank = now;

        // --- RAM delta logging ---
        const uint8_t *ram0 = b + 0x100;

        // Build compact delta line: "[IDX]old>new ..."
        // Buffer: each entry is " [XXX]XX>XX" = 12 chars, max ~340 entries
        char delta_buf[4096];
        int  delta_len    = 0;
        int  changed_count = 0;

        for (int i = 0; i < 2048 && delta_len < 3800; i++) {
            if (ram0[i] != s_last_ram0[i]) {
                delta_len += snprintf(delta_buf + delta_len,
                                      (int)sizeof(delta_buf) - delta_len,
                                      " [%03X]%02X>%02X",
                                      i, s_last_ram0[i], ram0[i]);
                changed_count++;
            }
        }

        if (changed_count > 0)
            ra_log_write("A7800 frame=%u chg=%d:%s\n",
                         frame_ctr, changed_count, delta_buf);

        memcpy(s_last_ram0, ram0, 2048);

        // --- rc_client_do_frame: only on real frame transitions ---
        // NOTE: we return 1 to suppress the default loop's idle calls.
        // Idle calls between FPGA frames risk reading mid-frame CPU state.
#ifdef HAS_RCHEEVOS
        if (game_loaded && client) {
            s_do_frame_count++;
            rc_client_do_frame((rc_client_t *)client);
        }
#endif
    }

    return 1;  // suppress default idle rc_client_do_frame calls
}

// ---------------------------------------------------------------------------
// Init / reset
// ---------------------------------------------------------------------------
static void atari7800_init(void)
{
    // nothing
}

static void atari7800_reset(void)
{
    s_last_frame     = 0xFFFFFFFF;
    s_last_20c4      = 0xFF;
    s_last_205b      = 0xFF;
    memset(s_last_ram0, 0, sizeof(s_last_ram0));
    s_last_vblank.tv_sec  = 0;
    s_last_vblank.tv_nsec = 0;
    s_vblank_min_us  = 100000L;
    s_vblank_max_us  = 0L;
    s_vblank_sum_us  = 0L;
    s_vblank_count   = 0;
    s_do_frame_count = 0;
}

static void atari7800_set_hardcore(int enabled)
{
    (void)enabled;  // no FPGA hardcore bits for 7800
}

// ---------------------------------------------------------------------------
// Console handler definition
// ---------------------------------------------------------------------------
const console_handler_t g_console_atari7800 = {
    .init             = atari7800_init,
    .reset            = atari7800_reset,
    .read_memory      = atari7800_read_memory,
    .poll             = atari7800_poll,
    .calculate_hash   = atari7800_calculate_hash,
    .set_hardcore     = atari7800_set_hardcore,
    .detect_protocol  = atari7800_detect_protocol,
    .console_id       = 51,    // RC_CONSOLE_ATARI_7800
    .name             = NULL,  // Not registered by name; switched via .a78 extension
    .hardcore_protected = 0,
};
