# One-screen UI inventory

This list covers every screen, field window and lower-screen mode in the game code. For each one it says whether the one-screen layer (`engine/ui`) redraws it.

Sources:
- the aletheia tree (`src/game`, paths below are relative to it);
- the recipes in `engine/ui/recipe_*.c`;
- the older code survey in [`unified-view-inventory.md`](unified-view-inventory.md), which is the place for screen-by-screen layer details. Some file names in it are older than the aletheia file renames of ae14c2cc42 (for example `br_pokesearch` is now `ov185/br_musicallook_proc.c`).

## Coverage key

| Mark | Meaning |
|---|---|
| **yes** (recipe) | A recipe redraws the screen with its own interface. |
| **part** (recipe) | A recipe redraws some states; the rest fall back. |
| **pixels** (recipe) | The recipe keeps the game's own DS pixels (the field's 2D layers over the world). It works, but it is not redrawn. |
| **sheet** | `field-lower` shows the game's DS lower screen as a picture over the world (`recipe_field.c` `lower_sheet`). |
| **lower-only** | The last-resort recipe: the DS lower screen, scaled, used when the upper screen is blank. |
| **none** | No recipe: the two DS screens. |

Priority:
- **P1**: a normal player sees it in the main story or in common play.
- **P2**: optional or side content that a normal player can reach offline.
- **P3**: needs a link partner or online service, or is rare, debug-only or unused.

## Summary: what to build next

The P1 entries that are not covered (**none**, **sheet**, **lower-only** or **pixels**), roughly in the order a player meets them:

1. Field text windows: the talk balloon, the plain/system/info windows, the money window, item get, the place-name banner (section 2). Today they are the DS's own pixels.
2. Pokedex search (ov210), the catch registration and nickname screen (ov207).
3. Town map / Fly (ov072).
4. Move relearner (ov173), the Pokemon selection for the move tutor and the daycare (ov091 in its other modes, see the note there).
5. The Bag in its "give item" and "pick a berry/item for a script" modes, if `bag` does not match them (check `recipe_bag.c` matching against `ItemMenuArgs` mode).
6. Egg hatching (ov215), Hall of Fame (ov180), staff roll (ov181), The End (ov182), the save screen after the credits (ov205).
7. Mail (ov136), Easy Chat (ov110), Trainer card signature (ov111 back side, `card` covers the front rows).
8. C-Gear itself (mode 0/8 is drawn as the eidos bar plus a `lower_sheet`), C-Gear help (ov124), C-Gear power on/off (mode 9), dowsing (mode 6).
9. Musical: dress-up (ov133), the show (ov132), photo (ov112).
10. Union Room lower screen (mode 2), Entralink lower screen (mode 3), the Entralink missions (ov071).
11. In-game trade (ov119), the Pokemon Center PC's other boxes modes (ov170 is covered; check the deposit/withdraw modes the PC menu opens).
12. Battle Subway map (ov224), Subway records (ov069), Battle Subway/Trial House award screens (ov220).

## 1. Screens and apps (procs)

The "Reached from" column gives the code that starts the proc.

### 1.1 Start menu (X menu) apps

| Screen | Where | Reached from | Covered | P |
|---|---|---|---|---|
| X menu itself | `ov021/field_menu.c`, sub-screen mode 1 | X | **yes** (`field-menu`) | P1 |
| Pokedex: splash, list, entry | `ov212/zukan.c:222` `ZUKAN_Create`; children `ov211` splash, `ov209/zukan_list.c:794`, `ov208/zukan_detail_*.c` | X menu, `ov010/event_field_proclink.c:565` | **yes** (`pokedex`) | P1 |
| Pokedex search | `ov210/zknsearch_sys.c:73` | Pokedex, `ov212/zukan.c:377` | **none** (the recipe falls back on purpose, `recipe_dex.c` header) | P1 |
| Pokedex entry: area map | `ov208/zukan_detail_area.c:741` (loads ov072 town map) | Pokedex entry, AREA page | check: `pokedex` reads the page id; the area page uses the town map resources | P1 |
| Pokemon (party list) | `ov091/pokelist.c`, `ov091/plist_menu.c:254` | X menu, `event_field_proclink.c:388` | **yes** (`party list`) | P1 |
| Summary | `ov131/p_status.c` | party list, PC, many others (`event_poke_status.c`) | **yes** (`pokemon summary`) | P1 |
| Bag | `ov070/itemmenu.c:2353` `ITEMMENU_Init` | X menu, `event_field_proclink.c:724`; scripts `ov010/scrcmd_proc.c:1857` `SCRCMD_PROC_CallBagScreen`; PC `ov170/box2_main.c:1100` | **yes** (`bag`) for the field mode; check the script and PC modes | P1 |
| Trainer card | `ov010/trcard_sys.c:259`, screen `ov111/viewer_main.c:483` | X menu, `event_field_proclink.c:971` | **yes** (`card`) | P1 |
| Trainer card signature / back | `ov111/viewer_*.c` | card, flip | check: `card` covers the rows and the badge case | P2 |
| Save | `ov034/report.c:252` (sub-screen mode 7) + report event `ov010/report_event.c` | X menu | **yes** (`save`) | P1 |
| Options | `ov068/option_menu.c:474` | X menu, `event_field_proclink.c:934` | **yes** (`options`) | P1 |
| Y shortcut menu | `ov021/shortcut_menu.c`, `ov010/event_shortcut_menu.c` | Y | **none** (check: it draws on the upper screen, so `field` may show it as pixels) | P1 |
| Town map / Fly | `ov072/townmap.c:357` `TOWNMAP_Create` | Fly (party menu, Y shortcut, HM), `event_field_proclink.c:868` | **none** | P1 |

### 1.2 Field story and NPC screens

| Screen | Where | Reached from | Covered | P |
|---|---|---|---|---|
| Starter choice | `ov223/psel.c:387` | script, `scrcmd_proc.c:2147` | **yes** (`starter`) | P1 |
| Name entry (player, rival, nickname, box) | `ov194/namein_graphic_main.c:183` | new game (`ov088/startmenu.c:2866`), scripts (`ov021/event_namein.c:106`, `ov021/scrcmd_sub_proc_events.c:127/164`), catch (`ov092/event_battle_return.c:404`), PC box name (`ov170/box2_main.c:3093`) | **yes** (`name entry`) | P1 |
| Catch: Pokedex registration and nickname | `ov207/zukan_toroku_graphic.c`, `ov207/zukan_nickname.c:179` | after a catch, `ov092/event_battle_return.c:392`; after a Dream World PDC catch `ov227/event_pdc_return.c:236` | **none** | P1 |
| Evolution | `ov195/shinka_demo.c:489` | after battle, level/trade/stone, `event_field_proclink.c:1082` | **yes** (`evolution`, and its party list for move learning) | P1 |
| Egg hatching | `ov215/egg_demo_main.c`, `egg_demo_graphic.c:219` | walking with an egg, `ov010/symbol_map.c:452` | **none** | P1 |
| Move relearner | `ov173/poke_summary_main.c:348` (`gMoveRelearnProcData`) | Mistralton move relearner, `scrcmd_proc.c:1966` `SCRCMD_PROC_CallMoveRelearn` (move list from `ov010/waza_oshie.c`) | **none** | P1 |
| Move tutor | same proc as the relearner (`ov173`), different move list (scripts) | tutors in Driftveil, Nacrene and the others | **none** | P2 |
| Party select for scripts (daycare, tutor, trade, gift checks) | `ov091` in a script mode; `ov021/party_select_list.c:214` for the two-party list | scripts, `scrcmd_proc.c:2348` daycare | **part** (`party list` if it matches the mode; check) | P1 |
| In-game NPC trade | `ov119/pokemontrade_proc.c:3355` + demos `ov117`, `ov118` | `ov021/fld_trade.c:193` and scripts | **none** | P1 |
| Pokemon Mart (buy) | `ov021/scrcmd_proc_fld.c:1022` `SCRCMD_PROC_FLD_StepShop` | Mart clerk | **yes** (`shop`) | P1 |
| Mart (sell) | the Bag in sell mode (`ov070`, `ITEMMENU_TaskSell*`) | Mart clerk, SELL | **yes** (`bag` handles the sell tasks) | P1 |
| PC boxes | `ov170/box2_main.c:443` | PC, `scrcmd_proc_fld.c:2000` | **yes** (`pc`) | P1 |
| PC menu (whose PC, deposit/withdraw/move) | field script list | PC | **yes** (`field` script list) | P1 |
| Hall of Fame viewer (PC) | `ov171/dendou_view.c:374` | PC, Professor's PC, `scrcmd_sub_proc_events.c:226` | **none** | P2 |
| Mail | `ov136/mailsys.c:246`, `mailview.c` | Bag (read), give mail, `event_field_proclink.c:999` | **none** | P2 |
| Easy Chat (word entry) | `ov110/pms_input.c:317` | interviews, mail, greetings, `scrcmd_proc.c:2151` | **none** | P2 |
| Xtransceiver story calls | `ov174/local_tvt_chara.c:228` (`gLocalTvtSysProcData`) | story calls, `scrcmd_proc.c:2077` | **none** | P1 |
| Battle Subway map | `ov224/subway_map_graphic.c:226` | script mode 2, `scrcmd_proc.c:2137` | **none** | P2 |
| Subway / Institute record list | `ov069/bsubway_record_list.c:390` | Gear Station, `fld_btl_inst_tool.c` | **none** | P2 |
| Regional / National Dex awards | `ov221/chihou_zukan_award.c:247`, `ov222/zenkoku_zukan_award.c:256` | professor after completing a Dex, `scrcmd_proc.c:2126/2132` | **none** | P2 |
| Trial House / Institute rank award | `ov220/th_award.c:261` | after a Trial House run, `fld_btl_inst_tool.c:1401` | **none** | P2 |
| Musical: dress-up | `ov133/dup_fitting.c:812` | Musical theatre, `ov010/musical_dressup_sys.c:92` | **none** | P2 |
| Musical: show | `ov132/sta_acting.c:761` | Musical, `ov010/scrcmd_musical.c:1091` | **none** | P2 |
| Musical: photo and prop case | `ov112/mus_shot_info.c:344`, `musical_shot_sys.c` | Musical | **none** | P2 |
| Entralink (Monolith): title, missions, Pass Powers | `ov071/monolith_tool.c`, `monolith_mission_select.c:730`, `monolith_power.c` | Entralink forest centre, `scrcmd_proc.c:2014` `SCRCMD_PROC_CallPlayerProfile` | **none** | P2 |
| Pass Power activation | `ov083/gpower_event.c:159` | Entralink, C-Gear | **none** (field event text) | P2 |
| Symbol encounter picker | `ov218/un_select.c:622` | Entralink symbol map event, `ov010/symbol_map.c:952` | **none** | P2 |
| Dream World PDC (Game Sync catch) | `ov098/pdc.c:98` | Entralink forest, `ov010/scrcmd_symbol.c:615` | **none** | P3 |
| Game manual (help) | `ov225/manual_graphic.c:204` | in-game guide, `ov021/event_game_manual.c:56`, `scrcmd_proc_fld.c:2134` | **none** | P2 |
| Value / mode screens of `ov037` | `ov037/gimmick_init_helpers.c` | `scrcmd_proc.c:2245..2297` | no screen (gimmick state), skip | n/a |

### 1.3 C-Gear apps and link features

| Screen | Where | Reached from | Covered | P |
|---|---|---|---|---|
| C-Gear panel | `ov029/c_gear.c:2345` (modes 0 and 8) | the field lower screen once obtained | **part** (`field`: own bar; the panel itself as `lower_sheet` behind the C-GEAR button) | P1 |
| No-gear panel | `ov030/no_gear.c:159` (mode 5) | before the C-Gear | **yes** (`field`) | P1 |
| C-Gear power on/off prompt | `ov035/c_gear_power_onoff.c:195` (mode 9) | power button on the C-Gear | **sheet** | P2 |
| C-Gear help | `ov124/cg_help.c:631` | C-Gear "?" | **none** | P2 |
| Wireless (Entralink comm view) | `ov122/intrude_comm_view.c:1034` | C-Gear Wireless, `ov010/event_wifi_proc.c:237` | **yes** (`wireless`) | P3 |
| IR: menu | `ov021/ircbattlemenu.c` | C-Gear IR | **lower-only** | P3 |
| IR: battle match | `ov100/ircbattlematch.c:1569` | IR menu, `ov010/event_ircbattle.c:292` | **lower-only** | P3 |
| IR: Feeling Check (compatibility) | `ov101/compatible_irc_sys.c:143`, menu `ov102`, touch test `ov103`, rhythm `ov104`, result `ov105`, ranking `ov106` | IR menu, `event_ircbattle.c:506` | **none** | P3 |
| IR / Wireless trade | `ov119/pokemontrade_proc.c` + IR demo `ov117` | IR menu, Union Room | **none** | P3 |
| Xtransceiver (live calls) | `ov172/comm_tvt_sys.c` and `ctvt_*.c` | C-Gear Wireless / IR, `event_wifi_proc.c:251` | **part** (`xtransceiver`: the call list; the talk/camera screens are **lower-only** or **none**) | P3 |
| Survey Radar | `ov217/research_top.c`, `research_list.c`, `research_graph.c` | C-Gear, `ov010/event_research_radar.c:234` | **none** | P2 |
| Entralink lower screen (Intrude) | `ov018/intrude_subdisp.c` (mode 3) | Entralink / visiting another world | **sheet** | P2 |
| Union Room lower screen (chat) | `ov031/union_subdisp.c:303` (mode 2) | Union Room | **sheet** | P3 |
| Union Room apps: card exchange, drawing, spinner | card `ov111` via `ov012/union_subproc.c:384`; drawing `ov023/oekaki.c:670`; spinner `ov137/guru2.c:130` | Union Room menu | **none** (card: check if `card` matches the comm mode) | P3 |
| Union / Colosseum party and summary | `ov090` + `ov091`, `ov131` | link battle setup, `union_subproc.c:765` | **part** (`party list`, `pokemon summary` if they match the link modes) | P3 |
| Wi-Fi Club | `ov099/wifi_club.c:7559` + 2D map `ov127`, `ov129` | Pokemon Center 2F, `ov006/event_wificlub.c` | **none** | P3 |
| Wi-Fi login and errors | `ov115/wifilogin_proc.c`, `wifilogin_disp.c` | any Wi-Fi feature, `arm9/net_err_proc.c` | **none** | P3 |
| GTS | `ov135/worldtrade.c:430` | Pokemon Center 2F, `ov004/event_worldtrade.c` | **none** | P3 |
| GTS Negotiations | `ov120/gtsnego_disp.c:1576` + trade `ov119` | Pokemon Center 2F, `scrcmd_proc.c:1908` | **none** | P3 |
| Geonet / trainer detail | `ov219/beacon_detail.c:585` | Pokemon Center 2F globe, `scrcmd_proc_fld.c:2099` | **none** | P2 |
| Beacon detail (pass-by info) | `ov216/beacon_detail_gra.c:635`, from beacon view (mode 4) | `ov010/beacon_view_event.c:46` | **none**; mode 4 **sheet** (reach in BW not confirmed) | P3 |
| Game Sync / Dream World | `ov123/gsync_*.c`, `pdwacc_*.c` | Pokemon Center 2F, `ov000/event_gsync.c` | **none** | P3 |
| Wi-Fi Battle Subway | `ov116/wifi_bsubway.c:276` | Gear Station | **none** | P3 |
| Random Matchup / Wi-Fi battle match | `ov175/wifibattlematch_core.c`, `ov177/wifibattlematch_graphic.c:163`, `ov199` | Pokemon Center 2F, `scrcmd_proc.c:2182` | **none** | P3 |
| Battle Competition (live) | `ov176/battle_championship_*.c`, `ov177` | title menu (`ov088/startmenu.c:1532`) and the PC | **none** | P3 |
| Battle Video (VS Recorder) | `ov186/br_*.c`, `ov183`, `ov184`, `ov185`, `ov187/br_main.c:172` | C-Gear / PC, `arm9/battle_rec.c` | **none** | P2 (local videos), P3 (online) |
| Save a battle video | `ov214/btl_rec_sel.c:476` | after a link or Subway battle, `ov008/event_battle_rec_save.c:412` | **none** | P2 |
| Link battle intro (VS screen) | `ov213/comm_btl_demo_graphic.c:167` | before a link battle | **none** | P3 |
| Multi-battle partner list | `ov097/vs_multi_list.c:332` | link multi battle | **none** | P3 |
| Mystery Gift | `ov121/mystery.c:598` | title menu, `ov088/startmenu.c:1525` | **none** | P2 |
| Poke Transfer (Pal Park style lab) | `ov107/mb_*.c` | Poke Transfer Lab, `ov021/scrcmd_palpark.c:96`; title menu download | **none** | P3 |
| Nintendo WFC settings | `ov016/*` (scene*.c) | title menu, `ov088/startmenu.c:1538` | **none** | P3 |

### 1.4 Boot, title and endings

| Screen | Where | Reached from | Covered | P |
|---|---|---|---|---|
| Logos, copyright, opening | `ov088/startmenu.c`, opening `ov179/cdemo_main.c:75` | boot | **yes** (`boot`) | P1 |
| Title screen | `ov088/startmenu.c` | boot | **yes** (`title`) | P1 |
| Title menu (Continue, New Game, Mystery Gift and the others) | `ov088/startmenu.c` | title | **yes** (`title_menu`) | P1 |
| Microphone test | `ov168/mictest.c:395` | title menu | **none** | P3 |
| New-game intro (professor) | `ov204/intro_cmd.c:851` | New Game | **yes** (`boot`, shown big; the menu choices are game pixels) | P1 |
| Save-data problem screens | `ov228/save_outside.c`, `ov088/save_control_intr.c:68` | boot with a damaged save | **none** | P3 |
| Hall of Fame entry | `ov180/hall_of_fame_proc.c:59` | after the Champion, `ov010/event_gameclear.c:240` | **none** | P1 |
| Staff roll | `ov181/staff_roll.c:391` | after the Hall of Fame, `event_gameclear.c:248` | **none** | P1 |
| The End | `ov182/the_end_graphic.c:208` | after the staff roll, `event_gameclear.c:275` | **none** | P1 |
| Save after the credits | `ov205/save_screen.c:260` | `event_gameclear.c:264` | **none** | P1 |
| 3D story demos (Royal Unova cruise and others) | `ov203/demo3d.c:144` | scripts, `event_field_proclink.c:1539` | **none** | P1 |
| Network error screen | `ov017/error_screen.c` | Wi-Fi errors (`ov009/dwc_rapcommon.c:169`) | **none** | P3 |
| Power-off message | `arm9/app_power.c` | lid / power events | **none** | P3 |
| Halt screen | `ov109/idle_screen.c:81` `IDLE_SCREEN_Run` (never returns; no caller in the tree) | not reached | skip | P3 |

### 1.5 Battle

| Screen | Where | Covered | P |
|---|---|---|---|
| Messages and animations | `ov093/btlv_core.c`, `btlv_scu.c` | **yes** (`battle_plain`) | P1 |
| FIGHT/POKEMON/BAG/RUN | `ov094/btlv_input.c` | **yes** (`battle_action`) | P1 |
| Moves | `ov094/btlv_input.c` | **yes** (`battle_move`) | P1 |
| Target (double/triple) | `ov094/btlv_input.c` | **yes** (`battle_target`) | P1 |
| Yes/no and other choices | `ov094/btlv_input.c` | **yes** (`battle_choice`) | P1 |
| Battle Bag | `ov197/b_bag_main.c:1317` | **yes** (`battle_bag`) | P1 |
| Battle party list | `ov198/b_plist_draw.c:2361` | **yes** (`battle_party`) | P1 |
| Rotation battle input | `ov094/btlv_input.c` rotation states | check: the action recipe may not cover the rotate choice | P2 |
| Triple battle shift | `ov094/btlv_input.c` | check | P2 |
| Timer (link battles) | `ov094/btlv_timer.c` | **none** | P3 |
| Battle recorder playback controls | `ov094/btlv_input.c` playback mode | **none** | P2 |
| DSi-only battle overlay | `ov093/btl_dsi_overlay.c:42` (ov140) | not on the DS cartridges' DS mode; skip | P3 |

## 2. Field windows and overlays on the upper screen

The window system is `ov021/field_msgbg.c`. The script commands are in `ov021/scrcmd_msgwin.c`.

Today all of these come through the `field` recipe as the game's own pixels: the DS's 2D layers drawn over the world (`view->hide_2d` is only set while a captured background shows). The exception is the script list and the yes/no menu, which `field_choices` redraws.

| Element | Where | Shown by | Covered | P |
|---|---|---|---|---|
| Talk balloon (3D speech bubble with tail) | `ov021/talkmsgwin.c:177` `TALKMSGWIN_Create`; `field_msgbg.c:1362` `FIELD_MSGBG_AddTalkWin`; balloon placement `scrcmd_msgwin.c` `SCRCMD_MSGWIN_CalcBalloonOffset` | NPC talk (`SCRCMD_MSGWIN_CmdBalloonWinMsg*`) | **pixels** (`field`; a click is A) | P1 |
| Plain window (no tail) | `field_msgbg.c:1492` `FIELD_MSGBG_CreatePlainWin`; `talkmsgwin.c` `TALKMSGWIN_SetupPlainWindow` | signs, narration (`SCRCMD_MSGWIN_OpenPlainWinMsg`) | **pixels** | P1 |
| System window (bottom box) | `field_msgbg.c:1146` `CreateSysWin`, `:1217` `CreateBottomSysWin` | system messages, item use | **pixels** | P1 |
| Message window (classic box) | `field_msgbg.c:735` `CreateMsgWin`, `:779` `CreateBottomMsgWin` | field messages outside scripts | **pixels** | P1 |
| Info window (top box) | `field_msgbg.c:789` `CreateInfoMsgWin`, `:1234` `CreateInfoSysWin`; `SCRCMD_MSGWIN_OpenInfoSysWin` | info lines over the field | **pixels** | P1 |
| Sub windows (extra small texts) | `field_msgbg.c:1616` `SetupSubMsgWin`; `SCRCMD_MSGWIN_CmdAddSubWinMsg` | scripts that show several texts at once | **pixels** | P2 |
| BG window (full-width banner text) | `field_msgbg.c:1735` `CreateBgWin`; `SCRCMD_MSGWIN_CmdBgWinMsg` | cutscene text, notices | **pixels** | P1 |
| SP window (special frame) | `field_msgbg.c:2053` `CreateSpWin`; `SCRCMD_MSGWIN_CmdSpWinMsg` | special notices (TV, PA style) | **pixels** | P2 |
| Script list menu | `field_msgbg.c:876` `CreateMenuFunc`; `SCRCMD_MSGWIN_CmdStartMenu` | NPC choices, PC menu, Mart menu | **yes** (`field` `field_choices`) | P1 |
| Field yes/no | `field_msgbg.c:1057` `CreateYesNoMenu`; `SCRCMD_MSGWIN_CmdYesNoWin` | questions | **yes** (`field` `field_choices`) | P1 |
| Money window | `SCRCMD_MSGWIN_CmdOpenMoneyWin` (`scrcmd_msgwin.c`); the Mart's own `scrcmd_proc_fld.c:1422` | Mart, paid services, game corner style NPCs | **pixels** (the Mart's is redrawn by `shop`) | P1 |
| Time icon (wait spinner) | `arm9/time_icon.c` `TIME_ICON_Create`; `SCRCMD_MSGWIN_CmdSetTimeIcon` | saving, linking | **pixels** | P1 |
| Message cursor (next arrow) | `field_msgbg.c:2145` `CreateCursor` | every key-wait message | **pixels** | P1 |
| Place-name banner | `ov021/field_place_name.c:280` (letters `place_name_letter.c`); forced by `ov021/scrcmd_screeneffects.c:643` | entering a new zone | **pixels** (the eidos bar slides away while it shows) | P1 |
| Item get (fanfare + message) | `ov021/scrcmd_trainer_audio_item.c` with a system/plain window | picking up items, gifts | **pixels** | P1 |
| Poke icon window | `ov021/scrcmd_misc.c:1654` `SCRCMD_ShowPokeIconWindow` | scripts that show a Pokemon icon (daycare, gifts) | **pixels** | P2 |
| Info window (zone info) | `ov021/scrcmd_misc.c:1904` `SCRCMD_ShowOrClearInfoWindow` | scripts | **pixels** | P2 |
| Season change card | `ov020/event_mapchange.c` `EVENT_MAPCHANGE_StartSeasonEffect` and the season effect events | the first map load of a new season | **pixels** | P1 |
| Trainer face-up (gym leader cut-in) | `ov082/fld_faceup.c:157` | gym leader and rival talks | **pixels** | P1 |
| Encounter name cut-in | `ov073/enceff_name_cutin.c`, `ov074/fld3d_ci_name.c` | special trainer encounters | **pixels** (the `field` encounter path) | P1 |
| HM and legendary cut-ins | `ov021/fld3d_ci.c` | Surf, Fly, legendaries | **pixels** | P1 |
| Dream World postman list | `ov021/pdw_postman.c:196` | Pokemon Center postman | **pixels** | P3 |
| Comm entry menu (link member list) | `ov021/comm_entry_menu.c:585` | link battle and musical rooms | **pixels** | P3 |
| Royal Unova daily layout | `ov036/daily_layout.c` | ferry; no window of its own (it deals the boat's cells) | n/a | n/a |
| Entralink scoreboard lamps | `ov020/el_scoreboard.c:257` | Entralink; 3D models, not a window | n/a | n/a |

Not found as a window of its own in BW:
- a Chatot (PERAP) record window: Chatot's voice is only save data and battle network code (`arm9/perapvoice.c`);
- a step-milestone message: no such field window exists. Walking counters only feed eggs, Pass Powers and friendship.

## 3. Field lower-screen modes

The mode table is `d_021D872C` in `ov021/field_subscreen.c`: 0x1C-byte rows of init/update/updateNoTransition/checkRequest/exit. `FIELD_SUBSCREEN_ResolveMode` (`field_subscreen.c`) turns mode 0 into mode 5 when the save has no C-Gear or the zone has none. Ten modes exist (0 to 9); 0xB is only the "no previous mode" value given to the first init. There is no mode 0xA.

| Mode | Init | Shows | Covered | P |
|---|---|---|---|---|
| 0 C-Gear | `field_subscreen.c:503` `FIELD_SUBSCREEN_InitCGear` → `ov029/c_gear.c` | the C-Gear hex panel, Wireless/IR/Entralink buttons, battery, time | **part** (`field`: own bar, the panel as `lower_sheet` behind C-GEAR) | P1 |
| 1 Menu | `:629` `InitMenu` → `ov021/field_menu.c` | the X menu buttons | **yes** (`field-menu`) | P1 |
| 2 Union Room | `:727` `InitUnion` → `ov031/union_subdisp.c:303` | the chat log, the scroll knob, the app buttons | **sheet** | P3 |
| 3 Entralink (Intrude) | `:759` `InitIntrude` → `ov018/intrude_subdisp.c` | the Entralink town map, the connected players, missions | **sheet** | P2 |
| 4 Beacon view | `:796` `InitBeaconView` → `ov032/beacon_view.c:2082` | pass-by trainer list, details (ov216) | **sheet** (reach in BW not confirmed) | P3 |
| 5 No C-Gear | `:597` `InitNoGear` → `ov030/no_gear.c:159` | the plain panel before the C-Gear | **yes** (`field`) | P1 |
| 6 Dowsing | `:819` `InitDowsing` → `ov033/dowsing.c:306` | the Dowsing MCHN radar | **sheet** | P1 |
| 7 Report | `:839` `InitReport` → `ov034/report.c:252` | the save report (party, badges, time) | **yes** (`save` for the X menu save; a script save in mode 7 falls to `field`) | P1 |
| 8 C-Gear with callback | `:519` `InitCGearWithCallback` | the C-Gear, opened with its intro animation (first time or after power on) | **part** (as mode 0) | P1 |
| 9 C-Gear power on/off | `:901` `InitPowerOnoff` → `ov035/c_gear_power_onoff.c:195` | the power on/off question | **sheet** | P2 |

## 4. Shared widgets that many screens use

If these are redrawn once, many screens benefit:

- `ov067/yesno_menu.c`: the touch yes/no used by the summary, evolution, PC and others (the `evolution` recipe already reads it);
- `ov067/touchbar.c`: the bottom bar with back/close buttons used by ov208, ov210, ov216, ov225 and others;
- `ov067/frame_list.c:158`: the scrolling frame list (ov218, Wi-Fi Club, and others);
- `arm9/app_taskmenu.c`: the touch task menu (party list, PC actions);
- `arm9/bmp_menulist.c`, `arm9/bmp_menu.c`: the classic list and menu;
- `arm9/infowin.c`: the info bar with the clock, battery and wireless signal (C-Gear, link screens).

## 5. Second pass

Items found after the first version, or settled by re-reading the recipes:

| Element | Where | Covered | P |
|---|---|---|---|
| Battle ability announcement (the "name plate" that pops up with a Pokemon's Ability) | `ov093/btlv_scu.c:2794` `BTLV_SCU_StartNamePlate` and the `*NamePlate*` functions after it | check: `recipe_battle.c` does not mention it, so it is probably DS pixels under `battle_plain` | P1 |
| Battle entry scenes (wild appear, trainer intro, send-out) | `ov093/btlv_scu.c` `BTLV_SCU_ProcEntry*`, `BTLV_SCU_Phase*` | `battle_plain` (messages); check the trainer intro text | P1 |
| Battle HP and EXP gauges, level-up stat panel, party ball window | `ov094/btlv_gauge.c`, `ov093/btlv_scu.c:3234` ball window | **yes** (`battle_*` plates, `recipe_battle.c` level-up panel) | P1 |
| PC: Tuck in (Dream World), the game's own box list, wallpaper and marking panels | `ov170/box2_*.c` | **none**: `pc` shows the DS screens for these on purpose (`recipe_pc.c` header) | P2 |
| Shop paying in BP (Battle Subway exchange) | `scrcmd_proc_fld.c` `SCRCMD_PROC_FLD_GetShopCurrency` | **yes** (`shop` reads the BP flag) | P2 |
| Summary "forget a move" (level up, Rare Candy, TM) | `ov131/p_sta_skill.c` | **yes** (`party list` and `pokemon summary` handle the skill mode) | P1 |
| Key items with a screen of their own: Prop Case, Pal Pad | not located in this pass (the Bag's key item dispatch, `ov010/field_skill_check.c` field link table, is not named) | check | P2 |

## Notes

- "check" means I could not settle the question from reading. The recipe may match the screen in that state, or it may not.
- The PC, Bag and party list open in several modes from scripts (give item, choose a Pokemon for a script, daycare, tutor). Whether the existing recipes match all of those modes is the first thing to test. They are P1 because the story uses them.
- Wi-Fi services that depended on Nintendo's servers (GTS, Global Link, Game Sync, Random Matchup) still have reachable screens up to their connection step. P3 marks them all; the order among them follows whatever eidos's link play brings back.
