#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "game/game.hpp"

#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

utils::hook::detour StartWeaponAnim_hook;
void StartWeaponAnim_stub(int localClientNum, int a2, unsigned int hand, unsigned int anim, unsigned int anim2, float transitiontime)
{
    if ((anim2 == 38 || anim2 == 40) && (anim == 1 || anim == 2))
    {
        anim2 = 27;
        transitiontime = 0.5f;
    }

    StartWeaponAnim_hook.invoke<void>(localClientNum, a2, hand, anim, anim2, transitiontime);
}

utils::hook::detour PM_BeginWeaponChange_hook;
void PM_BeginWeaponChange_stub(game::pmove_t* pm, game::Weapon newweapon, bool isNewAlternate, bool quick, unsigned int* holdrand)
{
    int weapAnim[2] = { pm->ps->weapState[0].weapAnim, pm->ps->weapState[1].weapAnim };
    PM_BeginWeaponChange_hook.invoke(pm, newweapon, isNewAlternate, quick, holdrand);

    if (pm->ps->pm_flags & 0x4000)
    {
        pm->ps->weapState[0].weapAnim = weapAnim[0];
        pm->ps->weapState[1].weapAnim = weapAnim[1];
    }
}

void PM_Weapon_BeginWeaponRaise_stub(game::pmove_t* pm, int, unsigned int, float, int, int hand)
{
    utils::hook::invoke<void>(0x140233DB0, pm, hand);//PM_WEAPON_idle
    pm->ps->weapState[hand].weapAnim = 1;
}

bool PM_Weapon_CheckForRightyTighty(game::pmove_t* pm)
{
    if ((pm->oldcmd.buttons & game::BUTTON_USERELOAD) == 0 && ((pm->cmd.buttons & game::BUTTON_USERELOAD) != 0) ||
        ((pm->oldcmd.buttons & game::BUTTON_RELOAD) == 0 && ((pm->cmd.buttons & game::BUTTON_RELOAD) != 0)))
    {
        if ((pm->ps->sprintState.lastSprintEnd - pm->ps->sprintState.lastSprintStart) < 50) //Increase to make righty tighty easier
        {
            if (game::PM_Weapon_AllowReload(pm->ps, game::WEAPON_HAND_RIGHT) && !game::PM_Weapon_AllowReload(pm->ps, game::WEAPON_HAND_LEFT))
            {
                game::PM_SetReloadingState(pm->ps, game::WEAPON_HAND_RIGHT);
                return true;
            }
        }
    }

    return false;
}

bool PM_Weapon_CheckForWristTwist(game::pmove_t* pm)
{
    if ((pm->cmd.buttons & game::BUTTON_USERELOAD) == 0 && ((pm->oldcmd.buttons & game::BUTTON_USERELOAD) != 0) ||
        (pm->cmd.buttons & game::BUTTON_RELOAD) == 0 && ((pm->oldcmd.buttons & game::BUTTON_RELOAD) != 0))
    {
        //if we are allowed to reload our left gun, and NOT allowed to reload right gun, start wrist twist
        if (game::PM_Weapon_AllowReload(pm->ps, game::WEAPON_HAND_LEFT) && !game::PM_Weapon_AllowReload(pm->ps, game::WEAPON_HAND_RIGHT))
        {
            game::PM_SetReloadingState(pm->ps, game::WEAPON_HAND_LEFT);
            //have no ideia what this could be on ghosts and im lazy
            //pm->ps->torsoAnim = 3181; //reload anim, overrides the reset in BG_ClearReloadAnim, makes it so the sprint anim is shown on 3rd person character 
            return true;
        }
    }

    return false;
}

void Sprint_State_Drop(game::pmove_t* pm)
{
    game::mp::playerState_s* ps = pm->ps;
    bool isDualWielding = game::BG_PlayerDualWieldingWeapon(ps, ps->weapon);
    int handIndex = game::BG_PlayerLastWeaponHand(ps);

    for (int i = 0; i <= handIndex; i++)
    {
        if (i == game::WEAPON_HAND_LEFT && PM_Weapon_CheckForRightyTighty(pm)) {
            continue;
        }

        ps->weapState[i].weaponState = game::WEAPON_SPRINT_DROP;
        ps->weapState[i].weaponTime = game::BG_SprintOutTime(ps->weapon, false, isDualWielding);
        ps->weapState[i].weaponDelay = 0;

        if ((BYTE)ps->pm_type < 7u)
        {
            ps->weapState[i].weapAnim = (0x21 | (ps->weapState[i].weaponState) & 0x800);
        }
    }
}

void Sprint_State_Raise(game::pmove_t* pm)
{
    game::mp::playerState_s* ps = pm->ps;
    int handIndex = game::BG_PlayerLastWeaponHand(ps);
	bool isDualWielding = game::BG_PlayerDualWieldingWeapon(ps, ps->weapon);

    for (int i = 0; i <= handIndex; i++)
    {
        ps->weapState[i].weaponState = game::WEAPON_SPRINT_RAISE;
        ps->weapState[i].weaponTime = game::BG_SprintInTime(ps->weapon, false, isDualWielding);
        ps->weapState[i].weaponDelay = 0;

        if ((BYTE)ps->pm_flags < 7u)
            ps->weapState[i].weapAnim = (ps->weapState[i].weaponState) & 0x800 | 0x1E;

        if (isDualWielding)
        {
            if (i == game::PlayerHandIndex::WEAPON_HAND_RIGHT)
            {
                PM_Weapon_CheckForRightyTighty(pm);
            }
            else if (i == game::PlayerHandIndex::WEAPON_HAND_LEFT)
            {
                PM_Weapon_CheckForWristTwist(pm);
            }
        }
    }
}

//thank you silver
utils::hook::detour PM_Weapon_CheckForSprint_hook;
void PM_Weapon_CheckForSprint_stub(game::pmove_t* pm)
{
    if (!pm->cmd.weapon) {
        return;
    }

    int weaponStateRight = pm->ps->weapState[game::WEAPON_HAND_RIGHT].weaponState;
    int weaponStateLeft = pm->ps->weapState[game::WEAPON_HAND_LEFT].weaponState;

    if ((pm->ps->pm_flags & 0x4) == 0 && weaponStateRight != game::WEAPON_FIRING && weaponStateRight != game::WEAPON_RECHAMBERING && weaponStateRight != game::WEAPON_MELEE_FIRE && weaponStateRight != game::WEAPON_MELEE_END)
    {
        if (weaponStateLeft != game::WEAPON_FIRING && weaponStateLeft != game::WEAPON_RECHAMBERING
            && weaponStateLeft != game::WEAPON_MELEE_FIRE && weaponStateLeft != game::WEAPON_MELEE_END
            && weaponStateRight != game::WEAPON_RAISING && weaponStateRight != game::WEAPON_RAISING_ALTSWITCH
            && weaponStateRight != game::WEAPON_DROPPING && weaponStateRight != game::WEAPON_DROPPING_QUICK && weaponStateRight != game::WEAPON_DROPPING_ALT
            && weaponStateRight != game::WEAPON_OFFHAND_INIT && weaponStateRight != game::WEAPON_OFFHAND_PREPARE && weaponStateRight != game::WEAPON_OFFHAND_HOLD && weaponStateRight != game::WEAPON_OFFHAND_HOLD_PRIMED && weaponStateRight != game::WEAPON_OFFHAND_END
            )
        {
            if (((pm->ps->pm_flags & 0x4000) != 0) && (weaponStateRight != game::WEAPON_SPRINT_RAISE && weaponStateRight != game::WEAPON_SPRINT_LOOP && weaponStateRight != game::WEAPON_SPRINT_DROP))
            {
                Sprint_State_Raise(pm);
            }
            else if (((pm->ps->pm_flags & 0x4000) == 0) && (weaponStateRight == game::WEAPON_SPRINT_RAISE || weaponStateRight == game::WEAPON_SPRINT_LOOP))
            {
                Sprint_State_Drop(pm);
            }
        }
    }
}

class cymatic_hooks final : public component_interface
{
public:
    void post_unpack() override
    {
		PM_Weapon_CheckForSprint_hook.create(0x140232B80, PM_Weapon_CheckForSprint_stub);
        StartWeaponAnim_hook.create(0x1402B3810, StartWeaponAnim_stub);
        PM_BeginWeaponChange_hook.create(0x14022E9D0, PM_BeginWeaponChange_stub);
        utils::hook::call(0x1402319F1, PM_Weapon_BeginWeaponRaise_stub);
    }
};

REGISTER_COMPONENT(cymatic_hooks)
