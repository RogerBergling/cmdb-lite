# Dependencies, impact, and services

`cmdb-lite` models a relationship as two separate facts:

1. **What the connection is** — `runs_on`, `depends_on`, `manages`,
   `monitors`, `provided_by`, `connects_to`, and so on.
2. **What happens to the source if the target is unavailable** — the
   relationship's impact class.

Do not use one overloaded `depends_on` edge for every kind of connection. A
vCenter management link is not the same thing as a VM's runtime dependency on
its hypervisor.

## Impact classes

Impact is always read as: **if the target is unavailable, what happens to the
source?**

| Impact | Meaning | Example |
|---|---|---|
| `availability` | Hard dependency. Target down means source down. | VM → ESXi host; application → database |
| `degraded` | Soft dependency. Source continues with reduced function. | Application → SMTP; mail cannot be sent |
| `management` | Management/control-plane link only. Target down does not stop the source. | vCenter → VM |
| `monitoring` | Observability link only. | Prometheus → host |
| `informational` | Documentation or association only. | CI → group, owner, location |

Relationship types have a default impact. The relationship form pre-selects it,
but the operator can change it for each connection. The relationship table also
allows changing the impact later.

### vCenter example

```text
vcenter01   manages   vm01     impact = management
vm01        runs_on   esxi01   impact = availability
```

If vCenter is down, `vm01` keeps running. The management edge remains visible in
the topology but is excluded from the default impact view.

Do not model this as `vm01 depends_on vcenter01` with `availability` impact.

## Services

Infrastructure services such as DNS, SMTP, and NTP should be separate CIs of the
seeded `Service` type. A service is a function; a server or router is a provider
of that function.

```text
DNS Service   provided_by   dns01    impact = availability
DNS Service   provided_by   dns02    impact = availability
app01         depends_on    DNS Service   impact = availability
app02         depends_on    SMTP Service  impact = degraded
```

This is more precise than attaching `dns=true` to a server because:

- one logical service can have multiple providers;
- one server or router can provide several services;
- consumers depend on the service, not on a particular provider;
- the service has its own owner, criticality, endpoint, port, and scope;
- provider changes do not require changing every consumer.

### Seeded Service fields

| Field | Purpose |
|---|---|
| `service_type` | DNS, SMTP, NTP, DHCP, LDAP/AD, vCenter, Monitoring, Backup, Other |
| `service_endpoint` | VIP, hostname, or address clients use |
| `service_port` | Service port, for example `53` |
| `service_protocol` | TCP, UDP, TCP and UDP, or Other |
| `availability_mode` | Single, Redundant, Active/passive, or Active/active |
| `service_scope` | Site, network, zone, or other scope |

Use `provided_by` from the service to the resource that provides it. Use
`depends_on` from a consumer to the service.

### Redundancy

Multiple `provided_by` relationships represent redundant providers. The
`availability_mode` field records the intended model, but automatic evaluation
of “is the service still up with one provider left?” is a later analysis step.
The first step is to record the providers and impact correctly.

## Relationship types

Seeded types and their defaults:

| Type | Inverse | Default impact |
|---|---|---|
| `runs_on` | `hosts` | `availability` |
| `uses` | `used_by` | `availability` |
| `hosted_in` | `hosts` | `availability` |
| `supports` | `supported_by` | `informational` |
| `depends_on` | `depended_on_by` | `availability` |
| `connects_to` | `connects_to` | `informational` |
| `member_of` | `contains` | `informational` |
| `manages` | `managed_by` | `management` |
| `monitors` | `monitored_by` | `monitoring` |
| `provided_by` | `provides` | `availability` |

`supports` is deliberately informational by default because its direction is the
reverse of a dependency. When a business service depends on a supporting CI,
model it from the dependent side, for example:

```text
customer-portal   depends_on   payments-api   impact = availability
```

## Dependency Map

The Dependency Map has an **Impact classes** filter. The default is
`availability` and `degraded`, so management, monitoring, and informational
edges do not make a CI look like it will fail.

Select additional impact classes to see the full topology. Edges are labelled
with both the relationship type and impact class. `manages`, `monitors`, and
`connects_to` edges are drawn with distinct dashed styles.

## Import and export

Relationship CSV accepts an optional `impact` column:

```csv
source_ci,relationship_type,target_ci,description,impact
vm01,runs_on,esxi01,Runs on the cluster,availability
vcenter01,manages,vm01,Inventory and power operations,management
DNS Service,provided_by,dns01,Primary resolver,availability
```

If `impact` is omitted or empty during import, the relationship type's default
is used. Exported relationship CSV and JSON include the impact.

## Migration

Schema version 4 adds the impact fields, the new relationship types, and the
`Service` CI type. Existing relationships are assigned their type's default
impact. The application creates the usual pre-migration backup before opening a
version 3 database with this release.
