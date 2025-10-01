# Run

To run:
```
mkdir build && cd build && cmake .. && make && ./cache_calculator
```

By default, debug output to stderr is enabled. It is controlled by the `DEBUG` definition.

The first and the only positional argument accepts a double value indicating the "steepness" of a jump: 
how large the difference in access time to consider when determining cache size and associativity. 
If the results are too noisy, increasing it may help. Typical values: `[1.25, 2.5]`

The benchmark will output supposed cache characteristics to the stdout:
```
Cache size is 131072 bytes
Cache associativity is 8
Cache line size: 128 bytes.
```

# Results

## Mac M1

```
Cache size is 131072 bytes
Cache associativity is 8
Cache line size: 128 bytes.
```

The available data is a bit confusing, but most sources converge on l1d cache being 128 KB and cache line 128 bytes:
```
# sysctl -a 
hw.cachelinesize: 128
hw.perflevel0.l1dcachesize: 131072
```

## Intel i5-1135g7

```
Cache size is 49152 bytes
Cache associativity is 12
Cache line size: 64 bytes.
```

Alings with the device data available:
```
# cat /sys/devices/system/cpu/cpu0/cache/index0/size
48K
# cat /sys/devices/system/cpu/cpu0/cache/index0/ways_of_associativity
12
# cat /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size
64
```

However, cache line size detection is much less reliable for some reason and may require a few runs to stabilize. 
