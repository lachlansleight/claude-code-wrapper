// Shared navigation header for the /control panels. Inject by including
// this script and adding `data-control-page="<key>"` to <body>.
(function () {
  const TOOLS = [
    { key: "control", label: "Control", href: "index.html" },
    { key: "simulator", label: "Face Simulator", href: "simulator.html" },
    { key: "expressions", label: "Expressions", href: "expressions.html" },
  ];

  const css = `
    .ctl-nav {
      display: flex;
      align-items: center;
      gap: 0.75em;
      padding: 0.55em 1em;
      margin: 0 -1em 1em;
      background: #11141b;
      border-bottom: 1px solid #2a3140;
      font-family: system-ui, -apple-system, sans-serif;
      flex-wrap: wrap;
    }
    .ctl-nav-brand {
      font-weight: 700;
      color: #e6e8ee;
      letter-spacing: 0.02em;
      margin-right: 0.5em;
    }
    .ctl-nav a {
      color: #8b93a7;
      text-decoration: none;
      padding: 0.3em 0.65em;
      border-radius: 4px;
      font-size: 0.9em;
    }
    .ctl-nav a:hover { color: #e6e8ee; background: #1d222c; }
    .ctl-nav a.active { color: #6ea8ff; background: #1d222c; }
  `;

  const styleEl = document.createElement("style");
  styleEl.textContent = css;
  document.head.appendChild(styleEl);

  document.addEventListener("DOMContentLoaded", () => {
    const active = document.body.dataset.controlPage || "";
    const nav = document.createElement("nav");
    nav.className = "ctl-nav";
    nav.innerHTML =
      '<span class="ctl-nav-brand">Robot Control</span>' +
      TOOLS.map(
        (t) =>
          `<a href="${t.href}"${t.key === active ? ' class="active"' : ""}>${t.label}</a>`,
      ).join("");
    document.body.insertBefore(nav, document.body.firstChild);
  });
})();
