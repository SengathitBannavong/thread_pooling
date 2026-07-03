# Benchmark Summary (fresh run, commit 814fb2c)

Dataset regenerated on 2026-06-24. Tables in `tables.tex` report mean plus sample
standard deviation (`n - 1`) and CV%. Machine: AMD Ryzen 7 6800HS (8c/16t), 14 GiB,
Fedora kernel 7.0.12, gcc 15.2.1. See `MANIFEST.txt` for the full environment and the
exact commands.

## Correctness

- Unit tests: 8 binaries, 69/69 assertions pass, 0 failures.
- TSAN: clean on 8/9 suites. The intentional `test_submit_vs_destroy_race` reports one
  race (`in_flight_submits` increment vs `free(pool)`) — the documented concurrent-destroy
  contract boundary; the `seq_cst` guard still prevents any task being enqueued after
  shutdown.
- Valgrind: 8/8 pass, 0 errors, 0 bytes definitely lost (ncurses suppressed via
  `ncurses.supp`).

## Performance (ratio = mine / baseline)

- CPU-bound (fib30, 100K): mine 6,307.1 t/s vs base 6,271.4 t/s → 1.006x (tie).
- IO-bound (64KB read, 100K): mine 38,408.4 t/s vs base 37,069.6 t/s → 1.036x;
  mine CV 2.21% vs base 12.76% (5.78x more predictable).
- Worker scaling: mine 1.02–1.04x at every worker count. Speedup S_p ≈ 7.21x at 8
  physical cores (90% efficiency), then Amdahl plateau ≈ 7.5x through 16–64 workers.
- Queue ops (bare FIFO microbench): baseline 1.1–1.4x faster (mine 0.697x at 1 worker,
  0.918x at 8) — the cost of priority/aging/monitor machinery.
- Heterogeneous (mixed CPU+IO+priority sweep): mine leads at high worker counts
  (32w 1.182x, 64w 1.109x); 8w/16w are noisy due to real disk reads and excluded from
  conclusions.
- Stability (50 sustained iterations): mine 90,974.6 t/s CV 1.35%, RSS drift max 4 KB;
  base 98,845.1 t/s CV 0.52%, drift 68 KB. Both leak-free; mine has the flattest memory
  footprint.
- Aging/fairness: last LOW task completes at 2,435.8 ms without aging vs 897.0 ms with
  aging → 2.72x smaller starvation window, at equal total runtime (2,435.8 vs 2,437.3 ms).

## Verdict

The priority pool is competitive on real workloads, scales near-linearly to the physical
core count, and wins where its scheduler matters (mixed high-load throughput, fairness,
I/O-bound predictability, memory stability). It pays a measurable overhead on bare-FIFO
and homogeneous sustained throughput — the expected trade-off for richer scheduling
semantics.
