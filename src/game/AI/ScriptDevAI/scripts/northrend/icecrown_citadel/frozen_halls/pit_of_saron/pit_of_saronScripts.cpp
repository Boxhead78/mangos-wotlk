/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* ScriptData
SDName: pit_of_saron
SD%Complete: 100
SDComment:
SDCategory: Pit of Saron
EndScriptData */

/* ContentData
EndContentData */

#include "AI/ScriptDevAI/include/sc_common.h"
#include "pit_of_saron.h"
#include "Spells/SpellAuras.h"
#include "Spells/Scripts/SpellScript.h"

enum
{
    // Ambush event
    SPELL_EMPOWERED_SHADOW_BOLT         = 69528,
    SPELL_SUMMON_UNDEAD                 = 69516,

    // Icicles
    SPELL_ICICLE                        = 69426,
    SPELL_ICICLE_DUMMY                  = 69428,
    SPELL_ICE_SHARDS_H                  = 70827,            // used to check the tunnel achievement
};

/*######
## npc_ymirjar_deathbringer
######*/

struct npc_ymirjar_deathbringerAI : public ScriptedAI
{
    npc_ymirjar_deathbringerAI(Creature* pCreature) : ScriptedAI(pCreature) { Reset(); }

    uint32 m_uiShadowBoltTimer;

    void Reset() override
    {
        m_uiShadowBoltTimer = urand(1000, 3000);
    }

    void MovementInform(uint32 uiMotionType, uint32 uiPointId) override
    {
        if (uiMotionType != POINT_MOTION_TYPE || !uiPointId)
            return;

        DoCastSpellIfCan(m_creature, SPELL_SUMMON_UNDEAD);
    }

    void UpdateAI(const uint32 uiDiff) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        if (m_uiShadowBoltTimer < uiDiff)
        {
            if (Unit* pTarget = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0))
            {
                if (DoCastSpellIfCan(pTarget, SPELL_EMPOWERED_SHADOW_BOLT) == CAST_OK)
                    m_uiShadowBoltTimer = urand(2000, 3000);
            }
        }
        else
            m_uiShadowBoltTimer -= uiDiff;

        DoMeleeAttackIfReady();
    }
};

bool EffectDummyCreature_spell_summon_undead(Unit* /*pCaster*/, uint32 uiSpellId, SpellEffectIndex uiEffIndex, Creature* pCreatureTarget, ObjectGuid /*originalCasterGuid*/)
{
    // always check spellid and effectindex
    if (uiSpellId == SPELL_SUMMON_UNDEAD && uiEffIndex == EFFECT_INDEX_0)
    {
        if (pCreatureTarget->GetEntry() != NPC_YMIRJAR_DEATHBRINGER)
            return true;

        float fX, fY, fZ;
        for (uint8 i = 0; i < 4; ++i)
        {
            pCreatureTarget->GetNearPoint(pCreatureTarget, fX, fY, fZ, 0, frand(8.0f, 12.0f), M_PI_F * 0.5f * i);
            pCreatureTarget->SummonCreature(i % 2 ? NPC_YMIRJAR_WRATHBRINGER : NPC_YMIRJAR_FLAMEBEARER, fX, fY, fZ, 3.75f, TEMPSPAWN_DEAD_DESPAWN, 0);
        }

        // always return true when we are handling this spell and effect
        return true;
    }

    return false;
}

/*######
## npc_collapsing_icicle
######*/

struct npc_collapsing_icicleAI : public ScriptedAI
{
    npc_collapsing_icicleAI(Creature* pCreature) : ScriptedAI(pCreature)
    {
        m_pInstance = (instance_pit_of_saron*)pCreature->GetInstanceData();
        Reset();
    }

    instance_pit_of_saron* m_pInstance;

    void Reset() override
    {
        DoCastSpellIfCan(m_creature, SPELL_ICICLE_DUMMY, CAST_TRIGGERED);
        DoCastSpellIfCan(m_creature, SPELL_ICICLE, CAST_TRIGGERED);
    }

    void SpellHitTarget(Unit* pTarget, const SpellEntry* pSpell) override
    {
        // Mark the achiev failed
        if (pSpell->Id == SPELL_ICE_SHARDS_H && pTarget->GetTypeId() == TYPEID_PLAYER && m_pInstance)
            m_pInstance->SetSpecialAchievementCriteria(TYPE_ACHIEV_DONT_LOOK_UP, false);
    }

    void AttackStart(Unit* /*pWho*/) override { }
    void MoveInLineOfSight(Unit* /*pWho*/) override { }
    void UpdateAI(const uint32 /*uiDiff*/) override { }
};

/*######
## at_pit_of_saron
######*/

bool AreaTrigger_at_pit_of_saron(Player* pPlayer, AreaTriggerEntry const* pAt)
{
    if (pPlayer->IsGameMaster() || !pPlayer->IsAlive())
        return false;

    instance_pit_of_saron* pInstance = (instance_pit_of_saron*)pPlayer->GetInstanceData();
    if (!pInstance)
        return false;

    if (pAt->id == AREATRIGGER_ID_TUNNEL_START)
    {
        if (pInstance->GetData(TYPE_GARFROST) != DONE || pInstance->GetData(TYPE_KRICK) != DONE ||
                pInstance->GetData(TYPE_AMBUSH) != NOT_STARTED)
            return false;

        pInstance->DoStartAmbushEvent();
        pInstance->SetData(TYPE_AMBUSH, IN_PROGRESS);
        return true;
    }
    if (pAt->id == AREATRIGGER_ID_TUNNEL_END)
    {
        if (pInstance->GetData(TYPE_AMBUSH) != IN_PROGRESS)
            return false;

        pInstance->SetData(TYPE_AMBUSH, DONE);
        return true;
    }

    return false;
}

/*######
## spell_necromantic_power - 69347
######*/

struct spell_necromantic_power : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const
    {
        if (effIdx != EFFECT_INDEX_0)
            return;

        Unit* target = spell->GetUnitTarget();
        if (!target || !target->IsCreature())
            return;

        target->RemoveAurasDueToSpell(69413);

        // apply feign death aura 28728; calculated spell value is 22516, but this isn't used
        target->CastSpell(target, 28728, TRIGGERED_OLD_TRIGGERED);
    }
};

/*######
## spell_strangulating_aura - 69413
######*/

struct spell_strangulating_aura : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        Unit* target = aura->GetTarget();
        if (!target)
            return;

        target->SetLevitate(apply);

        // on apply move randomly around Tyrannus
        if (apply)
            target->GetMotionMaster()->MoveRandomAroundPoint(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), frand(5.0f, 8.0f), frand(5.0f, 10.0f));
        // on remove fall to the ground
        else
        {
            target->GetMotionMaster()->Clear(false, true);
            target->GetMotionMaster()->MoveFall();
        }
    }
};

/*######
## spell_feigh_death_pos_aura - 28728
######*/

struct spell_feigh_death_pos_aura : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        Unit* target = aura->GetTarget();
        if (!target || !target->IsCreature())
            return;

        target->SetFeignDeath(apply, aura->GetCasterGuid(), aura->GetId());

        if (!apply)
        {
            Creature* champion = static_cast<Creature*>(target);

            target->CastSpell(target, 69350, TRIGGERED_OLD_TRIGGERED);
            champion->UpdateEntry(36796);
            champion->AIM_Initialize();
        }
    }
};

void AddSC_pit_of_saron()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "npc_ymirjar_deathbringer";
    pNewScript->GetAI = &GetNewAIInstance<npc_ymirjar_deathbringerAI>;
    pNewScript->pEffectDummyNPC = &EffectDummyCreature_spell_summon_undead;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_collapsing_icicle";
    pNewScript->GetAI = &GetNewAIInstance<npc_collapsing_icicleAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "at_pit_of_saron";
    pNewScript->pAreaTrigger = &AreaTrigger_at_pit_of_saron;
    pNewScript->RegisterSelf();

    RegisterSpellScript<spell_necromantic_power>("spell_necromantic_power");
    RegisterAuraScript<spell_strangulating_aura>("spell_strangulating_aura");
    RegisterAuraScript<spell_feigh_death_pos_aura>("spell_feigh_death_pos_aura");
}
