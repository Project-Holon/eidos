/*
 * patches: src/game/ov131/p_status.c P_STATUS_DeleteObjResource
 *
 * The summary shows the types mods add (sdk/type_icons.c): its pages give an
 * added type a character of its own (bw_type_icon_char, patches of
 * p_sta_info.c and p_sta_skill.c), drawn with the colours of the summary's
 * added types' row.  When the summary frees its resources it frees those
 * characters too and leaves that row.  The rest is the game's own.
 *
 * symbols:
 *   CLACT_ReleaseCharacter 0x0204A8D4 arm
 *   CLACT_ReleasePalette 0x0204ADA4 arm
 *   CLACT_ReleaseCellAnim 0x0204AFD8 arm
 *   P_STA_SUB_Exit 0x021D6808 thumb ov131
 *   P_STA_INFO_FreeResources 0x021D7BF0 thumb ov131
 *   P_STA_RIBBON_FreeArcGraphics 0x021D8904 thumb ov131
 *   P_STA_SKILL_Exit 0x021D9A04 thumb ov131
 *
 * The game's own record
 * ---------------------
 * PStatusWork (src/game/ov131/p_status_types.h), as far as this reads it:
 * subWork 0x88, infoWork 0x8C, skillWork 0x90, ribbonWork 0x94, objRes 0x98
 * (38: palettes 0..11, characters 12..24, cells 25..37), typeChrRes 0x130.
 */

typedef unsigned char u8;
typedef unsigned int u32;

#define PSTATUS_RES_PLTT_START 0
#define PSTATUS_RES_PLTT_END 11
#define PSTATUS_RES_CHR_START 12
#define PSTATUS_RES_CHR_END 24
#define PSTATUS_RES_CELL_START 25
#define PSTATUS_RES_CELL_END 37
#define PSTATUS_TYPE_MAX 17

typedef struct PStatusWork {
    u8 pad_000[0x88];
    void *subWork;                      /* 0x088 */
    void *infoWork;                     /* 0x08C */
    void *skillWork;                    /* 0x090 */
    void *ribbonWork;                   /* 0x094 */
    u32 objRes[38];                     /* 0x098 */
    u32 typeChrRes[PSTATUS_TYPE_MAX];   /* 0x130 */
} PStatusWork;

typedef char pstatus_type_chr_at_0x130[sizeof(void *) != 4 || __builtin_offsetof(PStatusWork, typeChrRes) == 0x130 ? 1 : -1];

/* sdk/type_icons.h */
extern void bw_type_icon_chars_release(const void *owner);
extern void bw_type_screen_pop(const void *owner);

void CLACT_ReleaseCharacter(u32 idx);
void CLACT_ReleasePalette(u32 idx);
void CLACT_ReleaseCellAnim(u32 idx);
void P_STA_SUB_Exit(PStatusWork *wk, void *work);
void P_STA_INFO_FreeResources(PStatusWork *wk, void *work);
void P_STA_RIBBON_FreeArcGraphics(PStatusWork *wk, void *work);
void P_STA_SKILL_Exit(PStatusWork *wk, void *work);

void P_STATUS_DeleteObjResource(PStatusWork *wk)
{
    u8 i;
    P_STA_RIBBON_FreeArcGraphics(wk,wk->ribbonWork);
    P_STA_SKILL_Exit(wk,wk->skillWork);
    P_STA_INFO_FreeResources(wk,wk->infoWork);
    P_STA_SUB_Exit(wk,wk->subWork);
    for(i=PSTATUS_RES_PLTT_START;i<=PSTATUS_RES_PLTT_END;i++) CLACT_ReleasePalette(wk->objRes[i]);
    for(i=PSTATUS_RES_CHR_START;i<=PSTATUS_RES_CHR_END;i++) CLACT_ReleaseCharacter(wk->objRes[i]);
    for(i=PSTATUS_RES_CELL_START;i<=PSTATUS_RES_CELL_END;i++) CLACT_ReleaseCellAnim(wk->objRes[i]);
    for(i=0;i<PSTATUS_TYPE_MAX;i++) CLACT_ReleaseCharacter(wk->typeChrRes[i]);
    bw_type_icon_chars_release(wk);
    bw_type_screen_pop(wk);
}
