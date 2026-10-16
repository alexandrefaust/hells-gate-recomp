// Hooks on the game's damage path: dante_damage_trace logs every hit, and
// with DANTESINFERNO_DEV_CHEATS (development builds only) god mode and one-hit
// kills for quick playthroughs.
//
// Every hit on any character ends in the damage component's apply function
// sub_82239AA8(component, damage_info, blocked), called by its damage event
// handler sub_82239130: health = GetHealth() - damage_info+48, clamped, then
// SetHealth() and death handling. The victim's team is read like the game's
// STANDARD_DAMAGE_AT_PLAYER_TEAM filter (sub_82239EE0): team component of
// the component's owner entity, team 2 = player team (Dante, Lucia).
//
// Cheats: hit reactions and knockback come from the damage event itself, which many
// components listen for. God mode drops damage events aimed at the player
// team where they are posted: sub_823EBA20(message), the usual path, which
// also gates the knockback impulse (message+8 = victim entity), and the
// generic event post sub_8252BE60(events, type, target), which the other
// damage posters use.

#include "guest_memory.h"

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>

#include <rex/logging/macros.h>

#include <cstdint>

REXCVAR_DEFINE_BOOL(dante_damage_trace, false, "Diagnostics",
                    "Log every hit: victim, player team or not, amount, damage multiplier, flags");

#ifdef DANTESINFERNO_DEV_CHEATS
REXCVAR_DEFINE_BOOL(dante_cheat_god, false, "Cheats",
                    "The player team takes no damage and no hits (no hit reactions or knockback)");
REXCVAR_DEFINE_BOOL(dante_cheat_one_hit, false, "Cheats",
                    "Any damage kills everything outside the player team");
#endif

namespace {

using namespace guest;

constexpr uint32_t kComponentOwner = 72;  // damage component -> entity
constexpr uint32_t kComponentMultiplier = 56;  // float, applied to incoming damage
constexpr uint32_t kComponentFlags = 60;       // STANDARD_DAMAGE_FLAG_* (1 = invincible)
constexpr uint32_t kDamageAmount = 48;    // damage info, float
constexpr uint32_t kDamageFlags = 68;
// Entity component lookup: slot index table at entity+72 (indexed by the
// component type's slot byte, +12), component pointers at entity+76.
constexpr uint32_t kEntitySlotTable = 72, kEntityComponents = 76;
constexpr uint32_t kTeamComponentType = 0x82C9F0C8;  // slot byte at +66
constexpr uint32_t kTeamOffset = 24;
constexpr uint32_t kPlayerTeam = 2;
#ifdef DANTESINFERNO_DEV_CHEATS
constexpr float kOneHitDamage = 1.0e9f;
constexpr uint32_t kMessageVictim = 8;  // damage message -> victim entity
// The two damage event types (descriptor addresses).
constexpr uint32_t kDamageEvents[] = {0x82CA737C, 0x82CA75E4};
#endif

bool IsPlayerTeam(const uint8_t* base, uint32_t entity) {
  if (!IsPlausibleAddress(entity)) return false;
  uint32_t components = Load32(base, entity + kEntityComponents);
  uint32_t slots = Load32(base, entity + kEntitySlotTable);
  if (!IsPlausibleAddress(components) || !IsPlausibleAddress(slots)) return false;
  uint8_t type_slot = *Ptr(base, kTeamComponentType + 66);
  uint8_t slot = *Ptr(base, slots + type_slot + 12);
  uint32_t team = Load32(base, components + slot * 4u);
  return IsPlausibleAddress(team) && Load32(base, team + kTeamOffset) == kPlayerTeam;
}

}  // namespace

REX_EXTERN(__imp__sub_82239AA8);
REX_HOOK_RAW(sub_82239AA8) {
  uint32_t component = ctx.r3.u32, info = ctx.r4.u32;
  bool active = REXCVAR_GET(dante_damage_trace);
#ifdef DANTESINFERNO_DEV_CHEATS
  active = active || REXCVAR_GET(dante_cheat_god) || REXCVAR_GET(dante_cheat_one_hit);
#endif
  if (!active || !IsPlausibleAddress(component) || !IsPlausibleAddress(info)) {
    __imp__sub_82239AA8(ctx, base);
    return;
  }
  uint32_t entity = Load32(base, component + kComponentOwner);
  const bool player = IsPlayerTeam(base, entity);
#ifdef DANTESINFERNO_DEV_CHEATS
  if (player && REXCVAR_GET(dante_cheat_god)) {
    StoreFloat(base, info + kDamageAmount, 0.0f);
  } else if (!player && REXCVAR_GET(dante_cheat_one_hit) &&
             LoadFloat(base, info + kDamageAmount) > 0.0f) {
    StoreFloat(base, info + kDamageAmount, kOneHitDamage);
  }
#endif
  if (REXCVAR_GET(dante_damage_trace)) {
    REXLOG_INFO("DAMAGE: victim {:08X} {} amount {:.2f} multiplier {:.2f} component flags {:08X} "
                "hit flags {:08X} blocked {}",
                entity, player ? "player" : "other", LoadFloat(base, info + kDamageAmount),
                LoadFloat(base, component + kComponentMultiplier),
                Load32(base, component + kComponentFlags), Load32(base, info + kDamageFlags),
                ctx.r5.u32 & 1);
  }
  __imp__sub_82239AA8(ctx, base);
}

#ifdef DANTESINFERNO_DEV_CHEATS
// Damage message -> damage event to the victim. Returns whether it was sent.
REX_EXTERN(__imp__sub_823EBA20);
REX_HOOK_RAW(sub_823EBA20) {
  if (REXCVAR_GET(dante_cheat_god) && IsPlausibleAddress(ctx.r3.u32) &&
      IsPlayerTeam(base, Load32(base, ctx.r3.u32 + kMessageVictim))) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_823EBA20(ctx, base);
}

// Generic event post (r4 = event type, r5 = target entity).
REX_EXTERN(__imp__sub_8252BE60);
REX_HOOK_RAW(sub_8252BE60) {
  if (REXCVAR_GET(dante_cheat_god) &&
      (ctx.r4.u32 == kDamageEvents[0] || ctx.r4.u32 == kDamageEvents[1]) &&
      IsPlayerTeam(base, ctx.r5.u32)) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8252BE60(ctx, base);
}
#endif  // DANTESINFERNO_DEV_CHEATS
