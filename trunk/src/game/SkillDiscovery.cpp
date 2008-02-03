/* 
 * Copyright (C) 2005-2008 MaNGOS <http://www.mangosproject.org/>
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

#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "ProgressBar.h"
#include "Policies/SingletonImp.h"
#include "ObjectAccessor.h"
#include "World.h"
#include "Util.h"
#include "SkillDiscovery.h"

#include <map>

struct SkillDiscoveryEntry
{
    uint32  spellId;
    float   chance;

    SkillDiscoveryEntry()
        : spellId(0), chance(0) {}

    SkillDiscoveryEntry(uint16 _spellId, float _chance)
        : spellId(_spellId), chance(_chance) {}
};

typedef std::list<SkillDiscoveryEntry> SkillDiscoveryList;
typedef HM_NAMESPACE::hash_map<int32, SkillDiscoveryList> SkillDiscoveryMap;

static SkillDiscoveryMap SkillDiscoveryStore;

void LoadSkillDiscoveryTable()
{

    SkillDiscoveryStore.clear();                            // need for reload

    uint32 count = 0;

    //                                                 0          1           2
    QueryResult *result = WorldDatabase.PQuery("SELECT `spellId`, `reqSpell`, `chance` FROM `skill_discovery_template`");

    if (result)
    {
        barGoLink bar(result->GetRowCount());

        std::ostringstream ssNonDiscoverableEntries;

        do
        {
            Field *fields = result->Fetch();
            bar.step();

            uint32 spellId         = fields[0].GetUInt32();
            int32  reqSkillOrSpell = fields[1].GetInt32();
            float  chance          = fields[2].GetFloat();

            if(reqSkillOrSpell > 0)                         // spell case
            {
                SpellEntry const* spellEntry = sSpellStore.LookupEntry(reqSkillOrSpell);
                if( !spellEntry )
                {
                    sLog.outErrorDb("Spell (ID: %u) have not existed spell (ID: %i) in `reqSpell` field in `skill_discovery_template` table",spellId,reqSkillOrSpell);
                    continue;
                }

                if( spellEntry->Mechanic != MECHANIC_DISCOVERY )
                {
                    sLog.outErrorDb("Spell (ID: %u) not have have MECHANIC_DISCOVERY (28) value in Mechanic field in spell.dbc but listed in `skill_discovery_template` table",spellId);
                    continue;
                }
            }
            else if( reqSkillOrSpell == 0 )                 // skill case
            {
                SkillLineAbilityEntry const *pAbility = sSkillLineAbilityStore.LookupEntry(spellId);
                if ( pAbility )
                    reqSkillOrSpell = -int32(pAbility->skillId);
                else
                {
                    sLog.outErrorDb("Spell (ID: %u) not listed in `SkillLineAbility.dbc` but listed with `reqSpell`=0 in `skill_discovery_template` table",spellId);
                    continue;
                }
            }
            else
            {
                sLog.outErrorDb("Spell (ID: %u) have negative value in `reqSpell` field in `skill_discovery_template` table",spellId);
                continue;
            }

            if( chance <= 0 )                               // chance
            {
                ssNonDiscoverableEntries << "spellId = " << spellId << " reqSkillOrSpell = " << reqSkillOrSpell << " chance = " << chance << "\n";
                continue;
            }

            SkillDiscoveryStore[reqSkillOrSpell].push_back( SkillDiscoveryEntry(spellId, chance) );

            count++;
        } while (result->NextRow());

        delete result;

        sLog.outString();
        sLog.outString( ">> Loaded %u skill discovery definitions", count );
        if(!ssNonDiscoverableEntries.str().empty())
            sLog.outErrorDb("Some items can't be successfully discovered: have in chance field value < 0.000001 in `skill_discovery_template` DB table . List:\n%s",ssNonDiscoverableEntries.str().c_str());
    }
    else
    {
        sLog.outString();
        sLog.outString( ">> Loaded 0 skill discovery definitions. DB table `skill_discovery_template` is empty." );
    }
}

uint32 GetSkillDiscoverySpell(uint32 skillId, uint32 spellId, Player* player)
{
    // check spell case
    SkillDiscoveryMap::iterator tab = SkillDiscoveryStore.find(spellId);

    if(tab != SkillDiscoveryStore.end())
    {
        for(SkillDiscoveryList::iterator item_iter = tab->second.begin(); item_iter != tab->second.end(); ++item_iter)
        {
            if( roll_chance_f(item_iter->chance * sWorld.getRate(RATE_DROP_ITEMS))
                && !player->HasSpell(item_iter->spellId) )
                return item_iter->spellId;
        }

        return 0;
    }

    // check skill line case
    tab = SkillDiscoveryStore.find(-(int32)skillId);
    if(tab != SkillDiscoveryStore.end())
    {
        for(SkillDiscoveryList::iterator item_iter = tab->second.begin(); item_iter != tab->second.end(); ++item_iter)
        {
            if( roll_chance_f(item_iter->chance * sWorld.getRate(RATE_DROP_ITEMS))
                && !player->HasSpell(item_iter->spellId) )
                return item_iter->spellId;
        }

        return 0;
    }

    return 0;
}