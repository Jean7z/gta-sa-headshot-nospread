#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#include <aml-psdk/game_sa/plugin.h>
#include <aml-psdk/game_sa/other/WeaponInfo.h>

#include <sys/mman.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000u
#endif

MYMODCFG(net.psdk.samod.ahead, SA Android Headshot & NoSpread, 1.0, Jean7z)

static ConfigEntry *cfgNoSpread, *cfgHeadshot, *cfgPlayerOnly, *cfgHeadRangeMul, *cfgHeadRangeMin;

#if defined(__aarch64__)

// libGTASA.so 2.10 arm64 constants
#define FMOV_S8_WZR  0x1E2703E8u // fmov s8, wzr
#define RET_INSTR    0xD65F03C0u // ret
#define PD_OFF       0x540u      // CPed::m_pPlayerData (non-null ONLY for the player ped)

/* Scatter sites inside CWeapon::FireInstantHit (2.10 arm64): every place that
 * loads/uses the weapon spread into s8. Replacing each with fmov s8, wzr kills
 * the spread (precision). Player-scoped trampolines run the vanilla op for NPCs
 * and zero it only for the player -> NPC gunfights stay 100% stock. */
static const uint32_t g_origInstrs[] = {
    0xBD4B5508u, // 0x7019FC ldr s8, [x8, #0xb54]
    0xBD451528u, // 0x701A6C ldr s8, [x9, #0x514]
    0xBC685928u, // 0x701A94 ldr s8, [x9, w8, uxtw #2]
    0xBD490108u, // 0x701C74 ldr s8, [x8, #0x900]
    0x1E2A1008u, // 0x701C84 fmov s8, #0.25
    0xBD4BE128u, // 0x701C90 ldr s8, [x9, #0xbe0]
    0x1E2A1008u, // 0x701CA0 fmov s8, #0.25
    0x1E2E1008u, // 0x701F40 fmov s8, #1.0
    0x1E200888u, // 0x7023FC fmul s8, s4, s0
};
static const uint32_t g_sites[] = {
    0x7019FC, 0x701A6C, 0x701A94,
    0x701C74, 0x701C84, 0x701C90, 0x701CA0,
    0x701F40,
    0x7023FC,
};
#define SITE_COUNT (sizeof(g_sites) / sizeof(g_sites[0]))
#define STUB_WORDS 5

static uint32_t encLdrX16FromX19Offset(void)
{
    // ldr x16, [x19, #0x540]  (imm12 = PD_OFF >> 3)
    return 0xF9400000u | ((PD_OFF >> 3) << 10) | (19u << 5) | 16u;
}
static uint32_t encCbzX16SkipTwice(void)
{
    // cbz x16, #+8  -> skip the fmov s8, wzr
    return 0xB4000000u | (2u << 5) | 16u;
}
static uint32_t encBl(uintptr_t from, uintptr_t to)
{
    int64_t diff = (int64_t)to - (int64_t)from;
    if(diff < -0x08000000 || diff > 0x07FFFFFF) return 0;
    return 0x94000000u | (uint32_t)(((uint64_t)diff >> 2) & 0x03FFFFFFu);
}

// Read free (unmapped) gaps from the game's own address space.
static size_t ReadFreeGaps(uintptr_t* starts, uintptr_t* sizes, size_t max)
{
    size_t n = 0;
    FILE* f = fopen("/proc/self/maps", "r");
    if(!f) return 0;
    uintptr_t prevEnd = 0;
    char line[512];
    while(fgets(line, sizeof(line), f))
    {
        uintptr_t start = 0, end = 0;
        if(sscanf(line, "%lx-%lx", (unsigned long*)&start, (unsigned long*)&end) != 2) continue;
        if(prevEnd && start > prevEnd && n < max)
        {
            uintptr_t gs = prevEnd, ge = start;
            if(ge - gs >= 0x2000) { starts[n] = gs; sizes[n] = ge - gs; ++n; }
        }
        prevEnd = end;
    }
    fclose(f);
    return n;
}

// Replace every scatter site with `bl` -> stub that runs the ORIGINAL
// instruction (vanilla for NPCs) and zeroes s8 only for the player.
// Returns true if the player-scoped trampolines were installed.
static bool BuildPlayerOnlyTrampolines(uintptr_t pGame)
{
    const size_t totalBytes = SITE_COUNT * STUB_WORDS * 4;
    uintptr_t stub = 0;

    // Preferred: find the free VA gap closest to libGTASA that sits within
    // BL reach (imm26, +-128 MB) of every scatter site, then MAP_FIXED.
    {
        uintptr_t starts[64], sizes[64];
        size_t n = ReadFreeGaps(starts, sizes, 64);
        int64_t bestScore = 0x7FFFFFFFFFFFFFFFll;
        uintptr_t bestStart = 0;
        for(size_t i = 0; i < n; ++i)
        {
            int64_t worst = 0;
            for(size_t s = 0; s < SITE_COUNT; ++s)
            {
                int64_t d = (int64_t)starts[i] - (int64_t)(pGame + g_sites[s]);
                if(d < 0) d = -d;
                if(d > worst) worst = d;
            }
            if(worst <= 0x07F00000 && worst < bestScore)
            {
                bestScore = worst;
                bestStart = starts[i];
            }
        }
        if(bestStart)
        {
            void* m = mmap((void*)bestStart, 0x1000 + totalBytes,
                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if(m == MAP_FAILED)
                logger->Error("PlayerOnly: gap 0x%lX unavailable, trying hints",
                              (unsigned long)bestStart);
            else stub = (uintptr_t)m;
        }
        else
            logger->Error("PlayerOnly: no free gap within +-128MB of libGTASA, trying hints");
    }

    // Fallback: kernel-chosen placement near libGTASA.
    if(!stub)
    {
        const uintptr_t hints[] = {
            pGame - 0x2000000u, pGame, pGame + 0x1000000u,
            pGame - 0x10000000u, 0,
        };
        for(size_t h = 0; h < sizeof(hints)/sizeof(hints[0]); ++h)
        {
            void* m = mmap((void*)(hints[h] & ~0xFFFu), 0x1000 + totalBytes,
                           PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if(m == MAP_FAILED) continue;
            bool reachable = true;
            for(size_t i = 0; i < SITE_COUNT; ++i)
            {
                if(!encBl(pGame + g_sites[i], (uintptr_t)m + i * (STUB_WORDS * 4)))
                { reachable = false; break; }
            }
            if(!reachable) { munmap(m, 0x1000 + totalBytes); continue; }
            stub = (uintptr_t)m;
            break;
        }
        if(!stub)
        {
            logger->Error("PlayerOnly: no reachable exec zone (falling back)");
            return false;
        }
    }

    const uint32_t ldrPd   = encLdrX16FromX19Offset();
    const uint32_t cbzSkip = encCbzX16SkipTwice();

    for(size_t i = 0; i < SITE_COUNT; ++i)
    {
        uint32_t* s = (uint32_t*)(stub + i * (STUB_WORDS * 4));
        s[0] = g_origInstrs[i];      // vanilla scatter op (NPCs / non-player)
        s[1] = ldrPd;                // ldr x16, [x19, #0x540]
        s[2] = cbzSkip;              // cbz x16, +8 (skip the fmov if not player)
        s[3] = FMOV_S8_WZR;          // fmov s8, wzr (player -> zero scatter)
        s[4] = RET_INSTR;            // ret
    }

    if(mprotect((void*)stub, 0x1000 + totalBytes, PROT_READ | PROT_EXEC) != 0)
    {
        logger->Error("PlayerOnly: execmem denied (falling back)");
        munmap((void*)stub, 0x1000 + totalBytes);
        return false;
    }
    __builtin___clear_cache((char*)stub, (char*)stub + totalBytes);

    for(size_t i = 0; i < SITE_COUNT; ++i)
    {
        uintptr_t site = pGame + g_sites[i];
        uint32_t bl = encBl(site, stub + i * (STUB_WORDS * 4));
        if(!bl) { logger->Error("PlayerOnly: BL range broken at 0x%X", g_sites[i]); return false; }
        aml->Unprot(site, 4);
        aml->Write32(site, bl);
    }
    logger->Info("PlayerOnly trampolines installed on %d sites (stubs at 0x%lX)",
                 (int)SITE_COUNT, (unsigned long)stub);
    return true;
}

/* Scatter is silenced by replacing every s8 load with fmov s8, wzr. */
static void ApplyGlobalNoSpread(uintptr_t pGame)
{
    for(size_t i = 0; i < SITE_COUNT; ++i)
    {
        aml->Unprot(pGame + g_sites[i], 4);
        aml->Write32(pGame + g_sites[i], FMOV_S8_WZR);
    }
    logger->Info("NoSpread patches applied (%d sites)", (int)SITE_COUNT);
}

/* Always-HEAD bone ------------------------------------------------------------
 * CPlayerPed::ProcessControl ends its target-bone decision with
 * `csel w8, w2, w8, NE` at 0x5C25A4: the HEAD bone wins only in the in-range
 * case, so a frame spent mid re-aim / transiently out of range picks SPINE and
 * the fired round lands on the chest. Replacing it with `mov w8, #5` forces
 * BONE_HEAD so the reticle AND the hit point are ALWAYS the head, including
 * while switching targets. Lives inside CPlayerPed::ProcessControl, so it only
 * ever affects the PLAYER (NPCs never run this code). */
static void ApplyAlwaysHeadBone(uintptr_t pGame)
{
    uintptr_t site = pGame + 0x5C25A4u;
    if(*(uint32_t*)site != 0x1A881148u)   // csel w8, w2, w8, NE
    {
        logger->Error("AlwaysHeadBone: unexpected opcode 0x%X at 0x5C25A4",
                      *(uint32_t*)site);
        return;
    }
    aml->Unprot(site, 4);
    aml->Write32(site, 0x528000A8u);      // mov w8, #5 (BONE_HEAD)
    logger->Info("AlwaysHeadBone patched: reticle + hit bone forced to HEAD");
}
#endif

/* CWeaponInfo::GetTargetHeadRange: the game already computes a dynamic,
 * weapon-derived head range (m_fWeaponRange * K * (skill + 2)). Instead of
 * the old hard-coded 1e6, scale that natural value by HeadRangeMul and floor
 * it at HeadRangeMin, so the aim/camera "magnetism" to the head is softened
 * (it is only consulted inside CPlayerPed::ProcessControl -> player-only). */
DECL_HOOK(float, CWeaponInfo__GetTargetHeadRange, CWeaponInfo* _this)
{
    float headRange = CWeaponInfo__GetTargetHeadRange(_this);
    if(!cfgHeadshot->GetBool()) return headRange;

    headRange *= cfgHeadRangeMul->GetFloat();
    if(headRange < cfgHeadRangeMin->GetFloat()) headRange = cfgHeadRangeMin->GetFloat();
    return headRange;
}

ON_MOD_LOAD()
{
    logger->SetTag("HeadshotNoSpread");

    cfgHeadshot     = cfg->Bind("Headshot", true, "Aimbot");
    cfgNoSpread     = cfg->Bind("NoSpread", true, "Aimbot");
    cfgPlayerOnly   = cfg->Bind("PlayerOnly", true, "Aimbot");
    cfgHeadRangeMul = cfg->Bind("HeadRangeMul", 2.0f, "Aimbot");
    cfgHeadRangeMin = cfg->Bind("HeadRangeMin", 30.0f, "Aimbot");

    uintptr_t pGame = aml->GetLib("libGTASA.so");
    if(!pGame)
    {
        logger->Error("libGTASA.so not found!");
        return;
    }

#if defined(__aarch64__)
    if(cfgNoSpread->GetBool())
    {
        if(cfgPlayerOnly->GetBool())
        {
            if(!BuildPlayerOnlyTrampolines(pGame)) ApplyGlobalNoSpread(pGame);
        }
        else ApplyGlobalNoSpread(pGame);
    }
    if(cfgHeadshot->GetBool()) ApplyAlwaysHeadBone(pGame);
#else
    logger->Info("NoSpread patches are arm64-only, skipped");
#endif

    HOOK(CWeaponInfo__GetTargetHeadRange,
         GetMainLibrarySymbol("_ZN11CWeaponInfo18GetTargetHeadRangeEv"));

    logger->Info("Aimbot loaded. Headshot=%s NoSpread=%s PlayerOnly=%s HeadRange=(mul %.2f, min %.1f)",
        cfgHeadshot->GetBool() ? "ON" : "OFF", cfgNoSpread->GetBool() ? "ON" : "OFF",
        cfgPlayerOnly->GetBool() ? "ON" : "OFF",
        cfgHeadRangeMul->GetFloat(), cfgHeadRangeMin->GetFloat());
}