/*
 * engine/ui/recipes.c -- the groups of recipes, in priority order
 * (engine/ui/ui.h, "Recipes").  A scene none matches falls back to the two
 * DS screens side by side.  Apps and battle come before the field: the
 * field's own state is still live under an app.
 */

#include "ui.h"

extern const EidosUiRecipe *const ui_recipes_settings[];
extern const u32 ui_recipes_settings_count;
extern const EidosUiRecipe *const ui_recipes_title[];
extern const u32 ui_recipes_title_count;
extern const EidosUiRecipe *const ui_recipes_mystery[];
extern const u32 ui_recipes_mystery_count;
extern const EidosUiRecipe *const ui_recipes_battle[];
extern const u32 ui_recipes_battle_count;
extern const EidosUiRecipe *const ui_recipes_bag[];
extern const u32 ui_recipes_bag_count;
extern const EidosUiRecipe *const ui_recipes_shop[];
extern const u32 ui_recipes_shop_count;
extern const EidosUiRecipe *const ui_recipes_dex[];
extern const u32 ui_recipes_dex_count;
extern const EidosUiRecipe *const ui_recipes_party[];
extern const u32 ui_recipes_party_count;
extern const EidosUiRecipe *const ui_recipes_menus[];
extern const u32 ui_recipes_menus_count;
extern const EidosUiRecipe *const ui_recipes_pc[];
extern const u32 ui_recipes_pc_count;
extern const EidosUiRecipe *const ui_recipes_input[];
extern const u32 ui_recipes_input_count;
extern const EidosUiRecipe *const ui_recipes_starter[];
extern const u32 ui_recipes_starter_count;
extern const EidosUiRecipe *const ui_recipes_field[];
extern const u32 ui_recipes_field_count;
extern const EidosUiRecipe *const ui_recipes_boot[];
extern const u32 ui_recipes_boot_count;
extern const EidosUiRecipe *const ui_recipes_lower[];
extern const u32 ui_recipes_lower_count;

const EidosUiRecipeGroup eidos_ui_groups[] = {
    { ui_recipes_settings, &ui_recipes_settings_count },
    /* the name entry runs over the app that opened it (the end of a
     * battle, the boxes, an Egg hatching, ...): before those apps */
    { ui_recipes_input, &ui_recipes_input_count },
    { ui_recipes_title, &ui_recipes_title_count },
    { ui_recipes_mystery, &ui_recipes_mystery_count },
    { ui_recipes_battle, &ui_recipes_battle_count },
    { ui_recipes_bag, &ui_recipes_bag_count },
    { ui_recipes_shop, &ui_recipes_shop_count },
    { ui_recipes_dex, &ui_recipes_dex_count },
    { ui_recipes_party, &ui_recipes_party_count },
    { ui_recipes_menus, &ui_recipes_menus_count },
    { ui_recipes_pc, &ui_recipes_pc_count },
    { ui_recipes_starter, &ui_recipes_starter_count },
    { ui_recipes_field, &ui_recipes_field_count },
    { ui_recipes_boot, &ui_recipes_boot_count },
    { ui_recipes_lower, &ui_recipes_lower_count },   /* last: anything else */
};

const u32 eidos_ui_group_count = sizeof(eidos_ui_groups) / sizeof(eidos_ui_groups[0]);
