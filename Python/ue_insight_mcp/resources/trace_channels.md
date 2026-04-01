# UE Trace Channels Reference

## Core Channels

| Channel | Description | Events |
|---|---|---|
| `cpu` | CPU timing scopes | Function/scope begin/end with timing |
| `gpu` | GPU timing events | GPU command timing |
| `frame` | Frame boundaries | Frame start/end, frame number |
| `memory` | Memory allocations | Alloc/Free events, memory tags |
| `loadtime` | Asset loading | Package/asset load begin/end/complete |
| `bookmark` | User bookmarks | Named markers in the timeline |

## Additional Channels

| Channel | Description |
|---|---|
| `counters` | Named counter values over time |
| `file` | File I/O operations |
| `log` | Log output capture |
| `object` | UObject lifecycle |
| `task` | Task graph events |
| `net` | Network replication |
| `audio` | Audio processing |
| `animation` | Animation evaluation |
| `rendering` | Rendering pipeline stages |
| `rhicommands` | RHI command buffer |
| `physics` | Physics simulation |
| `navmesh` | Navigation mesh |
| `assetregistry` | Asset Registry operations |
| `metadata` | Trace metadata (platform, build info) |

## Default Channel Set

The recommended default for general profiling:
```
cpu,gpu,frame,memory,loadtime
```

For detailed CPU profiling:
```
cpu,frame,task,object,loadtime,bookmark
```

For memory investigation:
```
memory,loadtime,object,assetregistry
```

For rendering analysis:
```
cpu,gpu,frame,rendering,rhicommands
```

## Notes

- Channels can be toggled while Trace is running
- More channels = larger .utrace file
- `bookmark` channel is lightweight and recommended always-on
- `cpu` channel has the most overhead but provides the most useful data
- Use `trace.channels.list` to see all channels registered in the current build
