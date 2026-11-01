# app_fridge

Portable native GSP UI and transactional inventory, shared by simulator and device
entry point. Depends on public ESP-GSP 1.5.1 and generated scene bindings.
fridge_store.c has no GSP, network, filesystem or ESP-IDF dependency.

app_fridge_create(ui, &platform, &controller) copies callbacks and owns its state
and timer. It borrows ui and platform.ctx. The platform loads and durably saves
the store. Missing storage initializes empty; corrupt storage fails closed.
Save failure rolls back before publication. Call dispatch on the UI owner thread;
board tasks must queue events. No function is ISR-safe.

Capture responses must match session and capture IDs. Corrections rebuild stock
from the transaction baseline: original 5, captured 2, corrected to 3 produces 8.
Removal consumes the oldest matching entry date first; insufficient stock rejects
atomically. One-level undo restores the baseline; a new transaction replaces undo.

Names: UTF-8, at most 63 bytes. Dates: YYYY-MM-DD, years 2000-2199. Quantities:
integer thousandths internally, 0-100000000 (meaning 0-100000). JSON/UI use decimal
quantities without units. Capacity: 96 stock/date records, 24 items per capture.

The host owns one opaque controller, frame buffer and ASCII relative data path.
It reads a single-command mailbox every 40 ms. Python serializes operations and
waits for native acknowledgement; it never writes inventory. File commits use
checksum, flush and atomic replace. Never run concurrent mailbox writers.

Stop dispatch before destroy. Lists retain callbacks until UI destruction, so
never resume a UI after freeing the controller. On partial create failure, close
the borrowed UI immediately. Canvas buffers remain borrowed until release. The
simulator quiesces polling before teardown; device lifetime is reboot.

The device composes app_board, app_cloud and app_voice. Camera pages 1 (ready)
and 2 (recognizing) retain the live preview. Every new capture begins a fresh
transaction; APPLY returns to page 1 and publishes a feedback ID only after
persistence. No result-confirmation page is entered. Native timers animate up to
three floating item badges, with authored opacity variants and bounded movement.
Corrections show only changed items. Operation IDs distinguish an entry greeting
from subsequent captures. Persisted store format is version 2. Decode accepts v1
without writing, preserves quantities/dates/baseline, assigns product IDs and
infers category/brand only from explicit legacy names. The next durable save
atomically replaces the same inventory key; v1 firmware cannot read the new data.

OBSERVE receives image-only category/brand/evidence, then fridge_match resolves
against the transaction baseline. An unbranded OUT observation can automatically
select a unique category candidate; ambiguity opens three paged choices over the
camera. Uncertain drinks always require selection, including a sole candidate.
SELECT/MORE are session/capture-scoped; retake/finish cancels pending observations.
No partial batch is committed while any item is unresolved. Explicit selection
may enrich a generic IN product's brand; undo restores its original metadata.
Names omit flavor on new observations. Product IDs, not display names, join lots.
Category/brand limits are 23/19 UTF-8 bytes. IDs are locally assigned positive
integers up to INT32_MAX. Match results expose up to 97 candidates (96 + new IN).

The device scene matches simulator geometry. Fixed glyphs fit ui_apps; an embedded
TTF in main_app supplies dynamic food names. `tests/continuous_flow.py` exercises
native touch/camera flow against an explicitly isolated simulator fixture.
