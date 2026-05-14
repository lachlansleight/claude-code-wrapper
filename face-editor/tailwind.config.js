const colors = require("tailwindcss/colors");

/** @type {import('tailwindcss').Config} */
module.exports = {
  content: ["./src/**/*.{js,ts,jsx,tsx,mdx}"],
  theme: {
    extend: {
      colors: {
        primary: colors.blue,
        secondary: colors.orange,
        neutral: colors.zinc,
        /** Face simulator / editor (semantic tokens, e.g. `text-face-muted`, `bg-face-panel`) */
        face: {
          bg: "#0f1115",
          panel: "#161a22",
          "panel-2": "#1d222c",
          border: "#2a3140",
          text: "#e6e8ee",
          muted: "#8b93a7",
          accent: "#6ea8ff",
          good: "#58d68d",
          bad: "#ff7b7b",
          canvas: "#0b0d12",
          hole: "#000000",
        },
      },
      height: {
        header: "3rem",
        footer: "3.5rem",
      },
      width: {
        label: "8rem",
        control: "calc(100% - 8rem)",
      },
      padding: {
        label: "8rem",
      },
      margin: {
        label: "8rem",
      },
      minHeight: (theme) => ({
        main: `calc(100vh - ${theme("height.header")} - ${theme("height.footer")})`,
        inner: `calc(100vh - ${theme("height.header")} - ${theme(
          "height.footer",
        )} - 4.5rem)`,
      }),
    },
  },
  plugins: [],
};
