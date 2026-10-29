#include "port.h"
#include "dsmem.h"
#include "port_test.h"

extern void *g_ProcSys, *svsys, *_pOverlayTables, *_pTouchPanelWork;
extern struct BwUiState {
    void *context;
    int (*bounds)(void *, void *);
} gUiState;

static int bounds(void *lower, void *upper)
{
    return lower == upper;
}

int main(void)
{
    int proc, save, overlays, ui, touch;
    bw_port_reset();
    g_ProcSys = &proc;
    svsys = &save;
    _pOverlayTables = &overlays;
    gUiState.context = &ui;
    gUiState.bounds = bounds;
    _pTouchPanelWork = &touch;
    BW_REQUIRE("process pointer retains all bits", g_ProcSys == &proc);
    BW_REQUIRE("save pointer retains all bits", svsys == &save);
    BW_REQUIRE("overlay pointer independent of UI", _pOverlayTables == &overlays);
    BW_REQUIRE("UI pointer independent of neighbors", gUiState.context == &ui);
    BW_REQUIRE("UI callback independent of touch pointer", gUiState.bounds == bounds);
    BW_REQUIRE("process DS address binds native cell", bw_ds_ptr(0x02146990u) == &g_ProcSys);
    BW_REQUIRE("UI DS address binds native record", bw_ds_ptr(0x021469BCu) == &gUiState);
    BW_REQUIRE("touch DS address binds native cell", bw_ds_ptr(0x021469C4u) == &_pTouchPanelWork);
    BW_CHECK_EQ("reverse pointer mapping", bw_ds_addr_of(&svsys), 0x02146994u);
    bw_port_reset();
    BW_REQUIRE("restart clears process and save", !g_ProcSys && !svsys);
    BW_REQUIRE("restart clears UI and callback", !gUiState.context && !gUiState.bounds);
    BW_REQUIRE("restart clears overlay and touch", !_pOverlayTables && !_pTouchPanelWork);
    BW_REPORT("test_globals_native");
}
