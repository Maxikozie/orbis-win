# Console run log

One line per launch on the PS4, appended at the end, newest last. Raw logs, frames and recordings live in
`build-ps4-logs/` (gitignored) and are named in the line. ps4-wine's runs (a–n, x1) are in
`refs/ps4-wine/docs/ps4/LOG.md`.

## Line format

```
YYYY-MM-DD HH:MM | <milestone or sub-step> | pkg <installer filename> <build marker> [+ ours <sha7>[+dirty]] | PASS|FAIL|CRASH|NOLOG|RECORDED | <check line or first failure> | <notes; build-ps4-logs/<files>>
```

| Column | What goes in it |
|---|---|
| `pkg` | The exact installer filename. Then the package's build marker as logged (e.g. `02.16`). When we built it, our commit (`git rev-parse --short=7 HEAD`), `+dirty` with uncommitted changes in it |
| verdict | `PASS` / `FAIL`: the milestone's check. `CRASH`: a kernel fault, CE-34878-0 or an unhandled guest exception before the check (quote the first record). `NOLOG`: nothing arrived (say which channels were silent). `RECORDED`: a diagnostic launch with no rule, its reading fixed before the run |
| fifth | The check's evidence, the first failing field, or the first error line |
| sixth | What changed since the previous run, what was and was not tested, the user's word, the file names |

## Runs

