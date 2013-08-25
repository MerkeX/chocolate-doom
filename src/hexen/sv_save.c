// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 1993-2008 Raven Software
// Copyright(C) 2008 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
//-----------------------------------------------------------------------------


// HEADER FILES ------------------------------------------------------------

#include "h2def.h"
#include "i_system.h"
#include "m_misc.h"
#include "i_swap.h"
#include "p_local.h"

// MACROS ------------------------------------------------------------------

#define MAX_TARGET_PLAYERS 512
#define MOBJ_NULL -1
#define MOBJ_XX_PLAYER -2
#define GET_BYTE (*SavePtr.b++)
#define GET_WORD SHORT(*SavePtr.w++)
#define GET_LONG LONG(*SavePtr.l++)
#define MAX_MAPS 99
#define BASE_SLOT 6
#define REBORN_SLOT 7
#define REBORN_DESCRIPTION "TEMP GAME"
#define MAX_THINKER_SIZE 256

// TYPES -------------------------------------------------------------------

typedef enum
{
    ASEG_GAME_HEADER = 101,
    ASEG_MAP_HEADER,
    ASEG_WORLD,
    ASEG_POLYOBJS,
    ASEG_MOBJS,
    ASEG_THINKERS,
    ASEG_SCRIPTS,
    ASEG_PLAYERS,
    ASEG_SOUNDS,
    ASEG_MISC,
    ASEG_END
} gameArchiveSegment_t;

typedef enum
{
    TC_NULL,
    TC_MOVE_CEILING,
    TC_VERTICAL_DOOR,
    TC_MOVE_FLOOR,
    TC_PLAT_RAISE,
    TC_INTERPRET_ACS,
    TC_FLOOR_WAGGLE,
    TC_LIGHT,
    TC_PHASE,
    TC_BUILD_PILLAR,
    TC_ROTATE_POLY,
    TC_MOVE_POLY,
    TC_POLY_DOOR
} thinkClass_t;

typedef struct
{
    thinkClass_t tClass;
    think_t thinkerFunc;
    void (*mangleFunc) ();
    void (*restoreFunc) ();
    size_t size;
} thinkInfo_t;

typedef struct
{
    thinker_t thinker;
    sector_t *sector;
} ssthinker_t;

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

void P_SpawnPlayer(mapthing_t * mthing);

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

static void ArchiveWorld(void);
static void UnarchiveWorld(void);
static void ArchivePolyobjs(void);
static void UnarchivePolyobjs(void);
static void ArchiveMobjs(void);
static void UnarchiveMobjs(void);
static void ArchiveThinkers(void);
static void UnarchiveThinkers(void);
static void ArchiveScripts(void);
static void UnarchiveScripts(void);
static void ArchivePlayers(void);
static void UnarchivePlayers(void);
static void ArchiveSounds(void);
static void UnarchiveSounds(void);
static void ArchiveMisc(void);
static void UnarchiveMisc(void);
static void SetMobjArchiveNums(void);
static void RemoveAllThinkers(void);
static int GetMobjNum(mobj_t * mobj);
static void SetMobjPtr(mobj_t **ptr, unsigned int archiveNum);
static void MangleSSThinker(ssthinker_t * sst);
static void RestoreSSThinker(ssthinker_t * sst);
static void RestoreSSThinkerNoSD(ssthinker_t * sst);
static void MangleScript(acs_t * script);
static void RestoreScript(acs_t * script);
static void RestorePlatRaise(plat_t * plat);
static void RestoreMoveCeiling(ceiling_t * ceiling);
static void AssertSegment(gameArchiveSegment_t segType);
static void ClearSaveSlot(int slot);
static void CopySaveSlot(int sourceSlot, int destSlot);
static void CopyFile(char *sourceName, char *destName);
static boolean ExistingFile(char *name);
static void OpenStreamOut(char *fileName);
static void CloseStreamOut(void);
static void StreamOutBuffer(void *buffer, int size);
static void StreamOutByte(byte val);
static void StreamOutWord(unsigned short val);
static void StreamOutLong(unsigned int val);
static void StreamOutPtr(void *ptr);

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

#define DEFAULT_SAVEPATH                "hexndata/"

char *SavePath = DEFAULT_SAVEPATH;

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static int MobjCount;
static mobj_t **MobjList;
static mobj_t ***TargetPlayerAddrs;
static int TargetPlayerCount;
static byte *SaveBuffer;
static boolean SavingPlayers;
static union
{
    byte *b;
    short *w;
    int *l;
} SavePtr;
static FILE *SavingFP;

// This list has been prioritized using frequency estimates
static thinkInfo_t ThinkerInfo[] = {
    {
     TC_MOVE_FLOOR,
     T_MoveFloor,
     MangleSSThinker,
     RestoreSSThinker,
     sizeof(floormove_t)}
    ,
    {
     TC_PLAT_RAISE,
     T_PlatRaise,
     MangleSSThinker,
     RestorePlatRaise,
     sizeof(plat_t)}
    ,
    {
     TC_MOVE_CEILING,
     T_MoveCeiling,
     MangleSSThinker,
     RestoreMoveCeiling,
     sizeof(ceiling_t)}
    ,
    {
     TC_LIGHT,
     T_Light,
     MangleSSThinker,
     RestoreSSThinkerNoSD,
     sizeof(light_t)}
    ,
    {
     TC_VERTICAL_DOOR,
     T_VerticalDoor,
     MangleSSThinker,
     RestoreSSThinker,
     sizeof(vldoor_t)}
    ,
    {
     TC_PHASE,
     T_Phase,
     MangleSSThinker,
     RestoreSSThinkerNoSD,
     sizeof(phase_t)}
    ,
    {
     TC_INTERPRET_ACS,
     T_InterpretACS,
     MangleScript,
     RestoreScript,
     sizeof(acs_t)}
    ,
    {
     TC_ROTATE_POLY,
     T_RotatePoly,
     NULL,
     NULL,
     sizeof(polyevent_t)}
    ,
    {
     TC_BUILD_PILLAR,
     T_BuildPillar,
     MangleSSThinker,
     RestoreSSThinker,
     sizeof(pillar_t)}
    ,
    {
     TC_MOVE_POLY,
     T_MovePoly,
     NULL,
     NULL,
     sizeof(polyevent_t)}
    ,
    {
     TC_POLY_DOOR,
     T_PolyDoor,
     NULL,
     NULL,
     sizeof(polydoor_t)}
    ,
    {
     TC_FLOOR_WAGGLE,
     T_FloorWaggle,
     MangleSSThinker,
     RestoreSSThinker,
     sizeof(floorWaggle_t)}
    ,
    {                           // Terminator
     TC_NULL, NULL, NULL, NULL, 0}
};

// CODE --------------------------------------------------------------------

// Autogenerated functions for reading/writing structs:

//
// acsstore_t
//

static void StreamIn_acsstore_t(acsstore_t *str)
{
    int i;

    // int map;
    str->map = GET_LONG;

    // int script;
    str->script = GET_LONG;

    // byte args[4];
    for (i=0; i<4; ++i)
    {
        str->args[i] = GET_BYTE;
    }
}

static void StreamOut_acsstore_t(acsstore_t *str)
{
    int i;

    // int map;
    StreamOutLong(str->map);

    // int script;
    StreamOutLong(str->script);

    // byte args[4];
    for (i=0; i<4; ++i)
    {
        StreamOutByte(str->args[i]);
    }
}


//
// ticcmd_t
// (this is based on the Vanilla definition of the struct)
//

static void StreamIn_ticcmd_t(ticcmd_t *str)
{
    // char forwardmove;
    str->forwardmove = GET_BYTE;

    // char sidemove;
    str->sidemove = GET_BYTE;

    // short angleturn;
    str->angleturn = GET_WORD;

    // short consistancy;
    str->consistancy = GET_WORD;

    // byte chatchar;
    str->chatchar = GET_BYTE;

    // byte buttons;
    str->buttons = GET_BYTE;

    // byte lookfly;
    str->lookfly = GET_BYTE;

    // byte arti;
    str->arti = GET_BYTE;
}

static void StreamOut_ticcmd_t(ticcmd_t *str)
{
    // char forwardmove;
    StreamOutByte(str->forwardmove);

    // char sidemove;
    StreamOutByte(str->sidemove);

    // short angleturn;
    StreamOutWord(str->angleturn);

    // short consistancy;
    StreamOutWord(str->consistancy);

    // byte chatchar;
    StreamOutByte(str->chatchar);

    // byte buttons;
    StreamOutByte(str->buttons);

    // byte lookfly;
    StreamOutByte(str->lookfly);

    // byte arti;
    StreamOutByte(str->arti);
}



//
// inventory_t
//

static void StreamIn_inventory_t(inventory_t *str)
{
    // int type;
    str->type = GET_LONG;

    // int count;
    str->count = GET_LONG;
}

static void StreamOut_inventory_t(inventory_t *str)
{
    // int type;
    StreamOutLong(str->type);

    // int count;
    StreamOutLong(str->count);
}


//
// pspdef_t
//

static void StreamIn_pspdef_t(pspdef_t *str)
{
    int state_num;

    // state_t *state;

    // This is a pointer; it is stored as an index into the states table.

    state_num = GET_LONG;

    if (state_num != 0)
    {
        str->state = states + state_num;
    }
    else
    {
        str->state = NULL;
    }

    // int tics;
    str->tics = GET_LONG;

    // fixed_t sx, sy;
    str->sx = GET_LONG;
    str->sy = GET_LONG;
}

static void StreamOut_pspdef_t(pspdef_t *str)
{
    // state_t *state;
    // This is a pointer; store the index in the states table,
    // rather than the pointer itself.
    if (str->state != NULL)
    {
        StreamOutLong(str->state - states);
    }
    else
    {
        StreamOutLong(0);
    }

    // int tics;
    StreamOutLong(str->tics);

    // fixed_t sx, sy;
    StreamOutLong(str->sx);
    StreamOutLong(str->sy);
}


//
// player_t
//

static void StreamIn_player_t(player_t *str)
{
    int i;

    // mobj_t *mo;
    // Pointer value is reset on load.
    GET_LONG;
    str->mo = NULL;

    // playerstate_t playerstate;
    str->playerstate = GET_LONG;

    // ticcmd_t cmd;
    StreamIn_ticcmd_t(&str->cmd);

    // pclass_t class;
    str->class = GET_LONG;

    // fixed_t viewz;
    str->viewz = GET_LONG;

    // fixed_t viewheight;
    str->viewheight = GET_LONG;

    // fixed_t deltaviewheight;
    str->deltaviewheight = GET_LONG;

    // fixed_t bob;
    str->bob = GET_LONG;

    // int flyheight;
    str->flyheight = GET_LONG;

    // int lookdir;
    str->lookdir = GET_LONG;

    // boolean centering;
    str->centering = GET_LONG;

    // int health;
    str->health = GET_LONG;

    // int armorpoints[NUMARMOR];
    for (i=0; i<NUMARMOR; ++i)
    {
        str->armorpoints[i] = GET_LONG;
    }

    // inventory_t inventory[NUMINVENTORYSLOTS];
    for (i=0; i<NUMINVENTORYSLOTS; ++i)
    {
        StreamIn_inventory_t(&str->inventory[i]);
    }

    // artitype_t readyArtifact;
    str->readyArtifact = GET_LONG;

    // int artifactCount;
    str->artifactCount = GET_LONG;

    // int inventorySlotNum;
    str->inventorySlotNum = GET_LONG;

    // int powers[NUMPOWERS];
    for (i=0; i<NUMPOWERS; ++i)
    {
        str->powers[i] = GET_LONG;
    }

    // int keys;
    str->keys = GET_LONG;

    // int pieces;
    str->pieces = GET_LONG;

    // signed int frags[MAXPLAYERS];
    for (i=0; i<MAXPLAYERS; ++i)
    {
        str->frags[i] = GET_LONG;
    }

    // weapontype_t readyweapon;
    str->readyweapon = GET_LONG;

    // weapontype_t pendingweapon;
    str->pendingweapon = GET_LONG;

    // boolean weaponowned[NUMWEAPONS];
    for (i=0; i<NUMWEAPONS; ++i)
    {
        str->weaponowned[i] = GET_LONG;
    }

    // int mana[NUMMANA];
    for (i=0; i<NUMMANA; ++i)
    {
        str->mana[i] = GET_LONG;
    }

    // int attackdown, usedown;
    str->attackdown = GET_LONG;
    str->usedown = GET_LONG;

    // int cheats;
    str->cheats = GET_LONG;

    // int refire;
    str->refire = GET_LONG;

    // int killcount, itemcount, secretcount;
    str->killcount = GET_LONG;
    str->itemcount = GET_LONG;
    str->secretcount = GET_LONG;

    // char message[80];
    for (i=0; i<80; ++i)
    {
        str->message[i] = GET_BYTE;
    }

    // int messageTics;
    str->messageTics = GET_LONG;

    // short ultimateMessage;
    str->ultimateMessage = GET_WORD;

    // short yellowMessage;
    str->yellowMessage = GET_WORD;

    // int damagecount, bonuscount;
    str->damagecount = GET_LONG;
    str->bonuscount = GET_LONG;

    // int poisoncount;
    str->poisoncount = GET_LONG;

    // mobj_t *poisoner;
    // Pointer value is reset.
    GET_LONG;
    str->poisoner = NULL;

    // mobj_t *attacker;
    // Pointer value is reset.
    GET_LONG;
    str->attacker = NULL;

    // int extralight;
    str->extralight = GET_LONG;

    // int fixedcolormap;
    str->fixedcolormap = GET_LONG;

    // int colormap;
    str->colormap = GET_LONG;

    // pspdef_t psprites[NUMPSPRITES];
    for (i=0; i<NUMPSPRITES; ++i)
    {
        StreamIn_pspdef_t(&str->psprites[i]);
    }

    // int morphTics;
    str->morphTics = GET_LONG;

    // unsigned int jumpTics;
    str->jumpTics = GET_LONG;

    // unsigned int worldTimer;
    str->worldTimer = GET_LONG;
}

static void StreamOut_player_t(player_t *str)
{
    int i;

    // mobj_t *mo;
    StreamOutPtr(str->mo);

    // playerstate_t playerstate;
    StreamOutLong(str->playerstate);

    // ticcmd_t cmd;
    StreamOut_ticcmd_t(&str->cmd);

    // pclass_t class;
    StreamOutLong(str->class);

    // fixed_t viewz;
    StreamOutLong(str->viewz);

    // fixed_t viewheight;
    StreamOutLong(str->viewheight);

    // fixed_t deltaviewheight;
    StreamOutLong(str->deltaviewheight);

    // fixed_t bob;
    StreamOutLong(str->bob);

    // int flyheight;
    StreamOutLong(str->flyheight);

    // int lookdir;
    StreamOutLong(str->lookdir);

    // boolean centering;
    StreamOutLong(str->centering);

    // int health;
    StreamOutLong(str->health);

    // int armorpoints[NUMARMOR];
    for (i=0; i<NUMARMOR; ++i)
    {
        StreamOutLong(str->armorpoints[i]);
    }

    // inventory_t inventory[NUMINVENTORYSLOTS];
    for (i=0; i<NUMINVENTORYSLOTS; ++i)
    {
        StreamOut_inventory_t(&str->inventory[i]);
    }

    // artitype_t readyArtifact;
    StreamOutLong(str->readyArtifact);

    // int artifactCount;
    StreamOutLong(str->artifactCount);

    // int inventorySlotNum;
    StreamOutLong(str->inventorySlotNum);

    // int powers[NUMPOWERS];
    for (i=0; i<NUMPOWERS; ++i)
    {
        StreamOutLong(str->powers[i]);
    }

    // int keys;
    StreamOutLong(str->keys);

    // int pieces;
    StreamOutLong(str->pieces);

    // signed int frags[MAXPLAYERS];
    for (i=0; i<MAXPLAYERS; ++i)
    {
        StreamOutLong(str->frags[i]);
    }

    // weapontype_t readyweapon;
    StreamOutLong(str->readyweapon);

    // weapontype_t pendingweapon;
    StreamOutLong(str->pendingweapon);

    // boolean weaponowned[NUMWEAPONS];
    for (i=0; i<NUMWEAPONS; ++i)
    {
        StreamOutLong(str->weaponowned[i]);
    }

    // int mana[NUMMANA];
    for (i=0; i<NUMMANA; ++i)
    {
        StreamOutLong(str->mana[i]);
    }

    // int attackdown, usedown;
    StreamOutLong(str->attackdown);
    StreamOutLong(str->usedown);

    // int cheats;
    StreamOutLong(str->cheats);

    // int refire;
    StreamOutLong(str->refire);

    // int killcount, itemcount, secretcount;
    StreamOutLong(str->killcount);
    StreamOutLong(str->itemcount);
    StreamOutLong(str->secretcount);

    // char message[80];
    for (i=0; i<80; ++i)
    {
        StreamOutByte(str->message[i]);
    }

    // int messageTics;
    StreamOutLong(str->messageTics);

    // short ultimateMessage;
    StreamOutWord(str->ultimateMessage);

    // short yellowMessage;
    StreamOutWord(str->yellowMessage);

    // int damagecount, bonuscount;
    StreamOutLong(str->damagecount);
    StreamOutLong(str->bonuscount);

    // int poisoncount;
    StreamOutLong(str->poisoncount);

    // mobj_t *poisoner;
    StreamOutPtr(str->poisoner);

    // mobj_t *attacker;
    StreamOutPtr(str->attacker);

    // int extralight;
    StreamOutLong(str->extralight);

    // int fixedcolormap;
    StreamOutLong(str->fixedcolormap);

    // int colormap;
    StreamOutLong(str->colormap);

    // pspdef_t psprites[NUMPSPRITES];
    for (i=0; i<NUMPSPRITES; ++i)
    {
        StreamOut_pspdef_t(&str->psprites[i]);
    }

    // int morphTics;
    StreamOutLong(str->morphTics);

    // unsigned int jumpTics;
    StreamOutLong(str->jumpTics);

    // unsigned int worldTimer;
    StreamOutLong(str->worldTimer);
}


//
// thinker_t
//

static void StreamIn_thinker_t(thinker_t *str)
{
    // struct thinker_s *prev, *next;
    // Pointers are discarded:
    GET_LONG;
    str->prev = NULL;
    GET_LONG;
    str->next = NULL;

    // think_t function;
    // Function pointer is discarded:
    GET_LONG;
    str->function = NULL;
}

static void StreamOut_thinker_t(thinker_t *str)
{
    // struct thinker_s *prev, *next;
    StreamOutPtr(str->prev);
    StreamOutPtr(str->next);

    // think_t function;
    StreamOutPtr(&str->function);
}


//
// mobj_t
//

static void StreamInMobjSpecials(mobj_t *mobj)
{
    unsigned int special1, special2;

    special1 = GET_LONG;
    special2 = GET_LONG;

    mobj->special1.i = special1;
    mobj->special2.i = special2;

    switch (mobj->type)
    {
            // Just special1
        case MT_BISH_FX:
        case MT_HOLY_FX:
        case MT_DRAGON:
        case MT_THRUSTFLOOR_UP:
        case MT_THRUSTFLOOR_DOWN:
        case MT_MINOTAUR:
        case MT_SORCFX1:
            SetMobjPtr(&mobj->special1.m, special1);
            break;

            // Just special2
        case MT_LIGHTNING_FLOOR:
        case MT_LIGHTNING_ZAP:
            SetMobjPtr(&mobj->special2.m, special2);
            break;

            // Both special1 and special2
        case MT_HOLY_TAIL:
        case MT_LIGHTNING_CEILING:
            SetMobjPtr(&mobj->special1.m, special1);
            SetMobjPtr(&mobj->special2.m, special2);
            break;

        default:
            break;
    }
}

static void StreamIn_mobj_t(mobj_t *str)
{
    unsigned int i;

    // thinker_t thinker;
    StreamIn_thinker_t(&str->thinker);

    // fixed_t x, y, z;
    str->x = GET_LONG;
    str->y = GET_LONG;
    str->z = GET_LONG;

    // struct mobj_s *snext, *sprev;
    // Pointer values are discarded:
    GET_LONG;
    str->snext = NULL;
    GET_LONG;
    str->sprev = NULL;

    // angle_t angle;
    str->angle = GET_LONG;

    // spritenum_t sprite;
    str->sprite = GET_LONG;

    // int frame;
    str->frame = GET_LONG;

    // struct mobj_s *bnext, *bprev;
    // Values are read but discarded; this will be restored when the thing's
    // position is set.
    GET_LONG;
    str->bnext = NULL;
    GET_LONG;
    str->bprev = NULL;

    // struct subsector_s *subsector;
    // Read but discard: pointer will be restored when thing position is set.
    GET_LONG;
    str->subsector = NULL;

    // fixed_t floorz, ceilingz;
    str->floorz = GET_LONG;
    str->ceilingz = GET_LONG;

    // fixed_t floorpic;
    str->floorpic = GET_LONG;

    // fixed_t radius, height;
    str->radius = GET_LONG;
    str->height = GET_LONG;

    // fixed_t momx, momy, momz;
    str->momx = GET_LONG;
    str->momy = GET_LONG;
    str->momz = GET_LONG;

    // int validcount;
    str->validcount = GET_LONG;

    // mobjtype_t type;
    str->type = GET_LONG;

    // mobjinfo_t *info;
    // Pointer value is read but discarded.
    GET_LONG;
    str->info = NULL;

    // int tics;
    str->tics = GET_LONG;

    // state_t *state;
    // Restore as index into states table.
    i = GET_LONG;
    str->state = &states[i];

    // int damage;
    str->damage = GET_LONG;

    // int flags;
    str->flags = GET_LONG;

    // int flags2;
    str->flags2 = GET_LONG;

    // specialval_t special1;
    // specialval_t special2;
    // Read in special values: there are special cases to deal with with
    // mobj pointers.
    StreamInMobjSpecials(str);

    // int health;
    str->health = GET_LONG;

    // int movedir;
    str->movedir = GET_LONG;

    // int movecount;
    str->movecount = GET_LONG;

    // struct mobj_s *target;
    i = GET_LONG;
    SetMobjPtr(&str->target, i);

    // int reactiontime;
    str->reactiontime = GET_LONG;

    // int threshold;
    str->threshold = GET_LONG;

    // struct player_s *player;
    // Saved as player number.
    i = GET_LONG;
    if (i == 0)
    {
        str->player = NULL;
    }
    else
    {
        str->player = &players[i - 1];
        str->player->mo = str;
    }

    // int lastlook;
    str->lastlook = GET_LONG;

    // fixed_t floorclip;
    str->floorclip = GET_LONG;

    // int archiveNum;
    str->archiveNum = GET_LONG;

    // short tid;
    str->tid = GET_WORD;

    // byte special;
    str->special = GET_BYTE;

    // byte args[5];
    for (i=0; i<5; ++i)
    {
        str->args[i] = GET_BYTE;
    }
}

static void StreamOutMobjSpecials(mobj_t *mobj)
{
    unsigned int special1, special2;
    boolean corpse;
    
    corpse = (mobj->flags & MF_CORPSE) != 0;
    special1 = mobj->special1.i;
    special2 = mobj->special2.i;

    switch (mobj->type)
    {
            // Just special1
        case MT_BISH_FX:
        case MT_HOLY_FX:
        case MT_DRAGON:
        case MT_THRUSTFLOOR_UP:
        case MT_THRUSTFLOOR_DOWN:
        case MT_MINOTAUR:
        case MT_SORCFX1:
        case MT_MSTAFF_FX2:
            if (corpse)
            {
                special1 = MOBJ_NULL;
            }
            else
            {
                special1 = GetMobjNum(mobj->special1.m);
            }
            break;

            // Just special2
        case MT_LIGHTNING_FLOOR:
        case MT_LIGHTNING_ZAP:
            if (corpse)
            {
                special2 = MOBJ_NULL;
            }
            else
            {
                special2 = GetMobjNum(mobj->special2.m);
            }
            break;

            // Both special1 and special2
        case MT_HOLY_TAIL:
        case MT_LIGHTNING_CEILING:
            if (corpse)
            {
                special1 = MOBJ_NULL;
                special2 = MOBJ_NULL;
            }
            else
            {
                special1 = GetMobjNum(mobj->special1.m);
                special2 = GetMobjNum(mobj->special2.m);
            }
            break;

            // Miscellaneous
        case MT_KORAX:
            special1 = 0; // Searching index
            break;

        default:
            break;
    }

    // Write special values to savegame file.

    StreamOutLong(special1);
    StreamOutLong(special2);
}

static void StreamOut_mobj_t(mobj_t *str)
{
    int i;

    // thinker_t thinker;
    StreamOut_thinker_t(&str->thinker);

    // fixed_t x, y, z;
    StreamOutLong(str->x);
    StreamOutLong(str->y);
    StreamOutLong(str->z);

    // struct mobj_s *snext, *sprev;
    StreamOutPtr(str->snext);
    StreamOutPtr(str->sprev);

    // angle_t angle;
    StreamOutLong(str->angle);

    // spritenum_t sprite;
    StreamOutLong(str->sprite);

    // int frame;
    StreamOutLong(str->frame);

    // struct mobj_s *bnext, *bprev;
    StreamOutPtr(str->bnext);
    StreamOutPtr(str->bprev);

    // struct subsector_s *subsector;
    StreamOutPtr(str->subsector);

    // fixed_t floorz, ceilingz;
    StreamOutLong(str->floorz);
    StreamOutLong(str->ceilingz);

    // fixed_t floorpic;
    StreamOutLong(str->floorpic);

    // fixed_t radius, height;
    StreamOutLong(str->radius);
    StreamOutLong(str->height);

    // fixed_t momx, momy, momz;
    StreamOutLong(str->momx);
    StreamOutLong(str->momy);
    StreamOutLong(str->momz);

    // int validcount;
    StreamOutLong(str->validcount);

    // mobjtype_t type;
    StreamOutLong(str->type);

    // mobjinfo_t *info;
    StreamOutPtr(str->info);

    // int tics;
    StreamOutLong(str->tics);

    // state_t *state;
    // Save as index into the states table.
    StreamOutLong(str->state - states);

    // int damage;
    StreamOutLong(str->damage);

    // int flags;
    StreamOutLong(str->flags);

    // int flags2;
    StreamOutLong(str->flags2);

    // specialval_t special1;
    // specialval_t special2;
    // There are lots of special cases for the special values:
    StreamOutMobjSpecials(str);

    // int health;
    StreamOutLong(str->health);

    // int movedir;
    StreamOutLong(str->movedir);

    // int movecount;
    StreamOutLong(str->movecount);

    // struct mobj_s *target;
    if ((str->flags & MF_CORPSE) != 0)
    {
        StreamOutLong(MOBJ_NULL);
    }
    else
    {
        StreamOutLong(GetMobjNum(str->target));
    }

    // int reactiontime;
    StreamOutLong(str->reactiontime);

    // int threshold;
    StreamOutLong(str->threshold);

    // struct player_s *player;
    // Stored as index into players[] array, if there is a player pointer.
    if (str->player != NULL)
    {
        StreamOutLong(str->player - players + 1);
    }
    else
    {
        StreamOutLong(0);
    }

    // int lastlook;
    StreamOutLong(str->lastlook);

    // fixed_t floorclip;
    StreamOutLong(str->floorclip);

    // int archiveNum;
    StreamOutLong(str->archiveNum);

    // short tid;
    StreamOutWord(str->tid);

    // byte special;
    StreamOutByte(str->special);

    // byte args[5];
    for (i=0; i<5; ++i)
    {
        StreamOutByte(str->args[i]);
    }
}



//==========================================================================
//
// SV_SaveGame
//
//==========================================================================

void SV_SaveGame(int slot, char *description)
{
    char fileName[100];
    char versionText[HXS_VERSION_TEXT_LENGTH];
    unsigned int i;

    // Open the output file
    sprintf(fileName, "%shex6.hxs", SavePath);
    OpenStreamOut(fileName);

    // Write game save description
    StreamOutBuffer(description, HXS_DESCRIPTION_LENGTH);

    // Write version info
    memset(versionText, 0, HXS_VERSION_TEXT_LENGTH);
    strcpy(versionText, HXS_VERSION_TEXT);
    StreamOutBuffer(versionText, HXS_VERSION_TEXT_LENGTH);

    // Place a header marker
    StreamOutLong(ASEG_GAME_HEADER);

    // Write current map and difficulty
    StreamOutByte(gamemap);
    StreamOutByte(gameskill);

    // Write global script info
    for (i = 0; i < MAX_ACS_WORLD_VARS; ++i)
    {
        StreamOutLong(WorldVars[i]);
    }

    for (i = 0; i < MAX_ACS_STORE + 1; ++i)
    {
        StreamOut_acsstore_t(&ACSStore[i]);
    }

    ArchivePlayers();

    // Place a termination marker
    StreamOutLong(ASEG_END);

    // Close the output file
    CloseStreamOut();

    // Save out the current map
    SV_SaveMap(true);           // true = save player info

    // Clear all save files at destination slot
    ClearSaveSlot(slot);

    // Copy base slot to destination slot
    CopySaveSlot(BASE_SLOT, slot);
}

//==========================================================================
//
// SV_SaveMap
//
//==========================================================================

void SV_SaveMap(boolean savePlayers)
{
    char fileName[100];

    SavingPlayers = savePlayers;

    // Open the output file
    sprintf(fileName, "%shex6%02d.hxs", SavePath, gamemap);
    OpenStreamOut(fileName);

    // Place a header marker
    StreamOutLong(ASEG_MAP_HEADER);

    // Write the level timer
    StreamOutLong(leveltime);

    // Set the mobj archive numbers
    SetMobjArchiveNums();

    ArchiveWorld();
    ArchivePolyobjs();
    ArchiveMobjs();
    ArchiveThinkers();
    ArchiveScripts();
    ArchiveSounds();
    ArchiveMisc();

    // Place a termination marker
    StreamOutLong(ASEG_END);

    // Close the output file
    CloseStreamOut();
}

//==========================================================================
//
// SV_LoadGame
//
//==========================================================================

void SV_LoadGame(int slot)
{
    int i;
    char fileName[100];
    player_t playerBackup[MAXPLAYERS];
    mobj_t *mobj;

    // Copy all needed save files to the base slot
    if (slot != BASE_SLOT)
    {
        ClearSaveSlot(BASE_SLOT);
        CopySaveSlot(slot, BASE_SLOT);
    }

    // Create the name
    sprintf(fileName, "%shex6.hxs", SavePath);

    // Load the file
    M_ReadFile(fileName, &SaveBuffer);

    // Set the save pointer and skip the description field
    SavePtr.b = SaveBuffer + HXS_DESCRIPTION_LENGTH;

    // Check the version text
    if (strcmp((char *) SavePtr.b, HXS_VERSION_TEXT))
    {                           // Bad version
        return;
    }
    SavePtr.b += HXS_VERSION_TEXT_LENGTH;

    AssertSegment(ASEG_GAME_HEADER);

    gameepisode = 1;
    gamemap = GET_BYTE;
    gameskill = GET_BYTE;

    // Read global script info

    for (i = 0; i < MAX_ACS_WORLD_VARS; ++i)
    {
        WorldVars[i] = GET_LONG;
    }

    for (i = 0; i < MAX_ACS_STORE + 1; ++i)
    {
        StreamIn_acsstore_t(&ACSStore[i]);
    }

    // Read the player structures
    UnarchivePlayers();

    AssertSegment(ASEG_END);

    Z_Free(SaveBuffer);

    // Save player structs
    for (i = 0; i < MAXPLAYERS; i++)
    {
        playerBackup[i] = players[i];
    }

    // Load the current map
    SV_LoadMap();

    // Don't need the player mobj relocation info for load game
    Z_Free(TargetPlayerAddrs);

    // Restore player structs
    inv_ptr = 0;
    curpos = 0;
    for (i = 0; i < MAXPLAYERS; i++)
    {
        mobj = players[i].mo;
        players[i] = playerBackup[i];
        players[i].mo = mobj;
        if (i == consoleplayer)
        {
            players[i].readyArtifact = players[i].inventory[inv_ptr].type;
        }
    }
}

//==========================================================================
//
// SV_UpdateRebornSlot
//
// Copies the base slot to the reborn slot.
//
//==========================================================================

void SV_UpdateRebornSlot(void)
{
    ClearSaveSlot(REBORN_SLOT);
    CopySaveSlot(BASE_SLOT, REBORN_SLOT);
}

//==========================================================================
//
// SV_ClearRebornSlot
//
//==========================================================================

void SV_ClearRebornSlot(void)
{
    ClearSaveSlot(REBORN_SLOT);
}

//==========================================================================
//
// SV_MapTeleport
//
//==========================================================================

void SV_MapTeleport(int map, int position)
{
    int i;
    int j;
    char fileName[100];
    player_t playerBackup[MAXPLAYERS];
    mobj_t *targetPlayerMobj;
    mobj_t *mobj;
    int inventoryPtr;
    int currentInvPos;
    boolean rClass;
    boolean playerWasReborn;
    boolean oldWeaponowned[NUMWEAPONS];
    int oldKeys = 0;
    int oldPieces = 0;
    int bestWeapon;

    if (!deathmatch)
    {
        if (P_GetMapCluster(gamemap) == P_GetMapCluster(map))
        {                       // Same cluster - save map without saving player mobjs
            SV_SaveMap(false);
        }
        else
        {                       // Entering new cluster - clear base slot
            ClearSaveSlot(BASE_SLOT);
        }
    }

    // Store player structs for later
    rClass = randomclass;
    randomclass = false;
    for (i = 0; i < MAXPLAYERS; i++)
    {
        playerBackup[i] = players[i];
    }

    // Save some globals that get trashed during the load
    inventoryPtr = inv_ptr;
    currentInvPos = curpos;

    // Only SV_LoadMap() uses TargetPlayerAddrs, so it's NULLed here
    // for the following check (player mobj redirection)
    TargetPlayerAddrs = NULL;

    gamemap = map;
    sprintf(fileName, "%shex6%02d.hxs", SavePath, gamemap);
    if (!deathmatch && ExistingFile(fileName))
    {                           // Unarchive map
        SV_LoadMap();
    }
    else
    {                           // New map
        G_InitNew(gameskill, gameepisode, gamemap);

        // Destroy all freshly spawned players
        for (i = 0; i < MAXPLAYERS; i++)
        {
            if (playeringame[i])
            {
                P_RemoveMobj(players[i].mo);
            }
        }
    }

    // Restore player structs
    targetPlayerMobj = NULL;
    for (i = 0; i < MAXPLAYERS; i++)
    {
        if (!playeringame[i])
        {
            continue;
        }
        players[i] = playerBackup[i];
        P_ClearMessage(&players[i]);
        players[i].attacker = NULL;
        players[i].poisoner = NULL;

        if (netgame)
        {
            if (players[i].playerstate == PST_DEAD)
            {                   // In a network game, force all players to be alive
                players[i].playerstate = PST_REBORN;
            }
            if (!deathmatch)
            {                   // Cooperative net-play, retain keys and weapons
                oldKeys = players[i].keys;
                oldPieces = players[i].pieces;
                for (j = 0; j < NUMWEAPONS; j++)
                {
                    oldWeaponowned[j] = players[i].weaponowned[j];
                }
            }
        }
        playerWasReborn = (players[i].playerstate == PST_REBORN);
        if (deathmatch)
        {
            memset(players[i].frags, 0, sizeof(players[i].frags));
            mobj = P_SpawnMobj(playerstarts[0][i].x << 16,
                               playerstarts[0][i].y << 16, 0,
                               MT_PLAYER_FIGHTER);
            players[i].mo = mobj;
            G_DeathMatchSpawnPlayer(i);
            P_RemoveMobj(mobj);
        }
        else
        {
            P_SpawnPlayer(&playerstarts[position][i]);
        }

        if (playerWasReborn && netgame && !deathmatch)
        {                       // Restore keys and weapons when reborn in co-op
            players[i].keys = oldKeys;
            players[i].pieces = oldPieces;
            for (bestWeapon = 0, j = 0; j < NUMWEAPONS; j++)
            {
                if (oldWeaponowned[j])
                {
                    bestWeapon = j;
                    players[i].weaponowned[j] = true;
                }
            }
            players[i].mana[MANA_1] = 25;
            players[i].mana[MANA_2] = 25;
            if (bestWeapon)
            {                   // Bring up the best weapon
                players[i].pendingweapon = bestWeapon;
            }
        }

        if (targetPlayerMobj == NULL)
        {                       // The poor sap
            targetPlayerMobj = players[i].mo;
        }
    }
    randomclass = rClass;

    // Redirect anything targeting a player mobj
    if (TargetPlayerAddrs)
    {
        for (i = 0; i < TargetPlayerCount; i++)
        {
            *TargetPlayerAddrs[i] = targetPlayerMobj;
        }
        Z_Free(TargetPlayerAddrs);
    }

    // Destroy all things touching players
    for (i = 0; i < MAXPLAYERS; i++)
    {
        if (playeringame[i])
        {
            P_TeleportMove(players[i].mo, players[i].mo->x, players[i].mo->y);
        }
    }

    // Restore trashed globals
    inv_ptr = inventoryPtr;
    curpos = currentInvPos;

    // Launch waiting scripts
    if (!deathmatch)
    {
        P_CheckACSStore();
    }

    // For single play, save immediately into the reborn slot
    if (!netgame)
    {
        SV_SaveGame(REBORN_SLOT, REBORN_DESCRIPTION);
    }
}

//==========================================================================
//
// SV_GetRebornSlot
//
//==========================================================================

int SV_GetRebornSlot(void)
{
    return (REBORN_SLOT);
}

//==========================================================================
//
// SV_RebornSlotAvailable
//
// Returns true if the reborn slot is available.
//
//==========================================================================

boolean SV_RebornSlotAvailable(void)
{
    char fileName[100];

    sprintf(fileName, "%shex%d.hxs", SavePath, REBORN_SLOT);
    return ExistingFile(fileName);
}

//==========================================================================
//
// SV_LoadMap
//
//==========================================================================

void SV_LoadMap(void)
{
    char fileName[100];

    // Load a base level
    G_InitNew(gameskill, gameepisode, gamemap);

    // Remove all thinkers
    RemoveAllThinkers();

    // Create the name
    sprintf(fileName, "%shex6%02d.hxs", SavePath, gamemap);

    // Load the file
    M_ReadFile(fileName, &SaveBuffer);
    SavePtr.b = SaveBuffer;

    AssertSegment(ASEG_MAP_HEADER);

    // Read the level timer
    leveltime = GET_LONG;

    UnarchiveWorld();
    UnarchivePolyobjs();
    UnarchiveMobjs();
    UnarchiveThinkers();
    UnarchiveScripts();
    UnarchiveSounds();
    UnarchiveMisc();

    AssertSegment(ASEG_END);

    // Free mobj list and save buffer
    Z_Free(MobjList);
    Z_Free(SaveBuffer);
}

//==========================================================================
//
// SV_InitBaseSlot
//
//==========================================================================

void SV_InitBaseSlot(void)
{
    ClearSaveSlot(BASE_SLOT);
}

//==========================================================================
//
// ArchivePlayers
//
//==========================================================================

static void ArchivePlayers(void)
{
    int i;

    StreamOutLong(ASEG_PLAYERS);
    for (i = 0; i < MAXPLAYERS; i++)
    {
        StreamOutByte(playeringame[i]);
    }
    for (i = 0; i < MAXPLAYERS; i++)
    {
        if (!playeringame[i])
        {
            continue;
        }
        StreamOutByte(PlayerClass[i]);
        StreamOut_player_t(&players[i]);
    }
}

//==========================================================================
//
// UnarchivePlayers
//
//==========================================================================

static void UnarchivePlayers(void)
{
    int i;

    AssertSegment(ASEG_PLAYERS);
    for (i = 0; i < MAXPLAYERS; i++)
    {
        playeringame[i] = GET_BYTE;
    }
    for (i = 0; i < MAXPLAYERS; i++)
    {
        if (!playeringame[i])
        {
            continue;
        }
        PlayerClass[i] = GET_BYTE;
        StreamIn_player_t(&players[i]);
        P_ClearMessage(&players[i]);
    }
}

//==========================================================================
//
// ArchiveWorld
//
//==========================================================================

static void ArchiveWorld(void)
{
    int i;
    int j;
    sector_t *sec;
    line_t *li;
    side_t *si;

    StreamOutLong(ASEG_WORLD);
    for (i = 0, sec = sectors; i < numsectors; i++, sec++)
    {
        StreamOutWord(sec->floorheight >> FRACBITS);
        StreamOutWord(sec->ceilingheight >> FRACBITS);
        StreamOutWord(sec->floorpic);
        StreamOutWord(sec->ceilingpic);
        StreamOutWord(sec->lightlevel);
        StreamOutWord(sec->special);
        StreamOutWord(sec->tag);
        StreamOutWord(sec->seqType);
    }
    for (i = 0, li = lines; i < numlines; i++, li++)
    {
        StreamOutWord(li->flags);
        StreamOutByte(li->special);
        StreamOutByte(li->arg1);
        StreamOutByte(li->arg2);
        StreamOutByte(li->arg3);
        StreamOutByte(li->arg4);
        StreamOutByte(li->arg5);
        for (j = 0; j < 2; j++)
        {
            if (li->sidenum[j] == -1)
            {
                continue;
            }
            si = &sides[li->sidenum[j]];
            StreamOutWord(si->textureoffset >> FRACBITS);
            StreamOutWord(si->rowoffset >> FRACBITS);
            StreamOutWord(si->toptexture);
            StreamOutWord(si->bottomtexture);
            StreamOutWord(si->midtexture);
        }
    }
}

//==========================================================================
//
// UnarchiveWorld
//
//==========================================================================

static void UnarchiveWorld(void)
{
    int i;
    int j;
    sector_t *sec;
    line_t *li;
    side_t *si;

    AssertSegment(ASEG_WORLD);
    for (i = 0, sec = sectors; i < numsectors; i++, sec++)
    {
        sec->floorheight = GET_WORD << FRACBITS;
        sec->ceilingheight = GET_WORD << FRACBITS;
        sec->floorpic = GET_WORD;
        sec->ceilingpic = GET_WORD;
        sec->lightlevel = GET_WORD;
        sec->special = GET_WORD;
        sec->tag = GET_WORD;
        sec->seqType = GET_WORD;
        sec->specialdata = 0;
        sec->soundtarget = 0;
    }
    for (i = 0, li = lines; i < numlines; i++, li++)
    {
        li->flags = GET_WORD;
        li->special = GET_BYTE;
        li->arg1 = GET_BYTE;
        li->arg2 = GET_BYTE;
        li->arg3 = GET_BYTE;
        li->arg4 = GET_BYTE;
        li->arg5 = GET_BYTE;
        for (j = 0; j < 2; j++)
        {
            if (li->sidenum[j] == -1)
            {
                continue;
            }
            si = &sides[li->sidenum[j]];
            si->textureoffset = GET_WORD << FRACBITS;
            si->rowoffset = GET_WORD << FRACBITS;
            si->toptexture = GET_WORD;
            si->bottomtexture = GET_WORD;
            si->midtexture = GET_WORD;
        }
    }
}

//==========================================================================
//
// SetMobjArchiveNums
//
// Sets the archive numbers in all mobj structs.  Also sets the MobjCount
// global.  Ignores player mobjs if SavingPlayers is false.
//
//==========================================================================

static void SetMobjArchiveNums(void)
{
    mobj_t *mobj;
    thinker_t *thinker;

    MobjCount = 0;
    for (thinker = thinkercap.next; thinker != &thinkercap;
         thinker = thinker->next)
    {
        if (thinker->function == P_MobjThinker)
        {
            mobj = (mobj_t *) thinker;
            if (mobj->player && !SavingPlayers)
            {                   // Skipping player mobjs
                continue;
            }
            mobj->archiveNum = MobjCount++;
        }
    }
}

//==========================================================================
//
// ArchiveMobjs
//
//==========================================================================

static void ArchiveMobjs(void)
{
    int count;
    thinker_t *thinker;

    StreamOutLong(ASEG_MOBJS);
    StreamOutLong(MobjCount);
    count = 0;
    for (thinker = thinkercap.next; thinker != &thinkercap;
         thinker = thinker->next)
    {
        if (thinker->function != P_MobjThinker)
        {                       // Not a mobj thinker
            continue;
        }
        if (((mobj_t *) thinker)->player && !SavingPlayers)
        {                       // Skipping player mobjs
            continue;
        }
        count++;
        StreamOut_mobj_t((mobj_t *) thinker);
    }
    if (count != MobjCount)
    {
        I_Error("ArchiveMobjs: bad mobj count");
    }
}

//==========================================================================
//
// UnarchiveMobjs
//
//==========================================================================

static void UnarchiveMobjs(void)
{
    int i;
    mobj_t *mobj;

    AssertSegment(ASEG_MOBJS);
    TargetPlayerAddrs = Z_Malloc(MAX_TARGET_PLAYERS * sizeof(mobj_t **),
                                 PU_STATIC, NULL);
    TargetPlayerCount = 0;
    MobjCount = GET_LONG;
    MobjList = Z_Malloc(MobjCount * sizeof(mobj_t *), PU_STATIC, NULL);
    for (i = 0; i < MobjCount; i++)
    {
        MobjList[i] = Z_Malloc(sizeof(mobj_t), PU_LEVEL, NULL);
    }
    for (i = 0; i < MobjCount; i++)
    {
        mobj = MobjList[i];
        StreamIn_mobj_t(mobj);

        // Restore broken pointers.
        mobj->info = &mobjinfo[mobj->type];
        P_SetThingPosition(mobj);
        mobj->floorz = mobj->subsector->sector->floorheight;
        mobj->ceilingz = mobj->subsector->sector->ceilingheight;

        mobj->thinker.function = P_MobjThinker;
        P_AddThinker(&mobj->thinker);
    }
    P_CreateTIDList();
    P_InitCreatureCorpseQueue(true);    // true = scan for corpses
}

//==========================================================================
//
// GetMobjNum
//
//==========================================================================

static int GetMobjNum(mobj_t * mobj)
{
    if (mobj == NULL)
    {
        return MOBJ_NULL;
    }
    if (mobj->player && !SavingPlayers)
    {
        return MOBJ_XX_PLAYER;
    }
    return mobj->archiveNum;
}

//==========================================================================
//
// SetMobjPtr
//
//==========================================================================

static void SetMobjPtr(mobj_t **ptr, unsigned int archiveNum)
{
    if (archiveNum == MOBJ_NULL)
    {
        *ptr = NULL;
    }
    else if (archiveNum == MOBJ_XX_PLAYER)
    {
        if (TargetPlayerCount == MAX_TARGET_PLAYERS)
        {
            I_Error("RestoreMobj: exceeded MAX_TARGET_PLAYERS");
        }
        TargetPlayerAddrs[TargetPlayerCount++] = ptr;
        *ptr = NULL;
    }
    else
    {
        *ptr = MobjList[archiveNum];
    }
}

//==========================================================================
//
// ArchiveThinkers
//
//==========================================================================

static void ArchiveThinkers(void)
{
    thinker_t *thinker;
    thinkInfo_t *info;
    byte buffer[MAX_THINKER_SIZE];

    StreamOutLong(ASEG_THINKERS);
    for (thinker = thinkercap.next; thinker != &thinkercap;
         thinker = thinker->next)
    {
        for (info = ThinkerInfo; info->tClass != TC_NULL; info++)
        {
            if (thinker->function == info->thinkerFunc)
            {
                StreamOutByte(info->tClass);
                memcpy(buffer, thinker, info->size);
                if (info->mangleFunc)
                {
                    info->mangleFunc(buffer);
                }
                StreamOutBuffer(buffer, info->size);
                break;
            }
        }
    }
    // Add a termination marker
    StreamOutByte(TC_NULL);
}

//==========================================================================
//
// UnarchiveThinkers
//
//==========================================================================

static void UnarchiveThinkers(void)
{
    int tClass;
    thinker_t *thinker;
    thinkInfo_t *info;

    AssertSegment(ASEG_THINKERS);
    while ((tClass = GET_BYTE) != TC_NULL)
    {
        for (info = ThinkerInfo; info->tClass != TC_NULL; info++)
        {
            if (tClass == info->tClass)
            {
                thinker = Z_Malloc(info->size, PU_LEVEL, NULL);
                memcpy(thinker, SavePtr.b, info->size);
                SavePtr.b += info->size;
                thinker->function = info->thinkerFunc;
                if (info->restoreFunc)
                {
                    info->restoreFunc(thinker);
                }
                P_AddThinker(thinker);
                break;
            }
        }
        if (info->tClass == TC_NULL)
        {
            I_Error("UnarchiveThinkers: Unknown tClass %d in "
                    "savegame", tClass);
        }
    }
}

//==========================================================================
//
// MangleSSThinker
//
//==========================================================================

static void MangleSSThinker(ssthinker_t * sst)
{
    sst->sector = (sector_t *) (sst->sector - sectors);
}

//==========================================================================
//
// RestoreSSThinker
//
//==========================================================================

static void RestoreSSThinker(ssthinker_t * sst)
{
    sst->sector = &sectors[(int) sst->sector];
    sst->sector->specialdata = sst->thinker.function;
}

//==========================================================================
//
// RestoreSSThinkerNoSD
//
//==========================================================================

static void RestoreSSThinkerNoSD(ssthinker_t * sst)
{
    sst->sector = &sectors[(int) sst->sector];
}

//==========================================================================
//
// MangleScript
//
//==========================================================================

static void MangleScript(acs_t * script)
{
    script->ip = (int *) ((int) (script->ip) - (int) ActionCodeBase);
    script->line = script->line ?
        (line_t *) (script->line - lines) : (line_t *) - 1;
    script->activator = (mobj_t *) GetMobjNum(script->activator);
}

//==========================================================================
//
// RestoreScript
//
//==========================================================================

static void RestoreScript(acs_t * script)
{
    script->ip = (int *) (ActionCodeBase + (int) script->ip);
    if ((int) script->line == -1)
    {
        script->line = NULL;
    }
    else
    {
        script->line = &lines[(int) script->line];
    }
    SetMobjPtr(&script->activator, (int) script->activator);
}

//==========================================================================
//
// RestorePlatRaise
//
//==========================================================================

static void RestorePlatRaise(plat_t * plat)
{
    plat->sector = &sectors[(int) plat->sector];
    plat->sector->specialdata = T_PlatRaise;
    P_AddActivePlat(plat);
}

//==========================================================================
//
// RestoreMoveCeiling
//
//==========================================================================

static void RestoreMoveCeiling(ceiling_t * ceiling)
{
    ceiling->sector = &sectors[(int) ceiling->sector];
    ceiling->sector->specialdata = T_MoveCeiling;
    P_AddActiveCeiling(ceiling);
}

//==========================================================================
//
// ArchiveScripts
//
//==========================================================================

static void ArchiveScripts(void)
{
    int i;

    StreamOutLong(ASEG_SCRIPTS);
    for (i = 0; i < ACScriptCount; i++)
    {
        StreamOutWord(ACSInfo[i].state);
        StreamOutWord(ACSInfo[i].waitValue);
    }

    for (i = 0; i< MAX_ACS_MAP_VARS; ++i)
    {
        StreamOutLong(MapVars[i]);
    }
}

//==========================================================================
//
// UnarchiveScripts
//
//==========================================================================

static void UnarchiveScripts(void)
{
    int i;

    AssertSegment(ASEG_SCRIPTS);
    for (i = 0; i < ACScriptCount; i++)
    {
        ACSInfo[i].state = GET_WORD;
        ACSInfo[i].waitValue = GET_WORD;
    }

    for (i = 0; i < MAX_ACS_MAP_VARS; ++i)
    {
        MapVars[i] = GET_LONG;
    }
}

//==========================================================================
//
// ArchiveMisc
//
//==========================================================================

static void ArchiveMisc(void)
{
    int ix;

    StreamOutLong(ASEG_MISC);
    for (ix = 0; ix < MAXPLAYERS; ix++)
    {
        StreamOutLong(localQuakeHappening[ix]);
    }
}

//==========================================================================
//
// UnarchiveMisc
//
//==========================================================================

static void UnarchiveMisc(void)
{
    int ix;

    AssertSegment(ASEG_MISC);
    for (ix = 0; ix < MAXPLAYERS; ix++)
    {
        localQuakeHappening[ix] = GET_LONG;
    }
}

//==========================================================================
//
// RemoveAllThinkers
//
//==========================================================================

static void RemoveAllThinkers(void)
{
    thinker_t *thinker;
    thinker_t *nextThinker;

    thinker = thinkercap.next;
    while (thinker != &thinkercap)
    {
        nextThinker = thinker->next;
        if (thinker->function == P_MobjThinker)
        {
            P_RemoveMobj((mobj_t *) thinker);
        }
        else
        {
            Z_Free(thinker);
        }
        thinker = nextThinker;
    }
    P_InitThinkers();
}

//==========================================================================
//
// ArchiveSounds
//
//==========================================================================

static void ArchiveSounds(void)
{
    seqnode_t *node;
    sector_t *sec;
    int difference;
    int i;

    StreamOutLong(ASEG_SOUNDS);

    // Save the sound sequences
    StreamOutLong(ActiveSequences);
    for (node = SequenceListHead; node; node = node->next)
    {
        StreamOutLong(node->sequence);
        StreamOutLong(node->delayTics);
        StreamOutLong(node->volume);
        StreamOutLong(SN_GetSequenceOffset(node->sequence,
                                           node->sequencePtr));
        StreamOutLong(node->currentSoundID);
        for (i = 0; i < po_NumPolyobjs; i++)
        {
            if (node->mobj == (mobj_t *) & polyobjs[i].startSpot)
            {
                break;
            }
        }
        if (i == po_NumPolyobjs)
        {                       // Sound is attached to a sector, not a polyobj
            sec = R_PointInSubsector(node->mobj->x, node->mobj->y)->sector;
            difference = (int) ((byte *) sec
                                - (byte *) & sectors[0]) / sizeof(sector_t);
            StreamOutLong(0);   // 0 -- sector sound origin
        }
        else
        {
            StreamOutLong(1);   // 1 -- polyobj sound origin
            difference = i;
        }
        StreamOutLong(difference);
    }
}

//==========================================================================
//
// UnarchiveSounds
//
//==========================================================================

static void UnarchiveSounds(void)
{
    int i;
    int numSequences;
    int sequence;
    int delayTics;
    int volume;
    int seqOffset;
    int soundID;
    int polySnd;
    int secNum;
    mobj_t *sndMobj;

    AssertSegment(ASEG_SOUNDS);

    // Reload and restart all sound sequences
    numSequences = GET_LONG;
    i = 0;
    while (i < numSequences)
    {
        sequence = GET_LONG;
        delayTics = GET_LONG;
        volume = GET_LONG;
        seqOffset = GET_LONG;

        soundID = GET_LONG;
        polySnd = GET_LONG;
        secNum = GET_LONG;
        if (!polySnd)
        {
            sndMobj = (mobj_t *) & sectors[secNum].soundorg;
        }
        else
        {
            sndMobj = (mobj_t *) & polyobjs[secNum].startSpot;
        }
        SN_StartSequence(sndMobj, sequence);
        SN_ChangeNodeData(i, seqOffset, delayTics, volume, soundID);
        i++;
    }
}

//==========================================================================
//
// ArchivePolyobjs
//
//==========================================================================

static void ArchivePolyobjs(void)
{
    int i;

    StreamOutLong(ASEG_POLYOBJS);
    StreamOutLong(po_NumPolyobjs);
    for (i = 0; i < po_NumPolyobjs; i++)
    {
        StreamOutLong(polyobjs[i].tag);
        StreamOutLong(polyobjs[i].angle);
        StreamOutLong(polyobjs[i].startSpot.x);
        StreamOutLong(polyobjs[i].startSpot.y);
    }
}

//==========================================================================
//
// UnarchivePolyobjs
//
//==========================================================================

static void UnarchivePolyobjs(void)
{
    int i;
    fixed_t deltaX;
    fixed_t deltaY;

    AssertSegment(ASEG_POLYOBJS);
    if (GET_LONG != po_NumPolyobjs)
    {
        I_Error("UnarchivePolyobjs: Bad polyobj count");
    }
    for (i = 0; i < po_NumPolyobjs; i++)
    {
        if (GET_LONG != polyobjs[i].tag)
        {
            I_Error("UnarchivePolyobjs: Invalid polyobj tag");
        }
        PO_RotatePolyobj(polyobjs[i].tag, (angle_t) GET_LONG);
        deltaX = GET_LONG - polyobjs[i].startSpot.x;
        deltaY = GET_LONG - polyobjs[i].startSpot.y;
        PO_MovePolyobj(polyobjs[i].tag, deltaX, deltaY);
    }
}

//==========================================================================
//
// AssertSegment
//
//==========================================================================

static void AssertSegment(gameArchiveSegment_t segType)
{
    if (GET_LONG != segType)
    {
        I_Error("Corrupt save game: Segment [%d] failed alignment check",
                segType);
    }
}

//==========================================================================
//
// ClearSaveSlot
//
// Deletes all save game files associated with a slot number.
//
//==========================================================================

static void ClearSaveSlot(int slot)
{
    int i;
    char fileName[100];

    for (i = 0; i < MAX_MAPS; i++)
    {
        sprintf(fileName, "%shex%d%02d.hxs", SavePath, slot, i);
        remove(fileName);
    }
    sprintf(fileName, "%shex%d.hxs", SavePath, slot);
    remove(fileName);
}

//==========================================================================
//
// CopySaveSlot
//
// Copies all the save game files from one slot to another.
//
//==========================================================================

static void CopySaveSlot(int sourceSlot, int destSlot)
{
    int i;
    char sourceName[100];
    char destName[100];

    for (i = 0; i < MAX_MAPS; i++)
    {
        sprintf(sourceName, "%shex%d%02d.hxs", SavePath, sourceSlot, i);
        if (ExistingFile(sourceName))
        {
            sprintf(destName, "%shex%d%02d.hxs", SavePath, destSlot, i);
            CopyFile(sourceName, destName);
        }
    }
    sprintf(sourceName, "%shex%d.hxs", SavePath, sourceSlot);
    if (ExistingFile(sourceName))
    {
        sprintf(destName, "%shex%d.hxs", SavePath, destSlot);
        CopyFile(sourceName, destName);
    }
}

//==========================================================================
//
// CopyFile
//
//==========================================================================

static void CopyFile(char *sourceName, char *destName)
{
    int length;
    byte *buffer;

    length = M_ReadFile(sourceName, &buffer);
    M_WriteFile(destName, buffer, length);
    Z_Free(buffer);
}

//==========================================================================
//
// ExistingFile
//
//==========================================================================

static boolean ExistingFile(char *name)
{
    FILE *fp;

    if ((fp = fopen(name, "rb")) != NULL)
    {
        fclose(fp);
        return true;
    }
    else
    {
        return false;
    }
}

//==========================================================================
//
// OpenStreamOut
//
//==========================================================================

static void OpenStreamOut(char *fileName)
{
    SavingFP = fopen(fileName, "wb");
}

//==========================================================================
//
// CloseStreamOut
//
//==========================================================================

static void CloseStreamOut(void)
{
    if (SavingFP)
    {
        fclose(SavingFP);
    }
}

//==========================================================================
//
// StreamOutBuffer
//
//==========================================================================

static void StreamOutBuffer(void *buffer, int size)
{
    fwrite(buffer, size, 1, SavingFP);
}

//==========================================================================
//
// StreamOutByte
//
//==========================================================================

static void StreamOutByte(byte val)
{
    fwrite(&val, sizeof(byte), 1, SavingFP);
}

//==========================================================================
//
// StreamOutWord
//
//==========================================================================

static void StreamOutWord(unsigned short val)
{
    val = SHORT(val);
    fwrite(&val, sizeof(unsigned short), 1, SavingFP);
}

//==========================================================================
//
// StreamOutLong
//
//==========================================================================

static void StreamOutLong(unsigned int val)
{
    val = LONG(val);
    fwrite(&val, sizeof(int), 1, SavingFP);
}

//==========================================================================
//
// StreamOutPtr
//
//==========================================================================

static void StreamOutPtr(void *val)
{
    long ptr;

    // Write a pointer value. In Vanilla Hexen pointers are 32-bit but
    // nowadays they might be larger. Whatever value we write here isn't
    // going to be much use when we reload the game.

    ptr = (long) val;
    StreamOutLong((unsigned int) (ptr & 0xffffffff));
}


