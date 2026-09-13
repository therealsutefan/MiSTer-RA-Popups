// achievements_tgfx16.cpp — RetroAchievements TurboGrafx-16 / PC Engine implementation

#include "achievements_console.h"
#include "achievements.h"
#include "ra_ramread.h"
#include "user_io.h"
#include "lib/md5/md5.h"
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef HAS_RCHEEVOS
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_hash.h"
#endif

// ---------------------------------------------------------------------------
// TG16 State
// ---------------------------------------------------------------------------

static console_state_t g_tgfx16_state = {0};
static int g_tgfx16_rtquery = 0; // 1 if FPGA supports realtime queries (v2)

// ---------------------------------------------------------------------------
// TG16 Selective Address Diagnostics
// ---------------------------------------------------------------------------

static void tgfx16_seladdr_dump_valcache(const char *label, void *map)
{
if (!map) return;
const uint8_t *base = (const uint8_t *)map;
int addr_count = ra_snes_addrlist_count();
const uint32_t *addrs = ra_snes_addrlist_addrs();

const ra_val_resp_hdr_t *resp = (const ra_val_resp_hdr_t *)(base + RA_SNES_VALCACHE_OFFSET);
const uint8_t *vals = base + RA_SNES_VALCACHE_OFFSET + 8;

int dump_len = addr_count < 32 ? addr_count : 32;
if (dump_len <= 0) dump_len = 32;
char hex[200];
int pos = 0, non_zero = 0;
for (int i = 0; i < dump_len && pos < (int)sizeof(hex) - 4; i++) {
pos += snprintf(hex + pos, sizeof(hex) - pos, "%02X ", vals[i]);
if (vals[i]) non_zero++;
}

ra_log_write("TGFX16 DUMP[%s] resp_id=%u resp_frame=%u addrs=%d non_zero=%d\n",
label, resp->response_id, resp->response_frame, addr_count, non_zero);
ra_log_write("TGFX16 DUMP[%s] VALCACHE[0..%d]: %s\n", label, dump_len - 1, hex);

// FPGA debug words
const uint8_t *dbg8 = base + 0x10;
uint16_t ok_cnt      = dbg8[0] | (dbg8[1] << 8);
uint16_t timeout_cnt = dbg8[2] | (dbg8[3] << 8);
uint8_t  dispatch_cnt = dbg8[6];
uint8_t  fpga_ver     = dbg8[7];
const uint8_t *dbg8b = base + 0x18;
uint16_t wram_cnt = dbg8b[0] | (dbg8b[1] << 8);
uint16_t cdram_cnt = dbg8b[2] | (dbg8b[3] << 8);
uint16_t first_addr = dbg8b[6] | (dbg8b[7] << 8);

ra_log_write("TGFX16 DUMP[%s] FPGA ver=0x%02X ok=%u timeout=%u dispatch=%u wram=%u cdram=%u faddr=0x%04X\n",
label, fpga_ver, ok_cnt, timeout_cnt, dispatch_cnt, wram_cnt, cdram_cnt, first_addr);

int show = addr_count < 10 ? addr_count : 10;
for (int i = 0; i < show; i++)
ra_log_write("TGFX16 DUMP[%s]   [%d] addr=0x%05X val=0x%02X\n", label, i, addrs[i], vals[i]);
}

// ---------------------------------------------------------------------------
// TG16 Implementation
// ---------------------------------------------------------------------------

static void tgfx16_init(void)
{
memset(&g_tgfx16_state, 0, sizeof(g_tgfx16_state));
g_tgfx16_rtquery = 0;
}

static void tgfx16_reset(void)
{
memset(&g_tgfx16_state, 0, sizeof(g_tgfx16_state));
g_tgfx16_rtquery = 0;
}

static uint32_t tgfx16_read_memory(void *map, uint32_t address, uint8_t *buffer, uint32_t num_bytes)
{
if (g_tgfx16_state.seladdr) {
if (g_tgfx16_state.collecting) {
for (uint32_t i = 0; i < num_bytes; i++)
ra_snes_addrlist_add(address + i);
}
if (g_tgfx16_state.cache_ready) {
if (achievements_smart_cache_enabled() && g_tgfx16_rtquery) {
// List-change misalignment is handled inside ra_snes_addrlist_*
// (active-snapshot mapping) — no rtquery-all reindex needed.
// Smart cache: serve from cache, rtquery on miss and schedule the
// address for the next FPGA batch via add_dynamic.
int any_miss = 0;
for (uint32_t i = 0; i < num_bytes; i++)
if (ra_snes_addrlist_contains(address + i) < 0) { any_miss = 1; break; }
if (!any_miss) {
for (uint32_t i = 0; i < num_bytes; i++)
buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
return num_bytes;
}
for (uint32_t i = 0; i < num_bytes; i++) {
if (ra_snes_addrlist_contains(address + i) >= 0) {
buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
} else {
buffer[i] = (uint8_t)ra_rtquery_read(map, address + i, 1);
ra_snes_addrlist_add_dynamic(address + i);
}
}
return num_bytes;
}
// Legacy path: read from cache
for (uint32_t i = 0; i < num_bytes; i++)
buffer[i] = ra_snes_addrlist_read_cached(map, address + i);
return num_bytes;
}
// Pre-cache rtquery fallback
if (g_tgfx16_rtquery && achievements_rtquery_enabled() && !g_tgfx16_state.collecting && num_bytes <= 4) {
uint32_t val = ra_rtquery_read(map, address, num_bytes);
for (uint32_t i = 0; i < num_bytes; i++)
buffer[i] = (uint8_t)(val >> (i * 8));
return num_bytes;
}
memset(buffer, 0, num_bytes);
return num_bytes;
}
memset(buffer, 0, num_bytes);
return num_bytes;
}

static int tgfx16_poll(void *map, void *client, int game_loaded)
{
#ifdef HAS_RCHEEVOS
if (!client || !game_loaded || !map || !g_tgfx16_state.seladdr)
return 0;

rc_client_t *rc_client = (rc_client_t *)client;

// ===================================================================
// Smart Cache path (Tier 1): rtquery handles cache misses, dynamic-only
// prune keeps the list bounded. Mirrors the NES/SNES/MD handlers.
// ===================================================================
if (achievements_smart_cache_enabled() && g_tgfx16_rtquery) {
if (ra_snes_addrlist_count() == 0 && !g_tgfx16_state.cache_ready) {
// Re-prime to WAITING before the all-zero collection frame so a
// mid-game re-bootstrap (stall recovery) cannot fire on zeros.
rc_client_reset(rc_client);
g_tgfx16_state.collecting = 1;
ra_snes_addrlist_begin_collect();
rc_client_do_frame(rc_client);
g_tgfx16_state.collecting = 0;
if (ra_snes_addrlist_end_collect(map))
ra_log_write("TGFX16 SmartCache: Bootstrap done, %d addrs\n",
ra_snes_addrlist_count());
else
ra_log_write("TGFX16 SmartCache: No addresses collected\n");
} else if (!g_tgfx16_state.cache_ready) {
if (ra_snes_addrlist_is_ready(map)) {
g_tgfx16_state.cache_ready = 1;
g_tgfx16_state.last_resp_frame = 0;
g_tgfx16_state.game_frames = 0;
g_tgfx16_state.poll_logged = 0;
clock_gettime(CLOCK_MONOTONIC, &g_tgfx16_state.cache_time);
// Discard the zero-primed bootstrap state (delta conditions
// would otherwise see 0 -> real transitions and fire).
rc_client_reset(rc_client);
ra_log_write("TGFX16 SmartCache: Cache active! %d addrs (rc_client reset)\n",
ra_snes_addrlist_count());
}
} else {
uint32_t resp_frame = ra_snes_addrlist_response_frame(map);
seladdr_resync_if_backward(&g_tgfx16_state, resp_frame, "TGFX16");
if (resp_frame > g_tgfx16_state.last_resp_frame) {
g_tgfx16_state.last_resp_frame = resp_frame;
g_tgfx16_state.game_frames++;
ra_frame_processed(resp_frame);

// Skip frames whose VALCACHE ordering is unconfirmed (see helper).
if (seladdr_frame_evaluable(map, "TGFX16"))
	rc_client_do_frame(rc_client);

if (achievements_smart_cleanup_enabled()
&& (g_tgfx16_state.game_frames % 3600 == 0)
&& ra_snes_addrlist_dyn_count() > 128) {
int removed = ra_snes_addrlist_prune_dynamic(map);
if (removed) {
ra_log_write("TGFX16 SmartCache: pruned %d dynamic addrs (%d static)\n",
removed, ra_snes_addrlist_count());
}
} else if (ra_snes_addrlist_has_pending()) {
ra_snes_addrlist_flush_dynamic(map);
}
}
}
uint32_t ms = g_tgfx16_state.game_frames / 300;
if (ms > 0 && ms != g_tgfx16_state.poll_logged) {
g_tgfx16_state.poll_logged = ms;
ra_log_write("POLL(TGFX16-SC): resp_frame=%u game_frames=%u addrs=%d dyn=%d\n",
g_tgfx16_state.last_resp_frame, g_tgfx16_state.game_frames,
ra_snes_addrlist_count(), ra_snes_addrlist_dyn_count());
}
return 1;
}

// ===================================================================
// Legacy path: periodic recollect (no rtquery)
// ===================================================================
if (ra_snes_addrlist_count() == 0 && !g_tgfx16_state.cache_ready) {
// Bootstrap: collect addresses.
// Re-prime to WAITING first (see smart-cache bootstrap).
rc_client_reset(rc_client);
g_tgfx16_state.collecting = 1;
ra_snes_addrlist_begin_collect();
rc_client_do_frame(rc_client);
g_tgfx16_state.collecting = 0;
int changed = ra_snes_addrlist_end_collect(map);
if (changed) {
ra_log_write("TGFX16 SelAddr: Bootstrap done, %d addrs\n",
ra_snes_addrlist_count());
tgfx16_seladdr_dump_valcache("bootstrap", map);
} else {
ra_log_write("TGFX16 SelAddr: No addresses collected\n");
}
} else if (!g_tgfx16_state.cache_ready) {
// Wait for FPGA response
if (ra_snes_addrlist_is_ready(map)) {
g_tgfx16_state.cache_ready = 1;
g_tgfx16_state.last_resp_frame = 0;
g_tgfx16_state.game_frames = 0;
g_tgfx16_state.poll_logged = 0;
clock_gettime(CLOCK_MONOTONIC, &g_tgfx16_state.cache_time);
// Discard the zero-primed bootstrap state (see smart-cache path).
rc_client_reset(rc_client);
ra_log_write("TGFX16 SelAddr: Cache active! (rc_client reset)\n");
tgfx16_seladdr_dump_valcache("cache-active", map);
}
} else {
// Normal frame processing
uint32_t resp_frame = ra_snes_addrlist_response_frame(map);
seladdr_resync_if_backward(&g_tgfx16_state, resp_frame, "TGFX16");
if (resp_frame > g_tgfx16_state.last_resp_frame) {
g_tgfx16_state.last_resp_frame = resp_frame;
g_tgfx16_state.game_frames++;
ra_frame_processed(resp_frame);
clock_gettime(CLOCK_MONOTONIC, &g_tgfx16_state.stall_time);
g_tgfx16_state.stall_frame = resp_frame;

if (g_tgfx16_state.game_frames <= 5) {
ra_log_write("TGFX16 SelAddr: GameFrame %u (resp_frame=%u)\n",
g_tgfx16_state.game_frames, resp_frame);
tgfx16_seladdr_dump_valcache("early-frame", map);
}

// Skip achievement processing while a recollect revision is in
// flight (newly collected addresses would read as 0).
if (ra_snes_addrlist_is_ready(map)) {
// Re-collect every ~5 min
// Smart cache mode: skip re-collect (no dynamic pointers in TGFx16)
int re_collect = !achievements_smart_cache_enabled()
&& (g_tgfx16_state.game_frames % 18000 == 0) && (g_tgfx16_state.game_frames > 0);
if (re_collect) {
g_tgfx16_state.collecting = 1;
ra_snes_addrlist_begin_collect();
}

rc_client_do_frame(rc_client);

if (re_collect) {
g_tgfx16_state.collecting = 0;
if (ra_snes_addrlist_end_collect(map)) {
ra_log_write("TGFX16 SelAddr: Address list refreshed, %d addrs\n",
ra_snes_addrlist_count());
}
}
}

} else {
	seladdr_check_stall_recovery(&g_tgfx16_state, resp_frame, "TGFX16");
}
}

// Periodic debug logging
uint32_t milestone = g_tgfx16_state.game_frames / 300;
if (milestone > 0 && milestone != g_tgfx16_state.poll_logged) {
g_tgfx16_state.poll_logged = milestone;
struct timespec now;
clock_gettime(CLOCK_MONOTONIC, &now);
double elapsed = (now.tv_sec - g_tgfx16_state.cache_time.tv_sec)
+ (now.tv_nsec - g_tgfx16_state.cache_time.tv_nsec) / 1e9;
double ms_per_cycle = (g_tgfx16_state.game_frames > 0) ?
(elapsed * 1000.0 / g_tgfx16_state.game_frames) : 0.0;
ra_log_write("POLL(TGFX16): resp_frame=%u game_frames=%u elapsed=%.1fs ms/cycle=%.1f addrs=%d\n",
g_tgfx16_state.last_resp_frame, g_tgfx16_state.game_frames, elapsed, ms_per_cycle,
ra_snes_addrlist_count());
if ((g_tgfx16_state.game_frames % 1800) < 300)
tgfx16_seladdr_dump_valcache("periodic", map);
}

return 1;
#else
return 0;
#endif
}

static int tgfx16_is_cd_image(const char *path)
{
const char *ext = strrchr(path, '.');
if (!ext) return 0;
return (strcasecmp(ext, ".cue") == 0 ||
        strcasecmp(ext, ".chd") == 0 ||
        strcasecmp(ext, ".ccd") == 0 ||
        strcasecmp(ext, ".iso") == 0 ||
        strcasecmp(ext, ".img") == 0);
}

static int tgfx16_calculate_hash(const char *rom_path, char *md5_hex_out)
{
char abs_path[1024];
if (rom_path[0] == '/') {
snprintf(abs_path, sizeof(abs_path), "%s", rom_path);
} else {
extern const char *getRootDir(void);
snprintf(abs_path, sizeof(abs_path), "%s/%s", getRootDir(), rom_path);
}

#ifdef HAS_RCHEEVOS
if (tgfx16_is_cd_image(rom_path)) {
// PC Engine CD: use rcheevos CD hashing with console_id 76
ra_log_write("TGFX16: CD image detected, using rc_hash (console_id=76): %s\n", abs_path);
if (rc_hash_generate_from_file(md5_hex_out, 76, abs_path)) {
ra_log_write("TGFX16 CD hash: %s\n", md5_hex_out);
return 1;
}
ra_log_write("TGFX16: rc_hash_generate_from_file failed for %s\n", abs_path);
return 0;
}
#endif

// HuCard ROM: .pce files may have a 512-byte header if size % 1024 == 512
FILE *f = fopen(abs_path, "rb");
if (!f) {
ra_log_write("TGFX16: Failed to open ROM for hashing: %s\n", abs_path);
return 0;
}

fseek(f, 0, SEEK_END);
long file_size = ftell(f);
fseek(f, 0, SEEK_SET);

if (file_size <= 0) { fclose(f); return 0; }

uint8_t *rom_data = (uint8_t *)malloc(file_size);
if (!rom_data) { fclose(f); return 0; }

size_t nread = fread(rom_data, 1, file_size, f);
fclose(f);

if ((long)nread != file_size) { free(rom_data); return 0; }

const uint8_t *hash_data = rom_data;
long hash_size = file_size;

// Skip 512-byte copier header if present
if ((file_size % 1024) == 512 && file_size > 512) {
ra_log_write("TGFX16: Copier header detected, skipping 512 bytes\n");
hash_data = rom_data + 512;
hash_size = file_size - 512;
}

MD5_CTX ctx;
MD5Init(&ctx);
MD5Update(&ctx, hash_data, hash_size);
unsigned char md5_bin[16];
MD5Final(md5_bin, &ctx);
for (int i = 0; i < 16; i++)
sprintf(&md5_hex_out[i * 2], "%02x", md5_bin[i]);
md5_hex_out[32] = '\0';

free(rom_data);
return 1;
}

static void tgfx16_set_hardcore(int enabled)
{
// FPGA hardcore signal (status[39]): forces the Game Genie engine off and
// hides the Cheats menu in hardware.
user_io_status_set("[39]", enabled ? 1 : 0);
// Legacy OSD "Cheats enabled" toggle (status[5], 1 = OFF)
user_io_status_set("[5]", enabled ? 1 : 0);
ra_log_write("TGFX16: Hardcore mode %s\n", enabled ? "enabled" : "disabled");
}

static int tgfx16_detect_protocol(void *map)
{
if (!ra_ramread_active(map)) {
	ra_log_write("TGFX16: FPGA mirror not detected -- RA support unavailable\n");
	return 0;
}
const ra_header_t *hdr = (const ra_header_t *)map;
if (hdr->region_count == 0) {
g_tgfx16_state.seladdr = 1;
ra_log_write("TGFX16 FPGA protocol: Selective Address (selective address reading)\n");
} else {
g_tgfx16_state.seladdr = 0;
ra_log_write("TGFX16 FPGA protocol: VBlank-gated mirror (region_count=%d)\n",
hdr->region_count);
}

if (g_tgfx16_state.seladdr) {
if (ra_rtquery_supported(map) && achievements_rtquery_enabled()) {
g_tgfx16_rtquery = 1;
ra_rtquery_init(map);
ra_log_write("TGFX16: Realtime queries supported and ENABLED\n");
} else if (ra_rtquery_supported(map)) {
g_tgfx16_rtquery = 0;
ra_log_write("TGFX16: Realtime queries supported but DISABLED by config\n");
} else {
g_tgfx16_rtquery = 0;
ra_log_write("TGFX16: Realtime queries NOT supported (FPGA v1)\n");
}
}
return 1;
}

// ---------------------------------------------------------------------------
// Console handler definition
// ---------------------------------------------------------------------------

const console_handler_t g_console_tgfx16 = {
.init = tgfx16_init,
.reset = tgfx16_reset,
.read_memory = tgfx16_read_memory,
.poll = tgfx16_poll,
.calculate_hash = tgfx16_calculate_hash,
.set_hardcore = tgfx16_set_hardcore,
.detect_protocol = tgfx16_detect_protocol,
.console_id = 8,  // RC_CONSOLE_PC_ENGINE
.name = "TGFX16"
};
