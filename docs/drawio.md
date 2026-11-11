# Dependency maps and draw.io

## Current implementation

The application provides:

1. Multi-seed dependency traversal in `src/core/graph.c`.
2. An SVG map rendered in the UI.
3. An **Export .drawio** action that writes an uncompressed `.drawio` file.
4. A **Diagrams** editor that stores `.drawio` XML in the encrypted database and
   creates real relationships when two nodes are connected. See
   [`diagrams.md`](diagrams.md).

The export is plaintext. Treat it with the same care as a CSV export: it is a
snapshot of part of the CMDB outside the encrypted database.

## Traversal semantics

Inputs:

- `ids`: 1–100 seed CI IDs.
- `depth`: 1–5.
- `direction`: `outgoing`, `incoming`, or `both`.
- `relationship_types`: optional list of relationship type names.
- `impacts`: optional list of impact classes (`availability`, `degraded`,
  `management`, `monitoring`, `informational`). The UI defaults to
  `availability` and `degraded`.

The graph builder uses breadth-first search:

- Seeds are depth 0.
- Outgoing traversal follows `source → target`.
- Incoming traversal follows `target → source`.
- `both` follows both directions.
- Nodes are deduplicated by ID; the first (shortest) depth wins.
- Edges are included only when both endpoints are in the result.
- A visited set makes cycles safe.
- Safety caps: 500 nodes, 5,000 edges. Truncation is reported in the response
  and shown as a warning in the UI.

## Map rendering

The UI lays nodes out in columns by depth and rows by discovery order. Each node
shows its name and CI type. Edges are labelled with the relationship type and
impact class (underscores are displayed as spaces). Seed nodes use a distinct
colour, and management/monitoring/informational edges use dashed styles.

Clicking a node opens the CI editor. This is a convenience for investigation, not
a replacement for the inventory view.

## `.drawio` format

The exporter writes uncompressed draw.io XML:

```xml
<mxfile host="cmdb-lite" ...>
  <diagram id="d-..." name="Dependency map">
    <mxGraphModel ...>
      <root>
        <mxCell id="0"/>
        <mxCell id="1" parent="0"/>
        <mxCell id="n-<ci-uuid>" value="Name&#10;Type" style="..." vertex="1" parent="1">
          <mxGeometry x="..." y="..." width="200" height="60" as="geometry"/>
        </mxCell>
        <mxCell id="e-<relationship-uuid>" value="runs on · availability" style="..." edge="1"
                parent="1" source="n-..." target="n-...">
          <mxGeometry relative="1" as="geometry"/>
        </mxCell>
      </root>
    </mxGraphModel>
  </diagram>
</mxfile>
```

Design choices:

- Uncompressed XML is readable, diffable, and easy to inspect/repair.
- `html=0` and XML escaping prevent HTML injection through CI names or
  relationship labels.
- Node and edge IDs are derived from CMDB UUIDs, so repeated exports of the same
  graph remain stable for external tooling.
- The layout is deliberately simple (depth columns). Users can rearrange the
  diagram in draw.io and re-import it later.

The file opens in diagrams.net, draw.io Desktop, and editor integrations that
support `.drawio` XML.

## Embedded editor

The embedded editor uses the built-in `.drawio`-compatible canvas:

- No remote assets or telemetry.
- A strict CSP covers the application document.
- Diagram XML is stored in the encrypted `diagrams` table.
- The C core validates the XML size and wraps the save operation in a
  transaction.
- Connecting two nodes creates a typed relationship through the normal
  relationship API; the Dependency Map reflects it.
- The editor is not the official draw.io web application, but its saved XML can
  be opened in diagrams.net and draw.io Desktop.
- Export to a plaintext `.drawio` file remains explicit and user-initiated.

Embedding a full draw.io distribution increases the application size and the
security-update surface. The pinned version must be reviewed and updated
deliberately. The editor is isolated in the webview; it does not receive the
database key or direct filesystem access.

## Security notes

- Never place credentials, passwords, or private keys in node labels or
  attributes; diagrams are part of the CMDB.
- Treat imported `.drawio` XML as untrusted input. Validate size and structure
  before storing it.
- A `.drawio` export is plaintext. Store it only where appropriate, and delete
  temporary copies.
- The embedded editor must not load remote stencils, fonts, or scripts. Bundle
  required assets locally and pin their hashes.
