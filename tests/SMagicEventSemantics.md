# SMagicEvent engine behaviour

Verified against `ChaosTheory_Editor.exe`, SHA-256
`BCE63AA2F44ACBB5104DAD09B00325CF32AE6A38EFE97AB2EBF6A44DCB9819A3`.
Addresses below are for its preferred image base `0x10e00000`.

The workbench uses reflected property offsets, dimensions, bool masks and enum
labels. It does not allocate or resize game arrays through the C++ runtime.
Property import uses the engine's `UProperty::ImportText` virtual at `+0x94`;
bounded scalar export uses `+0x90`. Dynamic groups are imported as complete
structures retaining fields the UI does not expose. The existing native
transaction buffer snapshots the owning actor, including its nested arrays.

## Activation and timing

`SMagicEvent.NativeTrigger` resolves to the native thunk at `0x111d30f0`.
The group loop at `0x111d31ab` works as follows:

- Groups with Repeat <= -1 do nothing. Zero allows unlimited cycles. Positive
  values decrement after a cycle; reaching zero changes the counter to -1.
- Sequence=False visits all EventGroup entries on each incoming activation.
- Sequence=True visits the current SequenceIndex entry only. It advances on a
  matching activation and wraps after the last entry; only wrapping consumes a
  repeat. Each step needs an activation; delays do not accumulate between steps.
- ValidOn=EVT_Trigger accepts Trigger; EVT_Untrigger accepts Untrigger. Both
  combined enum values accept either activation. A nonmatching sequential entry
  does not advance the index.
- A zero-delay entry dispatches immediately. Positive delays enqueue an event
  copy whose due time is Level.TimeSeconds + Delay (`0x111d327b`, `0x111d339a`).
  Every action scheduled by the same activation shares that time origin.
- The queue inserts at its front and dispatches through the update routine at
  `0x111d2ff0`. Queued events with equal times may run in a different order from the editor list. Use distinct delays where order matters.
- Triggering from StopActor clears the pending queue.

Dispatch at `0x111d2f10` handles the four Type enum values:

| Type | Behaviour |
| --- | --- |
| EVT_Trigger | Trigger targets |
| EVT_Untrigger | Untrigger targets |
| EVT_TriggerUntrigger | Trigger, then change this entry to the other alternating type |
| EVT_UntriggerTrigger | Untrigger, then change this entry to the other alternating type |

For delayed entries the changed Type belongs to the queued copy, which is
removed after dispatch; it does not update the authored group entry. The UI
retains native enum names in the inspector and explains this distinction.

The native fixture invokes the actual NativeTrigger thunk using an FFrame
expression stream. ProcessEvent suppresses ordinary script execution in editor
mode, so calling it alone does not test activation. Fixtures verify shared delay
origins, Repeat consumption, Sequence advancement, and ValidOn filtering.

## Creation and editing

- Actor creation uses the native paste function in one
  transaction, including position restoration and event wiring.
- CLASS_Abstract is bit 0 at UClass +0x8c, checked by StaticAllocateObject at
  `0x10fada50`. Abstract classes are omitted from creation pickers.
- Volume geometry uses native polygon export for the builder brush, or a closed
  six-face box imported as a native brush model. Builder rotation/scale/pivot
  are retained. Volumes are actors, not additive/subtractive level geometry.
- Movers in this build are mesh actors, not Brush subclasses. KeyPos/KeyRot and
  NumKeys are reflected authored arrays despite not carrying CPF_Edit. The
  workbench lets you edit them and Location/Rotation; other noneditable
  properties remain hidden. Capture uses the builder's world pose relative to
  the mover's base pose derived from its current key.
- Particle components use StaticConstructObject at
  `0x10fadf80`, with a transactional owning emitter and engine-managed reference
  array. Only map-owned, currently referenced components can be edited.
- Engine names are normalized to remove diagnostic `§(index)` annotations
  before display/import. Existing FName entries are never renamed in place.

## Limits and regression coverage

The limits are 4096 entries per array, 100,000 values per reflected root,
and 12 levels of nesting. Strings have length limits, and delays must be finite
values from 0 to 86400 seconds.
Unsupported properties are reported without writing them. Runtime group
SequenceIndex is retained but not offered as an editable control.

Run `tools/test_workflow_tools.cmd` and the isolated `-WorkflowTools` harness.
The harness saves schema diagnostics, screenshots, activation logs, and a
seven-actor test scene with two triggers. It checks compound undo/redo and exact
editable-property equality after a full save/reopen, then edits that reopened
event through the same bridge. It also tests the workbench's group button and timeline drag. Check rendering
and audio through Play Level; the workbench preview does not run gameplay.
