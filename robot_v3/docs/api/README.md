# robot_v3 API documentation

The public API of every header under `robot_v3/src/` is documented
inline using Doxygen comments. Run Doxygen from the `robot_v3/`
directory to generate browsable HTML:

```
cd robot_v3
doxygen Doxyfile
```

Output lands in `robot_v3/docs/api/html/`. Open `index.html` in a
browser.

## Pretty theme (optional)

The default Doxygen HTML is functional but dated. Drop in
[doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css)
for a modern look:

1. Clone or download a release of doxygen-awesome-css.
2. Copy `doxygen-awesome.css` and
   `doxygen-awesome-sidebar-only.css` into
   `robot_v3/docs/theme/`.
3. In `robot_v3/Doxyfile`, uncomment the `HTML_EXTRA_STYLESHEET`
   block (and switch `HTML_COLORSTYLE` to `AUTO_LIGHT`).
4. Re-run `doxygen Doxyfile`.

## Coverage

Each header has a top-of-file `@file` block introducing the module,
followed by per-symbol comments on every public type, function, and
non-trivial field. Implementation files (`*.cpp`) are intentionally
excluded — internal helpers stay private.

If you add a new module, follow the same pattern: a multi-paragraph
`@file` block at the top of the header explaining what the module
owns and how it composes with its peers, then concise per-symbol
comments. Keep them up to date — they're the contract other modules
rely on.
