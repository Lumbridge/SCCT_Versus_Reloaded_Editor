# SMagicEvent JSON exchange

Open an event in **View > SMagicEvent Workbench**, then click **Export JSON...**.
Share the exported file with the assistant and describe the behaviour you want.
Open the destination event and click **Import JSON...** to apply the edited file.
Use **New Event** first when you want a separate event. **Undo** restores all
imported changes in one step; save the map to retain them.

Export includes editable settings in the `SMagicEvent` category (including all
group/action fields and `StopActor`), plus `Event` and `InitialState`. It includes
the live property schema and map actor names, classes and Tags as reference
information for the person or assistant editing the file.

Import reads `format`, `version`, `class` and `properties`. It preserves the
destination actor's name, Tag, position and rotation. It does not create actors,
change linked actors, attach triggers, import assets, or interpret instructions
from the JSON. Link triggers and create target actors in the map/workbench.
`context`, `schema` and `instructions` are reference information only; validation
always uses the destination editor's live schema.

Property leaves are strings, including numbers and booleans. Every supplied
group/action must retain all its structure fields. Supplied arrays replace the
whole array; omitted top-level properties stay unchanged. Unknown properties,
wrong classes/versions, invalid timing and missing object references are rejected
before any changes are committed. An event changed while the import dialog was
open must be refreshed before retrying. Files are limited to 8 MiB and 64 nesting
levels. Actor Tags in action `Event` fields must match the intended target Tags;
they are not automatically renamed or remapped across maps.

For example, this replaces an event's groups with one action aimed at `DoorTag`:

```json
{
  "format": "scct.smagic-event",
  "version": 1,
  "class": "SBase.SMagicEvent",
  "properties": {
    "Groups": [
      {
        "Sequence": "False",
        "SequenceIndex": "0",
        "Repeat": "0",
        "EventGroup": [
          {
            "Event": "DoorTag",
            "Delay": "1.5",
            "Type": "EVT_Trigger",
            "ValidOn": "EVT_Trigger"
          }
        ]
      }
    ]
  }
}
```

Prefer editing a real export: it records the field choices supported by your
editor and the Tags available in your current map.
