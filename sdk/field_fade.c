/*
 * sdk/field_fade.c -- how the field changes maps: the "Map transitions"
 * setting (EIDOS_OPT_TRANSITIONS) as the overlay patches read it.
 *
 *   0 original  the game's own timing.
 *   1 fast      the door cross-fade steps four alpha levels a frame, not
 *               one (stubs/ov/patch_ov021_event_fieldmap_control.c); the
 *               new field builds in one frame, not one step a frame
 *               (stubs/ov/patch_ov021_fieldmap.c); the camera is not
 *               waited for while the screen is black
 *               (stubs/ov/patch_ov021_field_ground_anime.c).
 *   2 seamless  fast, and the screen never goes black: every fade to or
 *               from black becomes the door cross-fade.
 */

/* The port's settings (engine/platform/settings.h), spelled out: this file
 * is on the overlay link list, which does not see the platform headers. */
extern int eidos_settings_get(int id);
#define OPT_TRANSITIONS 16      /* settings.h's EIDOS_OPT_TRANSITIONS */

int eidos_field_transition(void)
{
    return eidos_settings_get(OPT_TRANSITIONS);
}

int eidos_door_fade_step(void)
{
    return eidos_field_transition() >= 1 ? 4 : 1;
}
