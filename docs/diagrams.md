# Embedded diagram editor

The **Diagrams** tab is an offline canvas for arranging CIs and creating typed
relationships. It is a lightweight `.drawio`-compatible editor, not the official
draw.io web application.

## Workflow

1. Unlock a database.
2. Open **Diagrams**. The first time, a new untitled diagram is created.
3. Choose a CI from **Add a CI** and press **Add selected CI**.
4. Drag nodes to arrange them.
5. Click one node, then another. The two selected nodes are the source and
   target.
6. Choose a relationship type and press **Connect selected nodes**.
7. Press **Save diagram**.

Connecting nodes calls the normal relationship API. It creates a real typed
relationship in the database, so the Dependency Map, graph traversal, and
relationship list update immediately. If the same `(source, type, target)` pair
already exists, the operation is rejected by the existing duplicate protection.

Removing a node from the diagram removes only the diagram's node and its
diagram edges. It does **not** delete the CI or the underlying relationship.

## Storage

Diagrams are stored in the `diagrams` table inside the encrypted SQLCipher
database. The stored content is `.drawio` XML (`mxfile` / `mxGraphModel` /
`mxCell`) and opens in diagrams.net or draw.io Desktop.

The editor preserves:

- node position and size (`180 x 64`);
- CI identity (`ci-<uuid>`);
- relationship type (`cmdbTypeId`);
- source/target edges.

The official draw.io application may normalize or add styling attributes when
it saves the file. The editor re-reads the graph structure and still displays
the nodes and edges.

## Limitations

- The official draw.io editor is not bundled; the built-in canvas is smaller and
  does not include draw.io shape libraries or cloud integrations.
- There is no automatic layout. Nodes are placed in a simple grid and can be
  dragged.
- Diagram edits do not create or delete CIs. Use the Inventory tab for that.
- Deleting a CI elsewhere leaves its node in old diagrams until the diagram is
  edited; opening a diagram with missing CIs drops the missing nodes and edges
  from the rendered canvas.
