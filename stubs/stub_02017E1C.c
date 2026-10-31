/*
 * POKE_TOOL_GetPartyValue (func_02017E1C) -- read one field of a Pokemon record by field id.
 *
 * This one IS matched (src/matched/unit_02017E1C.c), but it is deliberately
 * NOT in the port library's source list: its closure pulls in the record's
 * encryption layer (func_02018D10 / func_02018D6C) and the two big field
 * dispatchers, and building a valid encrypted record is a milestone of its
 * own.  See docs/building.md, "What is substituted, and why".
 *
 * Until then the port ships this stub, and tests/test_stats.c supplies
 * its own definition, which displaces this archive member at link time.
 */
#include "stubs.h"

BW_STUB(POKE_TOOL_GetPartyValue, 0x02017E1C, "Pokemon record getter: matched, but its closure needs the record encryption layer")
