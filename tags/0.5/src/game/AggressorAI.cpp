/* 
 * Copyright (C) 2005,2006 MaNGOS <http://www.mangosproject.org/>
 *
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

#include "AggressorAI.h"
#include "Errors.h"
#include "Creature.h"
#include "Player.h"
#include "Utilities.h"
#include "FactionTemplateResolver.h"
#include "TargetedMovementGenerator.h"
#include "Database/DBCStores.h"
#include <list>

int
AggressorAI::Permissible(const Creature *creature)
{
    // have some hostile factions, it will be selected by IsHostileTo check at MoveInLineOfSight
    if( !creature->IsNeutralToAll() )
        return PERMIT_BASE_PROACTIVE;

    return PERMIT_BASE_NO;
}

AggressorAI::AggressorAI(Creature &c) : i_creature(c), i_victimGuid(0), i_state(STATE_NORMAL), i_tracker(TIME_INTERVAL_LOOK)
{
}

void
AggressorAI::MoveInLineOfSight(Unit *u)
{
    if( !i_creature.getVictim() && u->isTargetableForAttack() && IsVisible(u) && u->isInAccessablePlaceFor(&i_creature))
    {
        float attackRadius = i_creature.GetAttackDistance(u);
        if(i_creature.GetDistanceSq(u) <= attackRadius*attackRadius)
        {
            if( i_creature.IsHostileTo( u ) )
                AttackStart(u);
            if(u->isStealth())
                u->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
        }
    }
}

void
AggressorAI::HealBy(Unit *healer, uint32 amount_healed)
{
}

void
AggressorAI::DamageInflict(Unit *healer, uint32 amount_healed)
{
}

bool
AggressorAI::_needToStop() const
{
    if( !i_creature.isAlive() || !i_creature.getVictim() || !i_creature.getVictim()->isTargetableForAttack() )
        return true;

    if(!i_creature.getVictim()->isInAccessablePlaceFor(&i_creature))
        return true;

    float rx,ry,rz;
    i_creature.GetRespawnCoord(rx, ry, rz);
    float spawndist=i_creature.GetDistanceSq(rx,ry,rz);
    float length = i_creature.GetDistanceSq(i_creature.getVictim());
    float hostillen=i_creature.GetHostilityDistance( i_creature.getVictim()->GetGUID() );
    return (( length > (10.0f + hostillen) * (10.0f + hostillen) && spawndist > 6400.0f )
        || ( length > (20.0f + hostillen) * (20.0f + hostillen) && spawndist > 2500.0f )
        || ( length > (30.0f + hostillen) * (30.0f + hostillen) ));
}

void AggressorAI::AttackStop(Unit *)
{
}

void AggressorAI::_stopAttack()
{
    if( !i_creature.isAlive() )
    {
        DEBUG_LOG("Creature stopped attacking cuz his dead [guid=%u]", i_creature.GetGUIDLow());
        i_victimGuid = 0;
        i_creature.CombatStop();
        return;
    }

    assert( i_victimGuid );

    Unit* victim = ObjectAccessor::Instance().GetUnit(i_creature, i_victimGuid );

    assert(!i_creature.getVictim() || i_creature.getVictim() == victim);

    if( !victim  )
    {
        DEBUG_LOG("Creature stopped attacking because victim is non exist [guid=%u]", i_creature.GetGUIDLow());
    }
    else if( !victim->isAlive() )
    {
        DEBUG_LOG("Creature stopped attacking cuz his victim is dead [guid=%u]", i_creature.GetGUIDLow());
    }
    else if( victim->isStealth() )
    {
        DEBUG_LOG("Creature stopped attacking cuz his victim is stealth [guid=%u]", i_creature.GetGUIDLow());
    }
    else if( victim->isInFlight() )
    {
        DEBUG_LOG("Creature stopped attacking cuz his victim is fly away [guid=%u]", i_creature.GetGUIDLow());
    }
    else
    {
        DEBUG_LOG("Creature stopped attacking due to target out run him [guid=%u]", i_creature.GetGUIDLow());
        //i_state = STATE_LOOK_AT_VICTIM;
        //i_tracker.Reset(TIME_INTERVAL_LOOK);
    }

    i_victimGuid = 0;
    i_creature.AttackStop();

    // TargetedMovementGenerator can be already remove at i_creature death and not updated i_victimGuid
    if( i_creature->top()->GetMovementGeneratorType() == MovementGenerator::TARGETED_MOTION_TYPE )
        static_cast<TargetedMovementGenerator *>(i_creature->top())->TargetedHome(i_creature);
}

void
AggressorAI::UpdateAI(const uint32 diff)
{
    // update i_victimGuid if i_creature.getVictim() !=0 and changed
    if(i_creature.getVictim())
        i_victimGuid = i_creature.getVictim()->GetGUID();

    // i_creature.getVictim() can't be used for check in case stop fighting, i_creature.getVictim() clear�� at Unit death etc.
    if( i_victimGuid )
    {
        if( _needToStop() )
        {
            DEBUG_LOG("Aggressor AI stoped attacking [guid=%u]", i_creature.GetGUIDLow());
            _stopAttack();                                  // i_victimGuid == 0 && i_creature.getVictim() == NULL now
            return;
        }

        assert((i_victimGuid != 0) == (i_creature.getVictim() != NULL) && "i_victimGuid and i_creature.getVictim() not synchronized.");

        //switch( i_state )
        //{
        /*case STATE_LOOK_AT_VICTIM:
        {
            if( IsVisible(i_pVictim) )
            {
                DEBUG_LOG("Victim %u re-enters creature's aggro radius fater stop attacking", i_pVictim->GetGUIDLow());
                i_state = STATE_NORMAL;
                i_creature->MovementExpired();
                break;                                  // move on
                // back to the cat and mice game if you move back in range
            }

            i_tracker.Update(diff);
            if( i_tracker.Passed() )
            {
                i_creature->MovementExpired();
                DEBUG_LOG("Creature running back home [guid=%u]", i_creature.GetGUIDLow());
                if( i_creature->top()->GetMovementGeneratorType() == MovementGenerator::TARGETED_MOTION_TYPE )
                    static_cast<TargetedMovementGenerator *>(i_creature->top())->TargetedHome(i_creature);
                i_state = STATE_NORMAL;
                i_pVictim = NULL;
            }
            else
            {

                float dx = i_pVictim->GetPositionX() - i_creature.GetPositionX();
                float dy = i_pVictim->GetPositionY() - i_creature.GetPositionY();
                float orientation = (float)atan2((double)dy, (double)dx);
                i_creature.Relocate(i_pVictim->GetPositionX(), i_pVictim->GetPositionY(), i_pVictim->GetPositionZ(), orientation);
            }

            break;
        }*/
        //case STATE_NORMAL:
        //{
        if( i_creature.IsStopped() )
        {
            if( i_creature.isAttackReady() )
            {
                Unit* newtarget = i_creature.SelectHostilTarget();
                if(newtarget)
                    AttackStart(newtarget);

                if(!i_creature.canReachWithAttack(i_creature.getVictim()))
                    return;
                i_creature.AttackerStateUpdate(i_creature.getVictim());
                i_creature.resetAttackTimer();

                if ( !i_creature.getVictim() )
                    return;

                if( _needToStop() )
                    _stopAttack();
            }
        }
        //   break;
        //}
        //default:
        //    break;
        //}
    }
}

bool
AggressorAI::IsVisible(Unit *pl) const
{
    bool seestealth = true;
    uint32 sight = sWorld.getConfig(CONFIG_SIGHT_MONSTER);
    float dist = i_creature.GetDistanceSq(pl);
    float attackRadius = i_creature.GetAttackDistance(pl);
    if(pl->isStealth())
    {
        int32 seevaluse;
        int notfront = i_creature.isInFront(pl, sight) ? 0 : 1;
        seevaluse = 5  + i_creature.getLevel() * 5 + i_creature.m_detectStealth - pl->m_stealthvalue - (uint32)sqrt(dist/100) - 50 * notfront;
        if(seevaluse<0)
            seestealth = false;
        else if(seevaluse>=int32(urand(0,30)))
            seestealth = true;
        else seestealth = false;
    }
                                                            // offset=1.0
    return seestealth && (dist * 1.0 <= sight) ;
}

void
AggressorAI::AttackStart(Unit *u)
{
    if( i_creature.getVictim() || !u )
        return;
    //    DEBUG_LOG("Creature %s tagged a victim to kill [guid=%u]", i_creature.GetName(), u->GetGUIDLow());
    i_victimGuid = u->GetGUID();
    i_creature.Attack(u);
    i_creature.resetAttackTimer();
    i_creature->Mutate(new TargetedMovementGenerator(*u));
}
