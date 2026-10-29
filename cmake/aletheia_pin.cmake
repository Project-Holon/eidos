#
# cmake/aletheia_pin.cmake -- warn when the aletheia checkout is not the
# revision the port is pinned to (docs/aletheia-pin.md).
#
# eidos compiles aletheia's matched C, its layout/ manifests and its tools/
# scripts out of the checkout at ALETHEIA_ROOT (CMakeLists.txt, "Where the
# aletheia decompilation checkout lives").  That checkout is edited by its own
# lanes, so the port's build is only reproducible against one revision of it.
# The pin below is that revision.
#
# This file reports, at configure time, when the checkout is not the pin:
# either its HEAD is another commit, or its tracked files have uncommitted
# changes.  Building against a newer checkout is a legitimate choice, so the
# report is a warning and never an error, and the build proceeds.  The warning
# names the pin and the checkout, so a rename or a split in an aletheia lane
# shows up as a deliberate update rather than a surprise.
#
# Moving the pin is a deliberate edit to this file: change the hash, say why
# in docs/aletheia-pin.md, and rebuild.  tools/pinned_checkout.py reads the
# same name to export the revision into a clean copy; the pin here is the one
# source of truth for both.
#
# Included from CMakeLists.txt through cmake/dsaddr.cmake, which is always
# included; the guard below makes a second include a no-op.
#

set(EIDOS_ALETHEIA_PIN "6c2aae879f123bcbc028cf6a8b7f8df614402ac8")

if(EIDOS_ALETHEIA_PIN_CHECKED)
    return()
endif()
set(EIDOS_ALETHEIA_PIN_CHECKED TRUE)

# tools/pinned_checkout.py exports the pin with `git archive`, so the copy is
# a plain tree with no .git and rev-parse below cannot read it.  The tool
# writes the exported revision to .eidos-aletheia-rev at the copy's root; read
# that instead.  A copy is the pin by construction, so there is nothing to
# compare but the stamp.
if(EXISTS "${ALETHEIA_ROOT}/.eidos-aletheia-rev")
    string(SUBSTRING "${EIDOS_ALETHEIA_PIN}" 0 10 _pin_short)
    file(READ "${ALETHEIA_ROOT}/.eidos-aletheia-rev" _pin_export)
    string(STRIP "${_pin_export}" _pin_export)
    if(_pin_export STREQUAL EIDOS_ALETHEIA_PIN)
        message(STATUS
            "aletheia pin: ${_pin_short} at ${ALETHEIA_ROOT} "
            "(clean export, matches docs/aletheia-pin.md)")
    else()
        string(SUBSTRING "${_pin_export}" 0 10 _pin_export_short)
        message(WARNING
            "aletheia pin: the clean export at ${ALETHEIA_ROOT} is "
            "${_pin_export_short} (${_pin_export}), but the port is pinned to "
            "${_pin_short} (${EIDOS_ALETHEIA_PIN}).  Re-run "
            "tools/pinned_checkout.py to export the pin.")
    endif()
    return()
endif()

find_package(Git QUIET)
if(GIT_EXECUTABLE)
    set(_pin_git "${GIT_EXECUTABLE}")
else()
    set(_pin_git git)
endif()

execute_process(
    COMMAND ${_pin_git} -C "${ALETHEIA_ROOT}" rev-parse HEAD
    RESULT_VARIABLE _pin_rc
    OUTPUT_VARIABLE _pin_head
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NOT _pin_rc EQUAL 0 OR NOT _pin_head)
    message(WARNING
        "aletheia pin: cannot read the revision of ${ALETHEIA_ROOT}.  "
        "The port is pinned to ${EIDOS_ALETHEIA_PIN} (docs/aletheia-pin.md).  "
        "A build against another checkout is allowed; the pin only records "
        "what the port was verified against.")
    return()
endif()

string(SUBSTRING "${EIDOS_ALETHEIA_PIN}" 0 10 _pin_short)
string(SUBSTRING "${_pin_head}" 0 10 _pin_head_short)

if(NOT _pin_head STREQUAL EIDOS_ALETHEIA_PIN)
    message(WARNING
        "aletheia pin: the checkout at ${ALETHEIA_ROOT} is at ${_pin_head_short} "
        "(${_pin_head}), but the port is pinned to ${_pin_short} "
        "(${EIDOS_ALETHEIA_PIN}).  Building against a different revision is "
        "allowed; the pin is what docs/aletheia-pin.md records and what the "
        "port was verified against.  Update cmake/aletheia_pin.cmake when the "
        "port moves to a new revision on purpose.")
endif()

# The pin names a commit.  Uncommitted changes make the sources this build
# compiles something other than that commit, even when HEAD matches it.
execute_process(
    COMMAND ${_pin_git} -C "${ALETHEIA_ROOT}" status --porcelain --untracked-files=no
    RESULT_VARIABLE _pin_dirty_rc
    OUTPUT_VARIABLE _pin_dirty
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)

if(_pin_dirty_rc EQUAL 0 AND _pin_dirty)
    string(REGEX REPLACE "\n" ";" _pin_dirty_lines "${_pin_dirty}")
    list(LENGTH _pin_dirty_lines _pin_dirty_count)
    message(WARNING
        "aletheia pin: the checkout at ${ALETHEIA_ROOT} has uncommitted changes "
        "to ${_pin_dirty_count} tracked file(s).  The pin ${_pin_short} "
        "(${EIDOS_ALETHEIA_PIN}) describes a clean tree at that revision, so the "
        "sources this build compiles are not that revision.  Building anyway.")
endif()

if(_pin_head STREQUAL EIDOS_ALETHEIA_PIN AND NOT _pin_dirty)
    message(STATUS
        "aletheia pin: ${_pin_short} at ${ALETHEIA_ROOT} "
        "(matches docs/aletheia-pin.md)")
endif()
