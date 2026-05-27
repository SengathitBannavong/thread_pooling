# Benchmark Summary

Fresh dataset generated in Phase 3; tables regenerated in Phase 5 with mean plus sample
standard deviation (`n - 1`) and CV%.

## Multi-axis Verdict

- CPU throughput: confirmed competitive, corrected from the expected `~1.01x` to `0.993x`.
  The fresh table reports mine at `6,096.4 t/s` and baseline at `6,141.6 t/s`.

- IO throughput: confirmed competitive, corrected from the expected `~0.99x` to `1.014x`.
  The fresh table reports mine at `23,733.2 t/s` and baseline at `23,400.5 t/s`.

- Worker scaling: confirmed advantage, corrected from the expected `~1.05x` at high
  worker counts to `1.031x` at 16 workers, `1.028x` at 32 workers, and `1.032x` at
  64 workers.

- Predictability: the expected IO-bound variance advantage is not supported by the
  generated table because CPU/IO CV is reported as `0.00% / 0.00%`. Queue predictability
  does show an advantage at several worker counts: queue 4w has `2.21x` stability
  advantage and queue 8w has `4.24x`.

- Queue operation feature cost: confirmed, with corrected values. Low-contention queue
  throughput is `0.699x` at 1 worker, not `~0.67x`; at 16 workers it improves to
  `0.962x`, not `~0.99x`. The priority/monitor/aging capable pool carries measurable
  overhead for pure queue operations, especially at low worker counts.

- Aging/fairness: supported. Without aging, the last low-priority task completes at
  `2493.2 ms` on average. With aging enabled, the last low-priority task completes at
  `922.5 ms` on average, reducing the starvation window by about `2.70x`. Total runtime
  stays similar: `2493.2 ms` without aging vs `2516.1 ms` with aging.

## Corrected Expected-shape Notes

- CPU: expected `~1.01x`, fresh data says `0.993x`.
- IO: expected `~0.99x`, fresh data says `1.014x`.
- Scaling 16/32/64 workers: expected `~1.05x`, fresh data says `1.031x`, `1.028x`,
  and `1.032x`.
- Queue 1 worker: expected `~0.67x`, fresh data says `0.699x`.
- Queue 16 workers: expected `~0.99x`, fresh data says `0.962x`.
- IO variance advantage: expected markedly lower CV, fresh table reports `0.00% / 0.00%`,
  so no IO CV advantage is claimed from the generated table.
